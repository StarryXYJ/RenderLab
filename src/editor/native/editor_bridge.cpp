#include "editor/native/editor_bridge.h"

#include "editor/native/camera_input.hpp"
#include "editor/native/viewport_config.hpp"

#include "core/log.hpp"
#include "platform/text.hpp"
#include "platform/window.hpp"
#include "renderer/raytracing.hpp"
#include "renderer/scene_renderer.hpp"
#include "rhi/device.hpp"
#include "rhi/runtime.hpp"
#include "rhi/swapchain.hpp"

#include <Windows.h>
#include <nlohmann/json.hpp>
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
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

static_assert(sizeof(rl_viewport_stats) == 256 && sizeof(rl_viewport_close_report) == 24);
static_assert(sizeof(rl_viewport_config) == 112 && sizeof(rl_viewport_config_state) == 272);
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
struct FatalConfiguration : std::runtime_error
{
    using std::runtime_error::runtime_error;
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
        const auto result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(result))
            throw std::runtime_error("The render thread requires COM MTA");
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
struct NativeHandle
{
    HANDLE value;
    explicit NativeHandle(HANDLE handle) : value(handle)
    {
        if (!value)
            throw std::runtime_error("Cannot create viewport scheduling handle");
    }
    ~NativeHandle()
    {
        CloseHandle(value);
    }
    NativeHandle(const NativeHandle&) = delete;
    NativeHandle& operator=(const NativeHandle&) = delete;
};
struct InputSnapshot
{
    uint32_t width = 0, height = 0, dpi = 96;
    bool focused = false, captured = false, paused = true, visible = false;
    std::array<bool, 256> keys{};
    float yaw = 0, pitch = 0;
    uint64_t paints = 0;
};
// Constructed, used and destroyed exclusively on the render thread.
struct RenderState
{
    Apartment apartment;
    Logger& logger;
    HWND child;
    std::unique_ptr<rhi::Device> device;
    std::unique_ptr<rhi::Runtime> runtime;
    std::unique_ptr<renderer::SceneRenderer> scene;
    std::unique_ptr<rhi::Swapchain> swapchain;
    renderer::SceneRenderOptions options;
    scene::Transform initialCamera;
    float cameraSpeed = 3;
    std::array<uint64_t, 2> completions{};
    std::array<rendergraph::Submission::GpuProfile, 2> profiles;
    std::array<uint64_t, 2> profileFrames{};
    std::array<uint64_t, 2> profileConfigs{};
    uint64_t configRevision = 0;
    std::vector<bool> initialized;
    std::array<bool, 256> keys{};
    rl_viewport_stats stats{};
    rl_viewport_close_report closed{};
    float yaw = 0, pitch = 0;

