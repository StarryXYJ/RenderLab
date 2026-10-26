#include "apps/sandbox/options.hpp"
#include "apps/sandbox/presentation.hpp"
#include "core/assert.hpp"
#include "core/log.hpp"
#include "platform/text.hpp"
#include "platform/window.hpp"
#include "rhi/device.hpp"
#include "rhi/runtime.hpp"

#include <Windows.h>
#include <objbase.h>

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
std::string_view ApplicationUsage()
{
#ifdef RENDERLAB_PLAYER
    return "RenderLab Player\n"
           "  --project FILE              Project to run (required)\n"
           "  --backend dx12|vulkan       Backend (default: dx12)\n"
           "  --validation auto|on|off    Device validation\n"
           "  --width N --height N        Output size (default: 1280x720)\n"
           "  --quality scene|low|medium|high  Shadow quality override\n"
           "  --view final|hdr|shadow|environment|brdf  Texture preview\n"
           "  --fixed-camera --debug-panel  Camera and native controls\n"
           "  --headless --benchmark N    Capture or measure a fixed scene\n"
           "  --benchmark-seconds S       Measure for a duration instead of a frame count\n"
           "  --cpu-profile               Record CPU allocations/bindings/submit/retirement\n"
           "  --warmup-frames N --warmup-seconds S  Minimum warmup budgets\n"
           "  --samples PATH              Per-frame JSONL (automatic for benchmarks)\n"
           "  --profile PATH --output DIR  Timing JSON and captured image\n"
           "  --frames N                  Close after N presented frames\n"
           "  --help                      Show help\n";
#else
    return sandbox::Usage();
#endif
}

int Run(const sandbox::Options& options, const std::filesystem::path& shaderRoot, Logger& logger)
{
    // A window thread owns its STA for the full host lifetime. Lazy WIC texture decoding must
    // not create and tear down the last COM apartment after the swapchain has been created.
    const auto initialized =
        CoInitializeEx(nullptr, options.headless ? COINIT_MULTITHREADED : COINIT_APARTMENTTHREADED);
    if (FAILED(initialized))
        throw std::runtime_error("Cannot initialize the Sandbox COM apartment");
    struct ApartmentScope
    {
        ~ApartmentScope()
        {
            CoUninitialize();
        }
    } apartment;
    std::unique_ptr<platform::Window> window;
    if (!options.headless)
    {
        window = std::make_unique<platform::Window>(
#ifdef RENDERLAB_PLAYER
            "RenderLab Player - " + options.backend,
#else
            "RenderLab Sandbox - " + options.backend,
#endif
            options.width, options.height);
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
    if (window || options.rhiTest || options.graphTest || !options.projectPath.empty())
    {
        auto runtime = device->CreateRuntime();
        sandbox::RunRendering(window.get(), *device, *runtime, options, shaderRoot, logger);
        runtime->WaitIdle();
        if (runtime->GetMemoryStats().used != 0)
            throw std::runtime_error("GPU resources leaked after sample shutdown");
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
#ifdef RENDERLAB_PLAYER
        if (!options.help && (options.projectPath.empty() || options.rhiTest || options.graphTest ||
                              options.sceneTest || options.graphNoCull || options.graphNoUpdate))
            throw std::invalid_argument(
                "Player requires --project and does not run Sandbox acceptance demos");
#endif
        if (options.help)
        {
            std::cout << ApplicationUsage();
            return 0;
        }
        renderlab::Logger logger(
            std::filesystem::path(renderlab::platform::Utf8ToWide(options.logPath)));
        try
        {
            const auto shaderRoot = std::filesystem::absolute(argv[0]).parent_path() / "shaders";
            return Run(options, shaderRoot, logger);
        }
        catch (const std::exception& error)
        {
            logger.Write(renderlab::LogLevel::Error, error.what());
            return 1;
        }
    }
    catch (const std::invalid_argument& error)
    {
        std::cerr << "Invalid arguments: " << error.what() << '\n' << ApplicationUsage();
        return 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "RenderLab startup failed: " << error.what() << '\n';
        return 1;
    }
}
