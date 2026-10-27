#include "editor/native/editor_bridge.h"

#include "editor/native/camera_input.hpp"

#include "core/log.hpp"
#include "platform/text.hpp"
#include "platform/window.hpp"
#include "renderer/scene_renderer.hpp"
#include "rhi/device.hpp"
#include "rhi/runtime.hpp"
#include "rhi/swapchain.hpp"

#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

static_assert(sizeof(rl_viewport_stats) == 184 && sizeof(rl_viewport_close_report) == 24);
namespace
{
using namespace renderlab;
thread_local std::string lastError;
using Clock = std::chrono::steady_clock;
uint64_t ElapsedNs(Clock::time_point start)
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
}
struct Failure : std::runtime_error
{
    Failure(rl_status status, const char* message) : std::runtime_error(message), status(status) {}
    rl_status status;
};
void Require(bool condition, const char* message)
{
    if (!condition)
        throw Failure(RL_INVALID_ARGUMENT, message);
}
struct Apartment
{
    Apartment()
    {
        const auto result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(result))
            throw std::runtime_error("The embedded viewport requires the UI thread's COM STA");
    }
    ~Apartment()
    {
        CoUninitialize();
    }
};
struct ChildWindow
{
    HWND value = nullptr;
    void Reset()
    {
        if (value && IsWindow(value))
            DestroyWindow(value);
        value = nullptr;
    }
    ~ChildWindow()
    {
        Reset();
    }
};
struct Session
{
    const DWORD owner = GetCurrentThreadId();
    Apartment apartment;
    Logger logger;
    ChildWindow child;
    std::unique_ptr<rhi::Device> device;
    std::unique_ptr<rhi::Runtime> runtime;
    std::unique_ptr<renderer::SceneRenderer> scene;
    std::unique_ptr<rhi::Swapchain> swapchain;
    renderer::SceneRenderOptions options;
    scene::Transform initialCamera;
    std::array<uint64_t, 2> completions{};
    std::array<rendergraph::Submission::GpuProfile, 2> profiles;
    std::array<uint64_t, 2> profileFrames{};
    std::vector<bool> initialized;
    std::array<bool, 256> keys{};
    rl_viewport_stats stats{};
    rl_viewport_close_report closed{};
    POINT mouse{};
    float yaw = 0, pitch = 0;
    bool dragging = false;

    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM w, LPARAM l)
    {
        auto* self = reinterpret_cast<Session*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            self = static_cast<Session*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self)
        {
            switch (message)
            {
            case WM_LBUTTONDOWN:
                SetFocus(window);
                return 0;
            case WM_RBUTTONDOWN:
                SetFocus(window);
                SetCapture(window);
                self->dragging = true;
                GetCursorPos(&self->mouse);
                return 0;
            case WM_RBUTTONUP:
                if (GetCapture() == window)
                    ReleaseCapture();
                self->dragging = false;
                return 0;
            case WM_MOUSEMOVE:
                if (self->dragging && GetCapture() == window)
                {
                    POINT point;
                    GetCursorPos(&point);
                    self->yaw += (point.x - self->mouse.x) * .15f;
                    self->pitch += (point.y - self->mouse.y) * .15f;
                    self->mouse = point;
                }
                return 0;
            case WM_KEYDOWN:
            case WM_KEYUP:
                if (w < self->keys.size())
                    self->keys[w] = message == WM_KEYDOWN;
                return 0;
            case WM_KILLFOCUS:
                self->keys.fill(false);
                self->yaw = self->pitch = 0;
                if (GetCapture() == window)
                    ReleaseCapture();
                self->dragging = false;
                return 0;
            case WM_CAPTURECHANGED:
                self->dragging = false;
                self->yaw = self->pitch = 0;
                return 0;
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT:
            {
                PAINTSTRUCT paint;
                BeginPaint(window, &paint);
                EndPaint(window, &paint);
                return 0;
            }
            case WM_NCDESTROY:
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                break;
            }
        }
        return DefWindowProcW(window, message, w, l);
    }
    Session(HWND parent, uint32_t backend, bool validation, bool profiling,
            const std::filesystem::path& project, const std::filesystem::path& shaders,
            const std::filesystem::path& log)
        : logger(log)
    {
        static const auto registered = []
        {
            WNDCLASSW type{};
            type.lpfnWndProc = Procedure;
            type.hInstance = GetModuleHandleW(nullptr);
            type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            type.lpszClassName = L"RenderLab.EmbeddedViewport";
            if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                throw std::runtime_error("Embedded viewport class registration failed");
            return true;
        }();
        (void)registered;
        child.value = CreateWindowExW(0, L"RenderLab.EmbeddedViewport", L"RenderLab viewport",
                                      WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0,
                                      0, 1, 1, parent, nullptr, GetModuleHandleW(nullptr), this);
        if (!child.value)
            throw std::runtime_error("Embedded viewport child creation failed");
        device =
            rhi::CreateDevice({backend == 0 ? rhi::Backend::Dx12 : rhi::Backend::Vulkan,
                               validation ? rhi::ValidationMode::On : rhi::ValidationMode::Off},
                              logger);
        runtime = device->CreateRuntime();
        runtime->SetAllocationBudget(2048ull * 1024 * 1024);
        scene = std::make_unique<renderer::SceneRenderer>(
            *runtime, backend == 0 ? shader::Target::Dxil : shader::Target::Spirv, project,
            shaders);
        initialCamera = scene->Document().Get(scene->Document().activeCamera).transform;
        swapchain = runtime->CreateSwapchain({child.value, GetModuleHandleW(nullptr)}, 1, 1);
        options.pipeline = renderer::PipelineType::Deferred;
        options.profile = profiling;
        stats.validation_enabled = device->GetReport().validationEnabled;
        stats.profiling_enabled = profiling;
        options.shadowResolution = 128;
        scene->ValidatePipeline(options.pipeline);
        logger.Write(LogLevel::Info, "Embedded child HWND created; UI owns the message loop");
    }
    ~Session()
    {
        try
        {
            Close();
        }
        catch (...)
        {
        }
    }
    void Tick(float dt, bool paused)
    {
        const auto tickStart = Clock::now();
        Require(IsWindow(child.value), "Viewport child HWND has already been destroyed");
        RECT rect;
        if (!GetClientRect(child.value, &rect))
            throw std::runtime_error("Cannot query embedded viewport client pixels");
        const auto width = static_cast<uint32_t>(std::max<LONG>(0, rect.right));
        const auto height = static_cast<uint32_t>(std::max<LONG>(0, rect.bottom));
        stats.dpi = GetDpiForWindow(child.value);
        stats.focused = GetFocus() == child.value;
        stats.captured = GetCapture() == child.value;
        stats.pressed_keys = static_cast<uint32_t>(std::count(keys.begin(), keys.end(), true));
        if (paused || !IsWindowVisible(child.value) ||
            IsIconic(GetAncestor(child.value, GA_ROOT)) || !width || !height)
        {
            ++stats.paused_ticks;
            return;
        }
        auto start = Clock::now();
        device->PollDiagnostics();
        stats.diagnostics_ns = ElapsedNs(start);
        stats.resize_ns = 0;
        if (stats.width != width || stats.height != height)
        {
            start = Clock::now();
            runtime->WaitIdle();
            swapchain->Resize(width, height);
            initialized.clear();
            stats.width = width;
            stats.height = height;
            ++stats.resizes;
            stats.resize_ns = ElapsedNs(start);
        }
        const auto slot = stats.frames % completions.size();
        auto& completion = completions[slot];
        start = Clock::now();
        runtime->Wait(completion);
        stats.fence_wait_ns = ElapsedNs(start);
        if (profiles[slot].ticks)
        {
            double nanoseconds = 0;
            for (const auto& timing : profiles[slot].Read(*runtime))
                nanoseconds += timing.nanoseconds;
            stats.gpu_ns = static_cast<uint64_t>(std::max(0.0, nanoseconds));
            stats.gpu_frame = profileFrames[slot];
            profiles[slot] = {};
        }
        start = Clock::now();
        const auto acquired = swapchain->Acquire();
        stats.acquire_ns = ElapsedNs(start);
        if (!acquired.texture)
        {
            runtime->WaitIdle();
            swapchain->Resize(width, height);
            initialized.clear();
            return;
        }
        if (initialized.size() <= acquired.index)
            initialized.resize(acquired.index + 1);
        const auto initial =
            initialized[acquired.index] ? rhi::State::Present : rhi::State::Undefined;
        if (stats.focused)
        {
            const auto axis = [&](size_t positive, size_t negative)
            { return float(keys[positive]) - float(keys[negative]); };
            const math::Vec3 motion{3 * dt * axis('D', 'A'), 3 * dt * axis('E', 'Q'),
                                    3 * dt * axis('S', 'W')};
            const auto rotation = editor::CameraLookDelta(60 * dt * axis(VK_RIGHT, VK_LEFT) + yaw,
                                                          60 * dt * axis(VK_DOWN, VK_UP) + pitch);
            if (stats.pressed_keys || yaw != 0 || pitch != 0)
                ++stats.input_frames;
            scene->Document().MoveCamera(motion, rotation);
        }
        yaw = pitch = 0;
        start = Clock::now();
        auto result = scene->Render(acquired.texture, initial, swapchain.get(), false, {}, options);
        stats.render_ns = ElapsedNs(start);
        stats.graph_compile_ns = result.compileNs;
        stats.graph_prepare_ns = result.prepareNs;
        stats.graph_record_ns = result.recordNs;
        completion = result.completion;
        profiles[slot] = std::move(result.profile);
        profileFrames[slot] = stats.frames + 1;
        stats.reflection_samples = result.reflectionSamples;
        initialized[acquired.index] = true;
        start = Clock::now();
        swapchain->Present();
        stats.present_ns = ElapsedNs(start);
        ++stats.frames;
        stats.pipeline = static_cast<uint32_t>(options.pipeline);
        stats.native_bytes = runtime->GetMemoryStats().used;
        stats.peak_bytes = runtime->GetMemoryStats().highWater;
        stats.tick_ns = ElapsedNs(tickStart);
    }
    rl_viewport_close_report Close()
    {
        if (runtime)
        {
            runtime->WaitIdle();
            profiles = {};
            swapchain.reset();
            scene.reset();
            runtime->WaitIdle();
            closed.remaining_bytes = runtime->GetMemoryStats().used;
            closed.peak_bytes = runtime->GetMemoryStats().highWater;
            runtime.reset();
            device->Shutdown();
            closed.validation_errors = device->GetValidationErrorCount();
            device.reset();
            logger.Write(LogLevel::Info,
                         std::format("Embedded shutdown: errors={}, remaining={}",
                                     closed.validation_errors, closed.remaining_bytes));
        }
        child.Reset();
        return closed;
    }
};
struct Slot
{
    uint32_t generation = 1;
    std::unique_ptr<Session> session;
};
std::array<Slot, 4> sessions;
std::mutex sessionsMutex;
Session& Find(rl_viewport_handle handle)
{
    const auto index = static_cast<uint32_t>(handle);
    const auto generation = static_cast<uint32_t>(handle >> 32);
    if (!index || index > sessions.size() || !sessions[index - 1].session ||
        sessions[index - 1].generation != generation)
        throw Failure(RL_INVALID_HANDLE, "Viewport handle is stale or invalid");
    auto& session = *sessions[index - 1].session;
    if (session.owner != GetCurrentThreadId())
        throw Failure(RL_WRONG_THREAD, "Viewport call must run on its creating UI thread");
    return session;
}
template <class F> rl_status Guard(F&& operation)
{
    try
    {
        std::lock_guard lock(sessionsMutex);
        operation();
        lastError.clear();
        return RL_OK;
    }
    catch (const Failure& error)
    {
        lastError = error.what();
        return error.status;
    }
    catch (const std::exception& error)
    {
        lastError = error.what();
        return RL_NATIVE_ERROR;
    }
    catch (...)
    {
        lastError = "Unknown native viewport error";
        return RL_NATIVE_ERROR;
    }
}
} // namespace