    RenderState(HWND window, uint32_t backend, bool validation, bool profiling,
                const std::filesystem::path& project, const std::filesystem::path& shaders,
                Logger& output, bool vsync)
        : logger(output), child(window)
    {
        device =
            rhi::CreateDevice({backend == 0 ? rhi::Backend::Dx12 : rhi::Backend::Vulkan,
                               validation ? rhi::ValidationMode::On : rhi::ValidationMode::Off},
                              logger);
        runtime = device->CreateRuntime();
        const auto& report = device->GetReport();
        logger.Write(LogLevel::Info,
                     std::format("Render owner thread {}: {} / driver {}", GetCurrentThreadId(),
                                 report.adapters.at(report.selectedAdapter).name,
                                 report.driverVersion));
        runtime->SetAllocationBudget(2048ull * 1024 * 1024);
        scene = std::make_unique<renderer::SceneRenderer>(
            *runtime, backend == 0 ? shader::Target::Dxil : shader::Target::Spirv, project,
            shaders);
        initialCamera = scene->Document().Get(scene->Document().activeCamera).transform;
        swapchain = runtime->CreateSwapchain({child, GetModuleHandleW(nullptr)}, 1, 1, vsync);
        options.pipeline = renderer::PipelineType::Deferred;
        options.profile = profiling;
        stats.validation_enabled = device->GetReport().validationEnabled;
        stats.profiling_enabled = profiling;
        options.shadowResolution = 1024;
        logger.Write(LogLevel::Info,
                     "Embedded child HWND created; UI owns HWND/input; render thread owns Runtime");
    }
    ~RenderState()
    {
        try
        {
            Close();
        }
        catch (...)
        {
        }
    }
    void Tick(float dt, const InputSnapshot& input)
    {
        const auto tickStart = Clock::now();
        const auto width = input.width, height = input.height;
        stats.dpi = input.dpi;
        stats.focused = input.focused;
        stats.captured = input.captured;
        stats.pressed_keys =
            static_cast<uint32_t>(std::count(input.keys.begin(), input.keys.end(), true));
        keys = input.keys;
        yaw = input.yaw;
        pitch = input.pitch;
        if (input.paused || !width || !height)
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
            stats.gpu_config_revision = profileConfigs[slot];
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
            const math::Vec3 motion{cameraSpeed * dt * axis('D', 'A'),
                                    cameraSpeed * dt * axis('E', 'Q'),
                                    cameraSpeed * dt * axis('S', 'W')};
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
        stats.scene_prepare_ns = result.scenePrepareNs;
        stats.graph_compile_ns = result.compileNs;
        stats.graph_prepare_ns = result.prepareNs;
        stats.graph_record_ns = result.recordNs;
        completion = result.completion;
        profiles[slot] = std::move(result.profile);
        profileFrames[slot] = stats.frames + 1;
        profileConfigs[slot] = configRevision;
        stats.reflection_samples = result.reflectionSamples;
        initialized[acquired.index] = true;
        start = Clock::now();
        swapchain->Present();
        stats.present_ns = ElapsedNs(start);
        ++stats.frames;
        stats.config_revision = configRevision;
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

        return closed;
    }
};

