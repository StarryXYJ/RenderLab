#include "core/log.hpp"
#include "platform/text.hpp"
#include "rhi/device.hpp"
#include "samples/material/material_demo.hpp"

#include <iostream>
#include <stdexcept>

int wmain(int argc, wchar_t** argv)
{
    using namespace renderlab;
    platform::ConfigureConsoleUtf8();
    try
    {
        if (argc != 4)
            throw std::invalid_argument(
                "Usage: renderlab_material_demo dx12|vulkan FILE.shader OUTPUT_DIRECTORY");
        const std::wstring backend = argv[1];
        if (backend != L"dx12" && backend != L"vulkan")
            throw std::invalid_argument("Unknown backend");
        const auto output = std::filesystem::path(argv[3]);
        Logger logger(output / "material.log");
        rhi::DeviceOptions options;
        options.backend = backend == L"dx12" ? rhi::Backend::Dx12 : rhi::Backend::Vulkan;
        options.validation = rhi::ValidationMode::On;
        auto device = rhi::CreateDevice(options, logger);
        {
            auto runtime = device->CreateRuntime();
            samples::VerifyMaterial(
                *runtime, backend == L"dx12" ? shader::Target::Dxil : shader::Target::Spirv, logger,
                argv[2], output);
            runtime->WaitIdle();
            if (runtime->GetMemoryStats().used != 0)
                throw std::runtime_error("Material GPU resources leaked");
        }
        device->PollDiagnostics();
        device->Shutdown();
        const auto errors = device->GetValidationErrorCount();
        logger.Write(LogLevel::Info, "Shutdown; validation errors=" + std::to_string(errors));
        return errors == 0 ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
