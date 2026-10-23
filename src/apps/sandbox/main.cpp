#include "apps/sandbox/options.hpp"
#include "core/assert.hpp"
#include "core/log.hpp"
#include "platform/text.hpp"
#include "platform/window.hpp"
#include "rhi/device.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace renderlab;

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

int Run(const sandbox::Options& options, Logger& logger)
{
    std::unique_ptr<platform::Window> window;
    if (!options.headless)
    {
        window = std::make_unique<platform::Window>("RenderLab M0 - " + options.backend, 1280, 720);
        RENDERLAB_ASSERT(window->GetNativeSurface().window != nullptr);
    }
    rhi::DeviceOptions deviceOptions;
    deviceOptions.backend = options.backend == "dx12" ? rhi::Backend::Dx12 : rhi::Backend::Vulkan;
    deviceOptions.validation = options.validation == "on"    ? rhi::ValidationMode::On
                               : options.validation == "off" ? rhi::ValidationMode::Off
                                                             : rhi::ValidationMode::Auto;
    deviceOptions.adapterIndex = options.adapter;
    auto device = rhi::CreateDevice(deviceOptions, logger);
    const auto& report = device->GetReport();
    for (const auto& adapter : report.adapters)
    {
        logger.Write(LogLevel::Info,
                     std::format("Adapter [{}]: {} (vendor=0x{:04X}, device=0x{:04X}, software={})",
                                 adapter.index, adapter.name, adapter.vendorId, adapter.deviceId,
                                 adapter.software));
    }
    logger.Write(LogLevel::Info,
                 std::format("Initialized {} adapter [{}], API={}, validation={}", report.backend,
                             report.selectedAdapter, report.apiVersion, report.validationEnabled));
    logger.Write(
        LogLevel::Info,
        std::format("Supported: rayPipeline={}, rayQuery={}, descriptorIndexing={}, meshShader={}",
                    report.supported.rayTracingPipeline, report.supported.rayQuery,
                    report.supported.descriptorIndexing, report.supported.meshShader));
    if (window)
    {
        if (options.windowTest)
        {
            CheckWindow(*window, logger);
        }
        const auto start = std::chrono::steady_clock::now();
        const auto exitAfterMs = options.exitAfterMs.value_or(options.windowTest ? 250u : 0u);
        const bool timed = options.exitAfterMs.has_value() || options.windowTest;
        while (window->PumpEvents())
        {
            device->PollDiagnostics();
            if (timed)
            {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - start)
                                         .count();
                if (elapsed >= exitAfterMs)
                {
                    window->RequestClose();
                    continue;
                }
                window->WaitForEvents(
                    static_cast<uint32_t>(std::min<int64_t>(100, exitAfterMs - elapsed)));
            }
            else
            {
                window->WaitForEvents(100);
            }
        }
        if (options.windowTest)
        {
            logger.Write(LogLevel::Info,
                         "Window smoke: close request processed; device is released before HWND");
        }
    }
    device->PollDiagnostics();
    // Include validation errors emitted during native device destruction in the final report.
    device->Shutdown();
    const auto errors = device->GetValidationErrorCount();
    const auto json = rhi::ReportToJson(report, errors);
    if (options.headless)
    {
        std::cout << json;
    }
    if (!options.reportPath.empty())
    {
        const auto path = std::filesystem::path(platform::Utf8ToWide(options.reportPath));
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path());
        }
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << json;
        if (!file)
        {
            throw std::runtime_error("Cannot write capability report");
        }
        logger.Write(LogLevel::Info, "Capability report saved: " + options.reportPath);
    }
    // Deterministic shutdown: device, then native window, with logger still alive.
    device.reset();
    window.reset();
    logger.Write(LogLevel::Info, std::format("Shutdown complete; validation errors={}", errors));
    return errors == 0 ? 0 : 1;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    renderlab::platform::ConfigureConsoleUtf8();
    try
    {
        std::vector<std::string> strings;
        for (int index = 1; index < argc; ++index)
        {
            strings.push_back(renderlab::platform::WideToUtf8(argv[index]));
        }
        // Finish allocating string storage before creating views that vector growth could
        // invalidate.
        std::vector<std::string_view> arguments;
        for (const auto& string : strings)
        {
            arguments.push_back(string);
        }
        const auto options = renderlab::sandbox::ParseOptions(arguments);
        if (options.help)
        {
            std::cout << renderlab::sandbox::Usage();
            return 0;
        }
        renderlab::Logger logger(
            std::filesystem::path(renderlab::platform::Utf8ToWide(options.logPath)));
        try
        {
            return Run(options, logger);
        }
        catch (const std::exception& error)
        {
            logger.Write(renderlab::LogLevel::Error, error.what());
            return 1;
        }
    }
    catch (const std::invalid_argument& error)
    {
        std::cerr << "Invalid arguments: " << error.what() << '\n' << renderlab::sandbox::Usage();
        return 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "RenderLab startup failed: " << error.what() << '\n';
        return 1;
    }
}