// UI thread owns the child window. A bounded latest-value mailbox separates the owners.
struct Session
{
    const DWORD owner = GetCurrentThreadId();
    const uint64_t identity;
    Logger logger;
    ChildWindow child;
    std::array<bool, 256> keys{};
    POINT mouse{};
    float yaw = 0, pitch = 0;
    bool dragging = false, closing = false;
    uint64_t paints = 0;
    std::mutex mutex;
    NativeHandle wake{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    bool stop = false, ready = false;
    InputSnapshot input;
    uint32_t pipeline = 2;
    float exposure = 0;
    uint64_t revision = 1, resets = 0;
    uint64_t steps = 0;
    rl_viewport_config configuration{}, defaults{};
    rl_viewport_config_state configurationState{sizeof(rl_viewport_config_state), 1};
    std::string configurationError, capabilities;
    std::array<std::string, 3> pipelineErrors;
    std::string error;
    std::string pendingLogs;
    scene::Transform camera;
    std::optional<scene::Transform> requestedCamera;
    rl_viewport_stats stats{};
    rl_viewport_close_report closed{};
    const Clock::time_point origin = Clock::now();
    std::array<rl_viewport_stats, 512> completedFrames{};
    size_t firstFrame = 0, frameCount = 0;
    uint64_t droppedSamples = 0;
    std::thread worker;

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
                ++self->paints;
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
    Session(uint64_t id, HWND parent, uint32_t backend, bool validation, bool profiling,
            uint32_t targetFps, bool continuous, const std::filesystem::path& project,
            const std::filesystem::path& shaders, const std::filesystem::path& log)
        : identity(id), logger(log)
    {
        logger.EnableMirror();
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
        worker = std::thread(
            [this, backend, validation, profiling, project, shaders, targetFps, continuous]
            {
                std::unique_ptr<RenderState> state;
                try
                {
                    NativeHandle timer{CreateWaitableTimerExW(
                        nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS)};
                    state =
                        std::make_unique<RenderState>(child.value, backend, validation, profiling,
                                                      project, shaders, logger, targetFps != 0);
                    auto& render = *state;
                    std::array<std::string, 3> failures;
                    for (uint32_t p = 1; p <= 3; ++p)
                    {
                        try
                        {
                            render.scene->ValidatePipeline(static_cast<renderer::PipelineType>(p));
                        }
                        catch (const std::exception& failure)
                        {
                            failures[p - 1] = failure.what();
                        }
                    }
                    scene::Extractor configExtractor;
                    const auto sourceSnapshot = configExtractor.Extract(render.scene->Document(), 1,
                                                                        render.scene->Models());
                    auto activeConfig =
                        editor::DefaultConfig(sourceSnapshot, targetFps, continuous, profiling);
                    nlohmann::json capabilityData{
                        {"schemaVersion", 1},
                        {"backend", backend == 0 ? "dx12" : "vulkan"},
                        {"adapter", render.device->GetReport()
                                        .adapters.at(render.device->GetReport().selectedAdapter)
                                        .name},
                        {"driver", render.device->GetReport().driverVersion},
                        {"asyncCompute", render.runtime->SupportsAsyncCompute()},
                        {"instanceCapacity", renderer::IndexedInstanceCapacity},
                        {"textureCapacity", renderer::IndexedTextureCapacity},
                        {"pipelines", nlohmann::json::array()},
                        {"submissions", nlohmann::json::array()}};
                    for (uint32_t p = 1; p <= 3; ++p)
                        capabilityData["pipelines"].push_back(
                            {{"value", p},
                             {"available", failures[p - 1].empty()},
                             {"reason", failures[p - 1]}});
                    for (uint32_t mode = 0; mode < 4; ++mode)
                    {
                        std::string reason;
                        auto candidate = activeConfig;
                        candidate.pipeline = 1;
                        candidate.submission = mode;
                        try
                        {
                            render.scene->ValidateOptions(
                                editor::RenderOptions(candidate, sourceSnapshot.light));
                        }
                        catch (const std::exception& failure)
                        {
                            reason = failure.what();
                        }
                        capabilityData["submissions"].push_back(
                            {{"value", mode}, {"available", reason.empty()}, {"reason", reason}});
                    }
                    {
                        std::lock_guard lock(mutex);
                        pipelineErrors = std::move(failures);
                        defaults = activeConfig;
                        configuration = defaults;
                        configuration.pipeline = pipeline;
                        configuration.exposure = exposure;
                        configuration.request_id = revision;
                        configurationState.requested = configuration;
                        configurationState.requested_id = revision;
                        configurationState.status = 1;
                        configurationState.ready = 1;
                        capabilities = capabilityData.dump();
                        ready = true;
                        camera = render.initialCamera;
                    }
                    uint64_t prepared = 0, appliedResets = 0, completedSteps = 0;
                    auto previous = Clock::now();
                    auto period =
                        std::chrono::nanoseconds(targetFps ? 1'000'000'000 / targetFps : 0);
                    auto deadline = previous;
                    auto dimensionsChanged = previous;
                    uint32_t pendingWidth = 0, pendingHeight = 0;
                    uint64_t painted = 0;
                    bool dirty = true, wasPaused = true;
                    float pendingYaw = 0, pendingPitch = 0;
                    std::optional<Clock::time_point> previousCompleted;
                    while (true)
                    {
                        const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                   deadline - Clock::now())
                                                   .count();
                        if (remaining > 0)
                        {
                            LARGE_INTEGER due{};
                            due.QuadPart = -std::max<int64_t>(1, (remaining + 99) / 100);
                            if (!SetWaitableTimer(timer.value, &due, 0, nullptr, nullptr, FALSE))
                                throw std::runtime_error("Cannot arm viewport frame timer");
                            const HANDLE handles[] = {wake.value, timer.value};
                            if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_FAILED)
                                throw std::runtime_error("Viewport scheduling wait failed");
                        }
                        InputSnapshot snapshot;
                        uint32_t selected;
                        rl_viewport_config selectedConfig;
                        uint64_t selectedRevision, selectedResets, selectedSteps;
                        {
                            std::lock_guard lock(mutex);
                            if (stop)
                                break;
                            snapshot = input;
                            if (requestedCamera)
                            {
                                render.scene->Document()
                                    .Get(render.scene->Document().activeCamera)
                                    .transform = *requestedCamera;
                                requestedCamera.reset();
                                dirty = true;
                            }
                            input.yaw = input.pitch = 0;
                            selectedConfig = configuration;
                            selectedRevision = configuration.request_id;
                            selectedResets = resets;
                            selectedSteps = steps;
                        }
                        if (selectedRevision != prepared)
                        {
                            try
                            {
                                editor::ValidateConfig(selectedConfig);
                                auto nextOptions =
                                    editor::RenderOptions(selectedConfig, sourceSnapshot.light);
                                render.scene->ValidateOptions(nextOptions);
                                if ((activeConfig.target_fps == 0) !=
                                    (selectedConfig.target_fps == 0))
                                {
                                    render.runtime->WaitIdle();
                                    render.swapchain.reset();
                                    try
                                    {
                                        render.swapchain = render.runtime->CreateSwapchain(
                                            {child.value, GetModuleHandleW(nullptr)}, 1, 1,
                                            selectedConfig.target_fps != 0);
                                    }
                                    catch (const std::exception& failure)
                                    {
                                        const auto reason = std::string(failure.what());
                                        try
                                        {
                                            render.swapchain = render.runtime->CreateSwapchain(
                                                {child.value, GetModuleHandleW(nullptr)}, 1, 1,
                                                activeConfig.target_fps != 0);
                                        }
                                        catch (...)
                                        {
                                            throw FatalConfiguration("Swapchain reconfiguration "
                                                                     "and recovery both failed");
                                        }
                                        render.initialized.clear();
                                        render.stats.width = render.stats.height = 0;
                                        throw std::invalid_argument("Presentation mode rejected: " +
                                                                    reason);
                                    }
                                    render.initialized.clear();
                                    render.stats.width = render.stats.height = 0;
                                }
                                render.options = std::move(nextOptions);
                                render.cameraSpeed = selectedConfig.camera_speed;
                                render.configRevision = selectedRevision;
                                render.stats.profiling_enabled = selectedConfig.profiling;
                                activeConfig = selectedConfig;
                                period = std::chrono::nanoseconds(activeConfig.target_fps
                                                                      ? 1'000'000'000 /
                                                                            activeConfig.target_fps
                                                                      : 0);
                                deadline = Clock::now();
                                dirty = true;
                                previousCompleted.reset();
                                logger.Write(
                                    LogLevel::Info,
                                    std::format("Configuration {} prepared", selectedRevision));
                            }
                            catch (const FatalConfiguration&)
                            {
                                throw;
                            }
                            catch (const std::exception& failure)
                            {
                                logger.Write(LogLevel::Warning,
                                             std::format("Configuration {} rejected: {}",
                                                         selectedRevision, failure.what()));
                                std::lock_guard lock(mutex);
                                configurationState.rejected_id = selectedRevision;
                                configurationError = failure.what();
                                if (configuration.request_id == selectedRevision)
                                    configurationState.status = 3;
                            }
                            prepared = selectedRevision;
                        }
                        selected = activeConfig.pipeline;
                        if (selectedResets != appliedResets)
                        {
                            render.scene->Document()
                                .Get(render.scene->Document().activeCamera)
                                .transform = render.initialCamera;
                            appliedResets = selectedResets;
                            dirty = true;
                        }
                        const auto now = Clock::now();
                        if (painted != snapshot.paints)
                        {
                            painted = snapshot.paints;
                            dirty = true;
                        }
                        pendingYaw += snapshot.yaw;
                        pendingPitch += snapshot.pitch;
                        if (!snapshot.focused || !snapshot.captured)
                            pendingYaw = pendingPitch = 0;
                        if (pendingWidth != snapshot.width || pendingHeight != snapshot.height)
                        {
                            pendingWidth = snapshot.width;
                            pendingHeight = snapshot.height;
                            dimensionsChanged = now;
                            dirty = true;
                        }
                        const bool stepping = selectedSteps != completedSteps && snapshot.visible &&
                                              snapshot.width && snapshot.height;
                        const bool paused =
                            (snapshot.paused && !stepping) || !snapshot.width || !snapshot.height;
                        if (stepping)
                            snapshot.paused = false;
                        if (wasPaused && !paused)
                            dirty = true;
                        wasPaused = paused;
                        const bool resizePending =
                            render.stats.frames &&
                            (render.stats.width != snapshot.width ||
                             render.stats.height != snapshot.height) &&
                            now - dimensionsChanged < std::chrono::milliseconds(60);
                        const bool moving =
                            snapshot.focused &&
                            (std::ranges::any_of(snapshot.keys, [](bool down) { return down; }) ||
                             pendingYaw != 0 || pendingPitch != 0);
                        const bool accumulating =
                            selected == 3 && (activeConfig.flags & 3) == 3 &&
                            render.stats.reflection_samples < renderer::MaxReflectionSamples;
                        const bool needsFrame =
                            dirty || moving || accumulating || activeConfig.continuous || stepping;
                        const auto dt = std::clamp(
                            std::chrono::duration<float>(now - previous).count(), 0.f, .1f);
                        const bool draw =
                            !paused && !resizePending && needsFrame && now >= deadline;
                        if (draw)
                        {
                            snapshot.yaw = pendingYaw;
                            snapshot.pitch = pendingPitch;
                            pendingYaw = pendingPitch = 0;
                            const auto before = render.stats.frames;
                            render.Tick(dt, snapshot);
                            previous = now;
                            if (render.stats.frames != before)
                            {
                                const auto completed = Clock::now();
                                render.stats.frame_interval_ns =
                                    previousCompleted
                                        ? static_cast<uint64_t>(
                                              std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                  completed - *previousCompleted)
                                                  .count())
                                        : 0;
                                render.stats.frame_time_ns = static_cast<uint64_t>(
                                    std::chrono::duration_cast<std::chrono::nanoseconds>(completed -
                                                                                         origin)
                                        .count());
                                previousCompleted = completed;
                                if (stepping)
                                    ++completedSteps;
                                dirty = false;
                            }
                            // No catch-up burst after a slow frame. VSync may impose a lower rate.
                            deadline += period;
                            if (deadline < Clock::now())
                                deadline = Clock::now() + period;
                        }
                        else if (paused || resizePending || !needsFrame)
                        {
                            previousCompleted.reset();
                            previous = now;
                            // Unlimited active frames still sleep while paused, idle or resizing.
                            deadline =
                                now +
                                (activeConfig.target_fps ? period : std::chrono::milliseconds(16));
                            if (paused)
                                ++render.stats.paused_ticks;
                        }
                        render.stats.dpi = snapshot.dpi;
                        // Retire profiles even after a one-shot static redraw. Completion is
                        // queried without adding a GPU wait or keeping the viewport rendering.
                        const auto completedPoint = render.runtime->Completed();
                        for (size_t slot = 0; slot < render.profiles.size(); ++slot)
                        {
                            if (render.profiles[slot].ticks &&
                                render.completions[slot] <= completedPoint)
                            {
                                double ns = 0;
                                for (const auto& timing :
                                     render.profiles[slot].Read(*render.runtime))
                                    ns += timing.nanoseconds;
                                if (render.profileFrames[slot] > render.stats.gpu_frame)
                                {
                                    render.stats.gpu_ns = static_cast<uint64_t>(std::max(0.0, ns));
                                    render.stats.gpu_frame = render.profileFrames[slot];
                                    render.stats.gpu_config_revision = render.profileConfigs[slot];
                                }
                                render.profiles[slot] = {};
                            }
                        }
                        render.stats.focused = snapshot.focused;
                        render.stats.captured = snapshot.captured;
                        render.stats.pressed_keys = static_cast<uint32_t>(
                            std::count(snapshot.keys.begin(), snapshot.keys.end(), true));
                        render.stats.ready = 1;
                        render.stats.ui_thread_id = owner;
                        render.stats.session_generation = identity;
                        render.stats.render_thread_id = GetCurrentThreadId();
                        render.stats.activity = paused ? 3 : resizePending ? 4 : needsFrame ? 1 : 2;
                        {
                            std::lock_guard lock(mutex);
                            const bool framePublished = draw && render.stats.frames != stats.frames;
                            if (draw && render.stats.frames != stats.frames)
                            {
                                if (frameCount == completedFrames.size())
                                {
                                    firstFrame = (firstFrame + 1) % completedFrames.size();
                                    --frameCount;
                                    ++droppedSamples;
                                }
                                render.stats.dropped_samples = droppedSamples;
                                completedFrames[(firstFrame + frameCount) %
                                                completedFrames.size()] = render.stats;
                                ++frameCount;
                            }
                            stats = render.stats;
                            if (framePublished)
                            {
                                configurationState.applied = activeConfig;
                                configurationState.applied_id = activeConfig.request_id;
                                if (configurationState.requested_id == activeConfig.request_id)
                                    configurationState.status = 2;
                            }
                            camera = render.scene->Document()
                                         .Get(render.scene->Document().activeCamera)
                                         .transform;
                        }
                    }
                    const auto report = render.Close();
                    std::lock_guard lock(mutex);
                    closed = report;
                }
                catch (const std::exception& failure)
                {
                    std::lock_guard lock(mutex);
                    error = failure.what();
                }
                catch (...)
                {
                    std::lock_guard lock(mutex);
                    error = "Unknown render thread error";
                }
                // Failure must also retire GPU resources on their owner, before the UI may
                // destroy the HWND. Keep shutdown callable after an asynchronous render error.
                if (state)
                {
                    try
                    {
                        const auto report = state->Close();
                        std::lock_guard lock(mutex);
                        closed = report;
                    }
                    catch (const std::exception& failure)
                    {
                        std::lock_guard lock(mutex);
                        if (error.empty())
                            error = failure.what();
                    }
                }
            });
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
    void CheckError()
    {
        if (!error.empty())
            throw std::runtime_error(error);
    }
    void Update(bool paused)
    {
        RECT rect{};
        Require(IsWindow(child.value), "Viewport child HWND has already been destroyed");
        if (!GetClientRect(child.value, &rect))
            throw std::runtime_error("Cannot query viewport pixels");
        std::lock_guard lock(mutex);
        CheckError();
        input.width = static_cast<uint32_t>(std::max<LONG>(0, rect.right));
        input.height = static_cast<uint32_t>(std::max<LONG>(0, rect.bottom));
        input.dpi = GetDpiForWindow(child.value);
        input.focused = GetFocus() == child.value;
        input.captured = GetCapture() == child.value;
        input.paused =
            paused || !IsWindowVisible(child.value) || IsIconic(GetAncestor(child.value, GA_ROOT));
        input.visible =
            IsWindowVisible(child.value) && !IsIconic(GetAncestor(child.value, GA_ROOT));
        input.keys = keys;
        input.paints = paints;
        input.yaw += yaw;
        input.pitch += pitch;
        yaw = pitch = 0;
        SetEvent(wake.value);
    }
    void Configure(uint32_t selected, float value, bool reset)
    {
        std::lock_guard lock(mutex);
        CheckError();
        if (ready && !pipelineErrors[selected - 1].empty())
            throw std::runtime_error(pipelineErrors[selected - 1]);
        pipeline = selected;
        exposure = value;
        resets += reset;
        ++revision;
        if (ready)
        {
            if (configurationState.status == 1)
                configurationState.superseded_id = configuration.request_id;
            if (configurationState.status == 3 &&
                configurationState.applied.size == sizeof(rl_viewport_config))
                configuration = configurationState.applied;
            configuration.pipeline = selected;
            configuration.exposure = value;
            configuration.request_id = revision;
            configurationState.requested = configuration;
            configurationState.requested_id = revision;
            configurationState.status = 1;
            configurationError.clear();
        }
        SetEvent(wake.value);
    }
    rl_viewport_stats GetStats()
    {
        std::lock_guard lock(mutex);
        CheckError();
        return stats;
    }
    uint32_t ReadFrames(rl_viewport_stats* output, uint32_t capacity)
    {
        std::lock_guard lock(mutex);
        CheckError();
        const auto count = std::min<size_t>(capacity, frameCount);
        for (size_t i = 0; i < count; ++i)
            output[i] = completedFrames[(firstFrame + i) % completedFrames.size()];
        firstFrame = (firstFrame + count) % completedFrames.size();
        frameCount -= count;
        return static_cast<uint32_t>(count);
    }
    rl_viewport_close_report Close()
    {
        if (closing)
            return closed;
        closing = true;
        if (worker.joinable())
        {
            {
                std::lock_guard lock(mutex);
                stop = true;
            }
            SetEvent(wake.value);
            // DXGI may synchronously send window messages during swapchain creation/teardown.
            // Service only sent Win32 messages while joining; no Dispatcher or posted callbacks.
            const auto thread = static_cast<HANDLE>(worker.native_handle());
            while (MsgWaitForMultipleObjectsEx(1, &thread, INFINITE, QS_SENDMESSAGE, 0) ==
                   WAIT_OBJECT_0 + 1)
            {
                MSG message;
                PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
            }
            worker.join();
        }
        child.Reset();
        std::lock_guard lock(mutex);
        closed.worker_failed = error.empty() ? 0 : 1;
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
    return 6;
}
rl_status RENDERLAB_EDITOR_CALL rl_viewport_configure_v2(rl_viewport_handle handle,
                                                         const rl_viewport_config* config)
{
    return Guard(
        [&]
        {
            Require(config != nullptr, "Configuration input is null");
            try
            {
                editor::ValidateConfig(*config);
            }
            catch (const std::invalid_argument& failure)
            {
                throw Failure(RL_INVALID_ARGUMENT, failure.what());
            }
            auto& session = Find(handle);
            std::lock_guard lock(session.mutex);
            session.CheckError();
            Require(session.ready && !session.closing, "Viewport configuration is not ready");
            Require(config->request_id > session.revision,
                    "Configuration request ID must increase");
            if (session.configurationState.status == 1)
                session.configurationState.superseded_id = session.configuration.request_id;
            session.configuration = *config;
            session.revision = config->request_id;
            session.pipeline = config->pipeline;
            session.exposure = config->exposure;
            session.configurationState.requested = *config;
            session.configurationState.requested_id = config->request_id;
            session.configurationState.status = 1;
            session.configurationError.clear();
            SetEvent(session.wake.value);
        });
}
rl_status RENDERLAB_EDITOR_CALL rl_viewport_get_config(rl_viewport_handle handle,
                                                       rl_viewport_config_state* state,
                                                       rl_viewport_config* defaults)
{
    return Guard(
        [&]
        {
            Require(state && defaults, "Configuration outputs are null");
            auto& session = Find(handle);
            std::lock_guard lock(session.mutex);
            session.CheckError();
            *state = session.configurationState;
            *defaults = session.defaults;
        });
}
rl_status RENDERLAB_EDITOR_CALL rl_viewport_config_text(rl_viewport_handle handle, uint32_t kind,
                                                        char* output, uint32_t capacity,
                                                        uint32_t* required)
{
    return Guard(
        [&]
        {
            Require(required && kind <= 1, "Invalid configuration text request");
            auto& session = Find(handle);
            std::lock_guard lock(session.mutex);
            const auto& value = kind == 0 ? session.capabilities : session.configurationError;
            *required = static_cast<uint32_t>(value.size() + 1);
            if (!output || capacity < *required)
                throw Failure(RL_BUFFER_TOO_SMALL, "Configuration text buffer is too small");
            std::memcpy(output, value.c_str(), *required);
        });
}
rl_status RENDERLAB_EDITOR_CALL rl_viewport_step(rl_viewport_handle handle)
{
    return Guard(
        [&]
        {
            auto& session = Find(handle);
            std::lock_guard lock(session.mutex);
            session.CheckError();
            Require(session.ready && session.input.paused && !session.closing,
                    "Single step requires a paused ready viewport");
            ++session.steps;
            SetEvent(session.wake.value);
        });
}
rl_status RENDERLAB_EDITOR_CALL rl_viewport_stop(rl_viewport_handle handle,
                                                 rl_viewport_close_report* report)
{
    return Guard(
        [&]
        {
            Require(report != nullptr, "Close report is null");
            *report = Find(handle).Close();
        });
}
rl_status RENDERLAB_EDITOR_CALL rl_viewport_read_logs(rl_viewport_handle handle, char* output,
                                                      uint32_t capacity, uint32_t* required)
{
    return Guard(
        [&]
        {
            Require(required != nullptr, "Log size output is null");
            auto& session = Find(handle);
            if (session.pendingLogs.empty())
            {
                uint64_t dropped = 0;
                auto events = session.logger.DrainMirror(128, dropped);
                nlohmann::json batch{{"events", nlohmann::json::array()}, {"dropped", dropped}};
                for (const auto& event : events)
                    batch["events"].push_back({{"sequence", event.sequence},
                                               {"time", event.unixMilliseconds},
                                               {"level", static_cast<uint32_t>(event.level)},
                                               {"message", event.message},
                                               {"truncated", event.truncated}});
                session.pendingLogs =
                    batch.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            }
            *required = static_cast<uint32_t>(session.pendingLogs.size() + 1);
            if (!output || capacity < *required)
                throw Failure(RL_BUFFER_TOO_SMALL, "Log batch buffer is too small");
            std::memcpy(output, session.pendingLogs.c_str(), *required);
            session.pendingLogs.clear();
        });
}
rl_status RENDERLAB_EDITOR_CALL rl_viewport_camera(rl_viewport_handle handle, float* pose,
                                                   uint32_t write)
{
    return Guard(
        [&]
        {
            Require(pose && write <= 1, "Invalid camera transfer");
            auto& session = Find(handle);
            std::lock_guard lock(session.mutex);
            if (write)
            {
                for (size_t i = 0; i < 7; ++i)
                    Require(std::isfinite(pose[i]), "Camera pose must be finite");
                const auto norm = std::sqrt(pose[3] * pose[3] + pose[4] * pose[4] +
                                            pose[5] * pose[5] + pose[6] * pose[6]);
                Require(std::isfinite(norm) && norm > .00001f, "Camera quaternion is invalid");
                scene::Transform transform;
                transform.position = {pose[0], pose[1], pose[2]};
                transform.rotation = {pose[3] / norm, pose[4] / norm, pose[5] / norm,
                                      pose[6] / norm};
                session.requestedCamera = transform;
                SetEvent(session.wake.value);
            }
            else
            {
                Require(session.ready, "Camera is not ready");
                const auto& p = session.camera.position;
                const auto& q = session.camera.rotation;
                const float values[]{p.x, p.y, p.z, q.x, q.y, q.z, q.w};
                std::copy(std::begin(values), std::end(values), pose);
            }
        });
}
rl_status RENDERLAB_EDITOR_CALL rl_viewport_create(void* parent, uint32_t backend,
                                                   uint32_t validation, uint32_t profiling,
                                                   uint32_t targetFps, uint32_t continuous,
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
                        validation <= 1 && profiling <= 1 && targetFps <= 240 && continuous <= 1 &&
                        project && shaders && log && handle && child,
                    "Invalid viewport creation arguments");
            Require(GetWindowThreadProcessId(static_cast<HWND>(parent), nullptr) ==
                        GetCurrentThreadId(),
                    "Native parent must belong to the creating UI thread");
            for (size_t i = 0; i < sessions.size(); ++i)
                if (!sessions[i].session)
                {
                    auto session = std::make_unique<Session>(
                        (uint64_t(sessions[i].generation) << 32) | (i + 1),
                        static_cast<HWND>(parent), backend, validation != 0, profiling != 0,
                        targetFps, continuous != 0, platform::Utf8ToWide(project),
                        platform::Utf8ToWide(shaders), platform::Utf8ToWide(log));
                    *handle = (uint64_t(sessions[i].generation) << 32) | (i + 1);
                    *child = session->child.value;
                    sessions[i].session = std::move(session);
                    return;
                }
            throw std::runtime_error("Viewport prototype supports at most four live sessions");
        });
}
rl_status RENDERLAB_EDITOR_CALL rl_viewport_update(rl_viewport_handle handle, uint32_t paused)
{
    return Guard(
        [&]
        {
            Require(paused <= 1, "Invalid viewport update arguments");
            Find(handle).Update(paused != 0);
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
            session.Configure(pipeline, exposure, reset != 0);
        });
}
rl_status RENDERLAB_EDITOR_CALL rl_viewport_get_stats(rl_viewport_handle handle,
                                                      rl_viewport_stats* stats)
{
    return Guard(
        [&]
        {
            Require(stats != nullptr, "Stats output is null");
            *stats = Find(handle).GetStats();
        });
}
rl_status RENDERLAB_EDITOR_CALL rl_viewport_read_frames(rl_viewport_handle handle,
                                                        rl_viewport_stats* frames,
                                                        uint32_t capacity, uint32_t* count)
{
    if (count)
        *count = 0;
    return Guard(
        [&]
        {
            Require(frames && count && capacity && capacity <= 512, "Invalid frame drain buffer");
            *count = Find(handle).ReadFrames(frames, capacity);
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
