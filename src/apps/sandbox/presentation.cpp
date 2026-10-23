#include "apps/sandbox/presentation.hpp"

#include "apps/sandbox/options.hpp"
#include "core/log.hpp"
#include "platform/text.hpp"
#include "platform/window.hpp"
#include "rhi/commands.hpp"
#include "rhi/device.hpp"
#include "rhi/runtime.hpp"
#include "samples/rhi_acceptance.hpp"
#include "samples/rhi_demo.hpp"

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
    samples::RhiDemo demo(runtime, shaderRoot / options.backend, logger);
    if (options.rhiTest)
        samples::VerifyRhi(runtime, demo, logger,
                           std::filesystem::path(platform::Utf8ToWide(options.outputPath)) /
                               options.backend);
    if (!window)
        return;
    if (options.windowTest)
        CheckWindow(*window, logger);
    auto swapchain = runtime.CreateSwapchain(window->GetNativeSurface(), window->GetState().width,
                                             window->GetState().height);
    demo.Prepare(swapchain->GetFormat());
    struct FrameContext
    {
        uint64_t completion = 0;
        std::shared_ptr<rhi::Texture> depth;
        std::shared_ptr<rhi::TextureView> depthView;
        bool initialized = false;
    };
    std::array<FrameContext, 2> frames;
    uint32_t width = window->GetState().width, height = window->GetState().height;
    std::vector<bool> imageInitialized;
    std::vector<std::shared_ptr<rhi::TextureView>> colorViews;
    const auto start = std::chrono::steady_clock::now();
    uint32_t frameNumber = 0, tick = 0, resizeCount = 0, suspendedTicks = 0;
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
            for (auto& frame : frames)
            {
                frame.depthView.reset();
                frame.depth.reset();
                frame.initialized = false;
            }
            colorViews.clear();
            swapchain->Resize(newWidth, newHeight);
            width = newWidth;
            height = newHeight;
            imageInitialized.clear();
            ++resizeCount;
            demo.Prepare(swapchain->GetFormat());
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
            colorViews.clear();
            swapchain->Resize(width, height);
            imageInitialized.clear();
            demo.Prepare(swapchain->GetFormat());
            continue;
        }
        if (imageInitialized.size() <= acquired.index)
            imageInitialized.resize(acquired.index + 1);
        if (colorViews.size() <= acquired.index)
            colorViews.resize(acquired.index + 1);
        if (!colorViews[acquired.index])
            colorViews[acquired.index] = runtime.CreateView(acquired.texture);
        const auto& desc = acquired.texture->desc;
        if (!frame.depth)
            frame.depth = runtime.CreateTexture(
                {desc.width, desc.height, rhi::Format::D32, false, true, "Frame.Depth"});
        if (!frame.depthView)
            frame.depthView = runtime.CreateView(frame.depth);
        auto commands = runtime.CreateCommands();
        commands->Barrier(acquired.texture,
                          imageInitialized[acquired.index] ? rhi::State::Present
                                                           : rhi::State::Undefined,
                          rhi::State::ColorAttachment);
        commands->Barrier(frame.depth,
                          frame.initialized ? rhi::State::DepthWrite : rhi::State::Undefined,
                          rhi::State::DepthWrite);
        demo.Render(*commands, colorViews[acquired.index], frame.depthView);
        commands->Barrier(acquired.texture, rhi::State::ColorAttachment, rhi::State::Present);
        frame.completion = runtime.Submit(std::move(commands), swapchain.get());
        frame.initialized = true;
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
