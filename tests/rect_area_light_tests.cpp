#include "core/log.hpp"
#include "rhi/commands.hpp"
#include "rhi/device.hpp"
#include "rhi/runtime.hpp"
#include "shader/dxc_compiler.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace renderlab;
using namespace rhi;
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
void Check(Runtime& runtime, shader::Target target, const std::filesystem::path& source)
{
    shader::DxcCompiler compiler;
    shader::ProgramRequest request;
    request.entry = {ShaderStage::Compute, source, "CSMain"};
    request.target = target;
    PipelineDesc desc;
    desc.layout = runtime.CreateLayout({{0, BindingKind::StorageBuffer}});
    desc.compute = runtime.CreateShader(ShaderStage::Compute, compiler.Compile(request), "CSMain");
    const auto pipeline = runtime.CreatePipeline(desc);
    constexpr uint32_t bytes = 5 * 4 * sizeof(float);
    const auto output = runtime.CreateBuffer({bytes, Memory::Device, "RectLight.Result", true});
    const auto readback = runtime.CreateBuffer({bytes, Memory::Readback, "RectLight.Readback"});
    Binding binding{0};
    binding.buffer = output;
    binding.size = bytes;
    binding.stride = 16;
    auto commands = runtime.CreateCommands();
    commands->Barrier(output, State::Undefined, State::Storage);
    commands->Bind(pipeline);
    commands->Bind(runtime.CreateGroup(desc.layout, {binding}));
    commands->Compute(1, 1);
    commands->Barrier(output, State::Storage, State::CopySource);
    commands->Copy(output, readback, bytes);
    commands->Barrier(readback, State::CopyDest, State::HostRead);
    runtime.Wait(runtime.Submit(std::move(commands)));
    std::array<float, 20> values{};
    std::memcpy(values.data(), readback->Map().data(), bytes);
    for (const auto value : values)
        Require(std::isfinite(value), "Rectangular light produced a nonfinite result");
    Require(std::abs(values[0] - 1) < 1e-6 && std::abs(values[1] - .0625) < 1e-6 &&
                values[2] == 0 && values[3] == 0,
            "Rectangular emitter PDF, backface or coincident-point contract failed");
    Require(std::abs(values[4] - 4.f / 13) < 1e-6 && std::abs(values[5] - 9.f / 13) < 1e-6 &&
                values[6] == 0 && values[7] == 1,
            "Power heuristic weights failed");
    Require(std::abs(values[12] - .5f) < 1e-6 && std::abs(values[13] - .5f) < 1e-6 &&
                std::abs(values[14] - float(.25 * 1.5 * std::sqrt(1.5))) < 1e-6 &&
                std::abs(values[15] - .5f) < 1e-6,
            "Off-axis conversion or large-PDF MIS weight failed");
    const double shearDistance2 = 1 + .75 * .75 + .5 * .5;
    Require(std::abs(values[16] - float(.25 * shearDistance2 * std::sqrt(shearDistance2))) < 1e-6 &&
                values[17] == 0 && values[18] == 0 && values[19] == 0,
            "Sheared area or zero/invalid PDF contract failed");
    // Independent midpoint integration over the projected area in double precision.
    constexpr int steps = 1024;
    constexpr double pi = 3.14159265358979323846;
    double reference = 0;
    for (int y = 0; y < steps; ++y)
        for (int x = 0; x < steps; ++x)
        {
            const double px = 2.0 * (x + .5) / steps - 1;
            const double py = 2.0 * (y + .5) / steps - 1;
            const double d2 = 1 + px * px + py * py;
            reference += 4 / (pi * d2 * d2 * steps * steps);
        }
    Require(std::abs(values[8] - reference) < 5e-4,
            "Area-to-solid-angle estimator disagreed with independent area integration");
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 4, "Expected backend, test shader path and output directory");
        Require(std::string(argv[1]) == "dx12" || std::string(argv[1]) == "vulkan",
                "Unknown backend");
        std::filesystem::create_directories(argv[3]);
        Logger logger(std::filesystem::path(argv[3]) / "rect-area.log");
        DeviceOptions options;
        options.backend = std::string(argv[1]) == "dx12" ? Backend::Dx12 : Backend::Vulkan;
        options.validation = ValidationMode::On;
        auto device = CreateDevice(options, logger);
        {
            auto runtime = device->CreateRuntime();
            Check(*runtime,
                  options.backend == Backend::Dx12 ? shader::Target::Dxil : shader::Target::Spirv,
                  argv[2]);
            runtime->WaitIdle();
            Require(runtime->GetMemoryStats().used == 0, "Rectangular light resources leaked");
            Require(runtime->GetDescriptorStats().inUse == 0,
                    "Rectangular light descriptors leaked");
        }
        device->PollDiagnostics();
        device->Shutdown();
        Require(device->GetValidationErrorCount() == 0, "Rectangular light validation errors");
        std::cout << "Rectangular emitter sampling contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
