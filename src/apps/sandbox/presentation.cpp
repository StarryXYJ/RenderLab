#include "apps/sandbox/presentation.hpp"

#include "apps/sandbox/capture.hpp"
#include "apps/sandbox/debug_panel.hpp"
#include "apps/sandbox/options.hpp"
#include "apps/sandbox/statistics.hpp"
#include "core/log.hpp"
#include "platform/text.hpp"
#include "platform/window.hpp"
#include "renderer/scene_renderer.hpp"
#include "rhi/commands.hpp"
#include "rhi/device.hpp"
#include "rhi/runtime.hpp"
#include "samples/graph/graph_demo.hpp"
#include "samples/rhi/acceleration_structure_acceptance.hpp"
#include "samples/rhi/rhi_acceptance.hpp"
#include "samples/rhi/rhi_demo.hpp"
#include "samples/scene/scene_acceptance.hpp"

#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <stdexcept>
#include <vector>

namespace renderlab::sandbox
{
namespace
{
void CheckWindowApartment()
{
    APTTYPE apartment;
    APTTYPEQUALIFIER qualifier;
    if (FAILED(CoGetApartmentType(&apartment, &qualifier)) ||
        (apartment != APTTYPE_STA && apartment != APTTYPE_MAINSTA))
        throw std::runtime_error("Window thread lost its host-owned COM STA");
}
void CheckWindow(platform::Window& window, Logger& logger)
{
    CheckWindowApartment();
    const auto previousResizeEvents = window.GetState().resizeEvents;
    const auto testWidth = window.GetState().width == 640 ? 641u : 640u;
    window.Resize(testWidth, 360);
    window.PumpEvents();
    const auto& resized = window.GetState();
    if (resized.width != testWidth || resized.height != 360 ||
        resized.resizeEvents <= previousResizeEvents)
    {
        throw std::runtime_error("Window resize smoke test failed");
    }
    window.Minimize();
    window.PumpEvents();
    if (!window.GetState().minimized || window.GetState().minimizeEvents == 0)
    {
        throw std::runtime_error("Window minimize smoke test failed");
    }
    window.Restore();
    window.PumpEvents();
    if (window.GetState().minimized || window.GetState().width == 0 ||
        window.GetState().height == 0)
    {
        throw std::runtime_error("Window restore smoke test failed");
    }
    logger.Write(
        LogLevel::Info,
        std::format("Window smoke: resize={}x360, minimize and restore passed", testWidth));
}
} // namespace

void RunRendering(platform::Window* window, rhi::Device& device, rhi::Runtime& runtime,
                  const sandbox::Options& options, const std::filesystem::path& shaderRoot,
                  Logger& logger)
{
    if (options.rhiTest)
    {
        samples::RhiDemo direct(runtime, shaderRoot / options.backend, logger);
        samples::VerifyRhi(runtime, direct, logger,
                           std::filesystem::path(platform::Utf8ToWide(options.outputPath)) /
                               options.backend);
    }
    if (options.graphTest)
        samples::VerifyGraph(runtime, shaderRoot / options.backend,
                             std::filesystem::path(platform::Utf8ToWide(options.outputPath)) /
                                 options.backend,
                             logger);
    if (options.accelerationStructureTest)
        samples::VerifyAccelerationStructures(
            runtime, options.backend == "dx12" ? shader::Target::Dxil : shader::Target::Spirv,
            shaderRoot / "m5" / "ray_query_acceptance.hlsl",
            std::filesystem::path(platform::Utf8ToWide(options.outputPath)) / options.backend,
            logger);
    if (!window && !options.projectPath.empty() && !options.sceneTest)
        CaptureScene(runtime, device, options, shaderRoot, logger);
    if (!window && !options.projectPath.empty() && options.sceneTest)
        samples::VerifyScene(
            runtime, options.backend == "dx12" ? shader::Target::Dxil : shader::Target::Spirv,
            std::filesystem::path(platform::Utf8ToWide(options.projectPath)), shaderRoot,
            std::filesystem::path(platform::Utf8ToWide(options.outputPath)) / options.backend,
            logger, RenderOptions(options).pipeline, options.sceneTest);
    if (!window)
        return;
    if (options.windowTest)
        CheckWindow(*window, logger);
    std::unique_ptr<samples::GraphDemo> demo;
    std::unique_ptr<renderer::SceneRenderer> scene;
    if (options.projectPath.empty())
        demo = std::make_unique<samples::GraphDemo>(runtime, shaderRoot / options.backend);
    else
        scene = std::make_unique<renderer::SceneRenderer>(
            runtime, options.backend == "dx12" ? shader::Target::Dxil : shader::Target::Spirv,
            std::filesystem::path(platform::Utf8ToWide(options.projectPath)), shaderRoot);
    auto swapchain = runtime.CreateSwapchain(window->GetNativeSurface(), window->GetState().width,
                                             window->GetState().height);
    if (demo)
        demo->Prepare(swapchain->GetFormat());
    auto settings = RenderOptions(options);
    if (scene)
        scene->ValidatePipeline(settings.pipeline);
    Statistics statistics;
    std::unique_ptr<DebugPanel> panel;
    scene::Transform originalCamera;
    std::string selectedLight;
    if (scene)
    {
        originalCamera = scene->Document().Get(scene->Document().activeCamera).transform;
        for (const auto& [id, transform] : scene->Document().WorldTransforms())
        {
            const auto& object = scene->Document().Get(id);
            if (transform.enabled && object.light && object.light->enabled)
            {
                selectedLight = id;
                break;
            }
        }
    }
    const auto createPanel = [&]
    {
        settings.profile = true;
        panel = std::make_unique<DebugPanel>(
            window->GetNativeSurface(),
            DebugControls{settings, scene->Document().exposure,
                          !selectedLight.empty() &&
                              scene->Document().Get(selectedLight).light->shadows},
            runtime.SupportsAccelerationStructures() && runtime.SupportsRayQuery() &&
                runtime.SupportsRayTracingPipeline());
        if (options.windowTest)
            panel->VerifyControls();
    };
    if (options.debugPanel)
        createPanel();
    struct FrameContext
    {
        uint64_t completion = 0;
        renderer::SceneReadback rendered;
        double cpuMs = 0;
    };
    std::array<FrameContext, 2> frames;
    uint32_t width = window->GetState().width, height = window->GetState().height;
    std::vector<bool> imageInitialized;
    const auto start = std::chrono::steady_clock::now();
    uint32_t frameNumber = 0, tick = 0, resizeCount = 0, suspendedTicks = 0;
    auto previousTime = start;
    auto lastPanelUpdate = start;
    bool debugKeyDown = false;
    uint32_t pipelineMask = 0;
    while (window->PumpEvents())
    {
        device.PollDiagnostics();
        ++tick;
        if (options.windowTest)
        {
            if (scene && options.pipeline == "raytracing" &&
                (tick == 7 || tick == 23 || tick == 47))
            {
                const auto selected = tick == 7    ? renderer::PipelineType::Forward
                                      : tick == 23 ? renderer::PipelineType::Deferred
                                                   : renderer::PipelineType::RayTracing;
                scene->ValidatePipeline(selected);
                if (panel)
                    panel->SelectPipeline(selected);
                else
                    settings.pipeline = selected;
            }
            if (tick == 5 || tick == 30 || tick == 50)
                window->Resize(641, 359);
            if (tick == 10 || tick == 35 || tick == 55)
                window->Resize(800, 450);
            if (tick == 15 || tick == 40)
                window->Minimize();
            if (tick == 20 || tick == 45)
                window->Restore();
            window->PumpEvents();
        }
        const auto& state = window->GetState();
        if (state.closeRequested)
            break;
        const auto debugKey = window->KeyDown(platform::Key::ToggleDebug);
        if (scene && debugKey && !debugKeyDown)
        {
            if (panel)
                panel->Toggle();
            else
                createPanel();
        }
        debugKeyDown = debugKey;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - start)
                                 .count();
        if ((options.exitAfterMs && elapsed >= *options.exitAfterMs) ||
            (options.frames && frameNumber >= *options.frames) ||
            (options.windowTest && tick >= 65))
        {
            window->RequestClose();
            continue;
        }
        const uint32_t newWidth = state.minimized ? 0 : state.width;
        const uint32_t newHeight = state.minimized ? 0 : state.height;
        if (newWidth != width || newHeight != height)
        {
            runtime.WaitIdle();
            swapchain->Resize(newWidth, newHeight);
            width = newWidth;
            height = newHeight;
            imageInitialized.clear();
            ++resizeCount;
            if (demo)
                demo->Prepare(swapchain->GetFormat());
        }
        if (!width || !height)
        {
            ++suspendedTicks;
            window->WaitForEvents(5);
            continue;
        }
        auto& frame = frames[frameNumber % frames.size()];
        runtime.Wait(frame.completion);
        if (frame.rendered.completion)
        {
            statistics.Add(frame.rendered, runtime, frame.cpuMs);
            frame.rendered = {};
        }
        if (panel && panel->Controls().render.pipeline != settings.pipeline)
        {
            try
            {
                scene->ValidatePipeline(panel->Controls().render.pipeline);
            }
            catch (const std::exception& error)
            {
                panel->SelectPipeline(settings.pipeline);
                logger.Write(LogLevel::Warning, error.what());
                const auto message = platform::Utf8ToWide(error.what());
                MessageBoxW(static_cast<HWND>(window->GetNativeSurface().window), message.c_str(),
                            L"Pipeline unavailable for this scene", MB_OK | MB_ICONINFORMATION);
            }
        }
        auto acquired = swapchain->Acquire();
        if (!acquired.texture)
        {
            runtime.WaitIdle();
            swapchain->Resize(width, height);
            imageInitialized.clear();
            if (demo)
                demo->Prepare(swapchain->GetFormat());
            continue;
        }
        if (imageInitialized.size() <= acquired.index)
            imageInitialized.resize(acquired.index + 1);
        const auto dump = frameNumber == 0
                              ? std::filesystem::path(platform::Utf8ToWide(options.graphDumpPath))
                              : std::filesystem::path{};
        const auto initial =
            imageInitialized[acquired.index] ? rhi::State::Present : rhi::State::Undefined;
        if (scene)
        {
            if (panel)
            {
                auto& controls = panel->Controls();
                settings = controls.render;
                scene->Document().exposure = controls.exposure;
                if (!selectedLight.empty())
                    scene->Document().Get(selectedLight).light->shadows = controls.shadows;
                if (controls.resetCamera)
                {
                    scene->Document().Get(scene->Document().activeCamera).transform =
                        originalCamera;
                    controls.resetCamera = false;
                }
            }
            const auto now = std::chrono::steady_clock::now();
            const auto dt =
                options.fixedCamera || (panel && panel->Controls().freezeCamera)
                    ? 0.f
                    : std::min(.1f, std::chrono::duration<float>(now - previousTime).count());
            previousTime = now;
            const auto axis = [&](platform::Key positive, platform::Key negative)
            { return float(window->KeyDown(positive)) - float(window->KeyDown(negative)); };
            scene->Document().MoveCamera(
                {3 * dt * axis(platform::Key::Right, platform::Key::Left),
                 3 * dt * axis(platform::Key::Up, platform::Key::Down),
                 3 * dt * axis(platform::Key::Backward, platform::Key::Forward)},
                {60 * dt * axis(platform::Key::LookUp, platform::Key::LookDown),
                 60 * dt * axis(platform::Key::TurnLeft, platform::Key::TurnRight), 0});
            const auto renderStart = std::chrono::steady_clock::now();
            frame.rendered =
                scene->Render(acquired.texture, initial, swapchain.get(), false, dump, settings);
            frame.cpuMs = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - renderStart)
                              .count();
            frame.completion = frame.rendered.completion;
            pipelineMask |= 1u << static_cast<uint32_t>(settings.pipeline);
            if (panel && now - lastPanelUpdate > std::chrono::milliseconds(250))
            {
                panel->Update(statistics.Text(scene->LastSnapshot(), runtime.GetMemoryStats()));
                lastPanelUpdate = now;
            }
        }
        else
            frame.completion =
                demo->Render(acquired.texture, initial, swapchain.get(), false,
                             {!options.graphNoUpdate, true, !options.graphNoCull}, dump)
                    .completion;
        imageInitialized[acquired.index] = true;
        if (options.windowTest && scene && frameNumber == 0)
            CheckWindowApartment();
        swapchain->Present();
        ++frameNumber;
    }
    runtime.WaitIdle();
    for (auto& frame : frames)
        if (frame.rendered.completion)
        {
            statistics.Add(frame.rendered, runtime, frame.cpuMs);
            frame.rendered = {};
        }
    if (!options.profilePath.empty())
    {
        const auto driverBudgets = device.QueryMemoryBudget();
        statistics.Save(std::filesystem::path(platform::Utf8ToWide(options.profilePath)), options,
                        device.GetReport(), runtime.GetMemoryStats(), nullptr, nullptr, nullptr,
                        nullptr, nullptr, &driverBudgets);
    }
    if (options.windowTest && (!frameNumber || resizeCount < 8 || suspendedTicks < 2))
        throw std::runtime_error("In-flight presentation resize/minimize coverage incomplete");
    if (options.windowTest && scene && options.pipeline == "raytracing" &&
        (pipelineMask & 14u) != 14u)
        throw std::runtime_error(
            "Window test did not render Forward, Deferred and Ray Tracing selections");
    logger.Write(LogLevel::Info,
                 std::format("Presentation completed: frames={}, rebuilds={}, suspendedTicks={}, "
                             "frameContexts=2 (independent of swapchain images)",
                             frameNumber, resizeCount, suspendedTicks));
}
} // namespace renderlab::sandbox
