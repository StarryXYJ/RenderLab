#include "apps/sandbox/presentation.hpp"

#include "apps/sandbox/options.hpp"
#include "core/log.hpp"
#include "platform/text.hpp"
#include "platform/window.hpp"
#include "renderer/scene_renderer.hpp"
#include "rhi/commands.hpp"
#include "rhi/device.hpp"
#include "rhi/runtime.hpp"
#include "samples/graph_demo.hpp"
#include "samples/rhi_acceptance.hpp"
#include "samples/rhi_demo.hpp"
#include "samples/scene_acceptance.hpp"

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
void CheckWindow(platform::Window& window, Logger& logger)
{
    const auto previousResizeEvents = window.GetState().resizeEvents;
    window.Resize(640, 360);
    window.PumpEvents();
    const auto& resized = window.GetState();
    if (resized.width != 640 || resized.height != 360 ||
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
    logger.Write(LogLevel::Info, "Window smoke: resize=640x360, minimize and restore passed");
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
    if (!window && !options.projectPath.empty())
        samples::VerifyScene(
            runtime, options.backend == "dx12" ? shader::Target::Dxil : shader::Target::Spirv,
            std::filesystem::path(platform::Utf8ToWide(options.projectPath)), shaderRoot,
            std::filesystem::path(platform::Utf8ToWide(options.outputPath)) / options.backend,
            logger, options.sceneTest);
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
    struct FrameContext
    {
        uint64_t completion = 0;
    };
    std::array<FrameContext, 2> frames;
    uint32_t width = window->GetState().width, height = window->GetState().height;
    std::vector<bool> imageInitialized;
    const auto start = std::chrono::steady_clock::now();
    uint32_t frameNumber = 0, tick = 0, resizeCount = 0, suspendedTicks = 0;
    auto previousTime = start;
    while (window->PumpEvents())
    {
        device.PollDiagnostics();
        ++tick;
        if (options.windowTest)
        {
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
            const auto now = std::chrono::steady_clock::now();
            const auto dt = std::min(.1f, std::chrono::duration<float>(now - previousTime).count());
            previousTime = now;
            const auto axis = [&](platform::Key positive, platform::Key negative)
            { return float(window->KeyDown(positive)) - float(window->KeyDown(negative)); };
            scene->Document().MoveCamera(
                {3 * dt * axis(platform::Key::Right, platform::Key::Left),
                 3 * dt * axis(platform::Key::Up, platform::Key::Down),
                 3 * dt * axis(platform::Key::Backward, platform::Key::Forward)},
                {60 * dt * axis(platform::Key::LookUp, platform::Key::LookDown),
                 60 * dt * axis(platform::Key::TurnLeft, platform::Key::TurnRight), 0});
            frame.completion =
                scene->Render(acquired.texture, initial, swapchain.get(), false, dump).completion;
        }
        else
            frame.completion =
                demo->Render(acquired.texture, initial, swapchain.get(), false,
                             {!options.graphNoUpdate, true, !options.graphNoCull}, dump)
                    .completion;
        imageInitialized[acquired.index] = true;
        swapchain->Present();
        ++frameNumber;
    }
    runtime.WaitIdle();
    if (options.windowTest && (!frameNumber || resizeCount < 8 || suspendedTicks < 2))
        throw std::runtime_error("In-flight presentation resize/minimize coverage incomplete");
    logger.Write(LogLevel::Info,
                 std::format("Presentation completed: frames={}, rebuilds={}, suspendedTicks={}, "
                             "frameContexts=2 (independent of swapchain images)",
                             frameNumber, resizeCount, suspendedTicks));
}
} // namespace renderlab::sandbox
