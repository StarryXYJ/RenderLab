#include "assets/io.hpp"
#include "core/log.hpp"
#include "renderer/ltc_table.hpp"
#include "rhi/commands.hpp"
#include "rhi/device.hpp"
#include "rhi/runtime.hpp"
#include "rhi/upload.hpp"
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
    desc.layout = runtime.CreateLayout({{0, BindingKind::StorageBuffer},
                                        {1, BindingKind::SampledTexture},
                                        {2, BindingKind::SampledTexture}});
    desc.compute = runtime.CreateShader(ShaderStage::Compute, compiler.Compile(request), "CSMain");
    const auto pipeline = runtime.CreatePipeline(desc);
    constexpr uint32_t bytes = 8 * 4 * sizeof(float);
    const auto output = runtime.CreateBuffer({bytes, Memory::Device, "LTC.Result", true});
    const auto readback = runtime.CreateBuffer({bytes, Memory::Readback, "LTC.Readback"});
    Binding binding{0};
    binding.buffer = output;
    binding.size = bytes;
    binding.stride = 16;
    auto commands = runtime.CreateCommands();
    const auto upload = [&](const char* name)
    {
        const auto data = renderer::DecodeLtcTable(
            assets::detail::Read(source.parent_path().parent_path() / "RenderLab" / name));
        const auto pitch = TextureRowPitch(data.width, Format::Rgba32Float);
        auto texture = runtime.CreateTexture(
            {data.width, data.height, Format::Rgba32Float, false, false, "LTC.Table"});
        auto buffer =
            runtime.CreateBuffer({uint64_t(pitch) * data.height, Memory::Upload, "LTC.Upload"});
        for (uint32_t y = 0; y < data.height; ++y)
            std::memcpy(buffer->Map().data() + y * pitch, data.pixels.data() + y * data.width * 4,
                        data.width * 16);
        commands->Barrier(buffer, State::HostWrite, State::CopySource);
        commands->Barrier(texture, State::Undefined, State::CopyDest);
        commands->CopyTexture(buffer, texture, pitch, true);
        commands->Barrier(texture, State::CopyDest, State::Sampled);
        return runtime.CreateView(texture);
    };
    const auto matrix = upload("LtcMatrix.dds"), amplitude = upload("LtcAmplitude.dds");
    commands->Barrier(output, State::Undefined, State::Storage);
    commands->Bind(pipeline);
    commands->Bind(runtime.CreateGroup(desc.layout, {binding, {1, matrix}, {2, amplitude}}));
    commands->Compute(4, 1);
    commands->Barrier(output, State::Storage, State::CopySource);
    commands->Copy(output, readback, bytes);
    commands->Barrier(readback, State::CopyDest, State::HostRead);
    runtime.Wait(runtime.Submit(std::move(commands)));
    std::array<float, 32> values{};
    std::memcpy(values.data(), readback->Map().data(), bytes);
    constexpr double pi = 3.14159265358979323846;
    constexpr int steps = 512;
    const double color[] = {.7, .4, .2};
    for (int scenario = 0; scenario < 4; ++scenario)
    {
        double reference[3]{};
        const double nv = scenario == 2 ? .5 : 1;
        const double vx = std::sqrt(1 - nv * nv);
        const double rough = scenario == 1 ? .5 : .8;
        const double a2 = rough * rough * rough * rough;
        for (int y = 0; y < steps; ++y)
            for (int x = 0; x < steps; ++x)
            {
                const double u = (x + .5) / steps - .5, v = (y + .5) / steps - .5;
                const double px = (scenario == 2 ? .7 : 0) + 2 * u + (scenario == 2 ? .6 * v : 0);
                const double py = 2 * v, pz = scenario == 3 ? .4 + 2 * u : 2;
                if (pz <= 0)
                    continue;
                const double d2 = px * px + py * py + pz * pz, d = std::sqrt(d2);
                const double lx = px / d, nl = pz / d;
                const double hx = vx + lx, hy = py / d, hz = nv + nl;
                const double hlen = std::sqrt(hx * hx + hy * hy + hz * hz);
                const double nh = hz / hlen, vh = (vx * hx + nv * hz) / hlen;
                const double ggx = a2 / (pi * std::pow(nh * nh * (a2 - 1) + 1, 2));
                const double vis = .5 / (nl * std::sqrt(nv * nv * (1 - a2) + a2) +
                                         nv * std::sqrt(nl * nl * (1 - a2) + a2));
                const double weight = (scenario == 3 ? 1.6 / d : 4 * nl) / (d2 * steps * steps);
                for (int c = 0; c < 3; ++c)
                {
                    const double f0 = scenario == 1 ? color[c] : .04;
                    const double f = f0 + (1 - f0) * std::pow(1 - vh, 5);
                    const double diffuse = scenario == 1 ? 0 : (1 - f) * color[c] / pi;
                    reference[c] += (diffuse + f * ggx * vis) * nl * weight;
                }
            }
        for (int c = 0; c < 3; ++c)
        {
            const auto actual = values[scenario * 4 + c];
            std::cout << "scenario " << scenario << " channel " << c << ": " << actual << " vs "
                      << reference[c] << '\n';
            Require(std::isfinite(actual) &&
                        std::abs(actual - reference[c]) < .04 * reference[c] + .001,
                    "LTC disagreed with independent BRDF integration");
            Require(values[(scenario + 4) * 4 + c] == 0, "LTC emitted through back face");
        }
    }
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
        std::cout << "LTC evaluation contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