uint32_t RENDERLAB_EDITOR_CALL rl_editor_abi_version()
{
    return 2;
}
rl_status RENDERLAB_EDITOR_CALL rl_viewport_create(void* parent, uint32_t backend,
                                                   uint32_t validation, uint32_t profiling,
                                                   const char* project, const char* shaders,
                                                   const char* log, rl_viewport_handle* handle,
                                                   void** child)
{
    if (handle)
        *handle = 0;
    if (child)
        *child = nullptr;
    return Guard(
        [&]
        {
            Require(parent && IsWindow(static_cast<HWND>(parent)) && backend <= 1 &&
                        validation <= 1 && profiling <= 1 && project && shaders && log && handle &&
                        child,
                    "Invalid viewport creation arguments");
            Require(GetWindowThreadProcessId(static_cast<HWND>(parent), nullptr) ==
                        GetCurrentThreadId(),
                    "Native parent must belong to the creating UI thread");
            for (size_t i = 0; i < sessions.size(); ++i)
                if (!sessions[i].session)
                {
                    auto session = std::make_unique<Session>(
                        static_cast<HWND>(parent), backend, validation != 0, profiling != 0,
                        platform::Utf8ToWide(project), platform::Utf8ToWide(shaders),
                        platform::Utf8ToWide(log));
                    *handle = (uint64_t(sessions[i].generation) << 32) | (i + 1);
                    *child = session->child.value;
                    sessions[i].session = std::move(session);
                    return;
                }
            throw std::runtime_error("Viewport prototype supports at most four live sessions");
        });
}
rl_status RENDERLAB_EDITOR_CALL rl_viewport_tick(rl_viewport_handle handle, float dt,
                                                 uint32_t paused)
{
    return Guard(
        [&]
        {
            Require(std::isfinite(dt) && dt >= 0 && paused <= 1, "Invalid viewport tick arguments");
            Find(handle).Tick(std::min(dt, .1f), paused != 0);
        });
}
rl_status RENDERLAB_EDITOR_CALL rl_viewport_configure(rl_viewport_handle handle, uint32_t pipeline,
                                                      float exposure, uint32_t reset)
{
    return Guard(
        [&]
        {
            Require(pipeline >= 1 && pipeline <= 3 && std::isfinite(exposure) && exposure >= -10 &&
                        exposure <= 10 && reset <= 1,
                    "Invalid viewport settings");
            auto& session = Find(handle);
            const auto selected = static_cast<renderer::PipelineType>(pipeline);
            session.scene->ValidatePipeline(selected);
            session.options.pipeline = selected;
            session.scene->Document().exposure = exposure;
            if (reset)
                session.scene->Document().Get(session.scene->Document().activeCamera).transform =
                    session.initialCamera;
        });
}
rl_status RENDERLAB_EDITOR_CALL rl_viewport_get_stats(rl_viewport_handle handle,
                                                      rl_viewport_stats* stats)
{
    return Guard(
        [&]
        {
            Require(stats != nullptr, "Stats output is null");
            *stats = Find(handle).stats;
        });
}
rl_status RENDERLAB_EDITOR_CALL rl_viewport_destroy(rl_viewport_handle handle,
                                                    rl_viewport_close_report* report)
{
    return Guard(
        [&]
        {
            Require(report != nullptr, "Close report is null");
            *report = Find(handle).Close();
            auto& slot = sessions[static_cast<uint32_t>(handle) - 1];
            slot.session.reset();
            if (++slot.generation == 0)
                ++slot.generation;
        });
}
rl_status RENDERLAB_EDITOR_CALL rl_editor_last_error(char* output, uint32_t capacity,
                                                     uint32_t* required)
{
    if (!required)
        return RL_INVALID_ARGUMENT;
    *required = static_cast<uint32_t>(lastError.size() + 1);
    if (!output || capacity < *required)
        return RL_BUFFER_TOO_SMALL;
    std::memcpy(output, lastError.c_str(), *required);
    return RL_OK;
}
