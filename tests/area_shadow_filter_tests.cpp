#include "core/log.hpp"
#include "rhi/commands.hpp"
#include "rhi/device.hpp"
#include "rhi/runtime.hpp"
#include "rhi/upload.hpp"
#include "shader/dxc_compiler.hpp"

#include <algorithm>
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
    request.generatedTypes = "struct SceneAreaShadowData { float4x4 Matrix0, Matrix1; float4 "
                             "Light0, Light1, Parameters; };";
    desc.layout = runtime.CreateLayout({{0, BindingKind::StorageBuffer},
                                        {1, BindingKind::SampledTexture},
                                        {2, BindingKind::SampledTexture},
                                        {3, BindingKind::SampledTexture}});
    desc.compute = runtime.CreateShader(ShaderStage::Compute, compiler.Compile(request), "CSMain");
    const auto pipeline = runtime.CreatePipeline(desc);
    constexpr uint32_t bytes = (5 + 128) * 4 * sizeof(float), size = 64;
    const auto output = runtime.CreateBuffer({bytes, Memory::Device, "AreaShadow.Result", true});
    const auto readback = runtime.CreateBuffer({bytes, Memory::Readback, "AreaShadow.Readback"});
    auto texture =
        runtime.CreateTexture({size, size, Format::Rgba32Float, false, false, "AreaShadow.Map"});
    const auto pitch = TextureRowPitch(size, Format::Rgba32Float);
    auto upload =
        runtime.CreateBuffer({uint64_t(pitch) * size, Memory::Upload, "AreaShadow.Upload"});
    for (uint32_t y = 0; y < size; ++y)
        for (uint32_t x = 0; x < size; ++x)
        {
            const float depth = x < size / 2 ? (y < size / 2 ? .00990099f : .00490049f)
                                : x >= 3 * size / 4
                                    ? .0032343033f + .001f * ((y + .5f) / size - .5f)
                                    : 0;
            const float visibility = x >= size / 2 ? 1.f : 0.f;
            const std::array<float, 4> texel{depth, visibility, visibility, 0};
            std::memcpy(upload->Map().data() + y * pitch + x * 16, texel.data(), 16);
        }
    std::array<std::shared_ptr<Texture>, 2> guides;
    std::array<std::shared_ptr<Buffer>, 2> guideUploads;
    for (uint32_t g = 0; g < 2; ++g)
    {
        guides[g] = runtime.CreateTexture(
            {size, size, Format::Rgba32Float, false, false, "AreaShadow.Guide"});
        guideUploads[g] = runtime.CreateBuffer(
            {uint64_t(pitch) * size, Memory::Upload, "AreaShadow.GuideUpload"});
        for (uint32_t y = 0; y < size; ++y)
            for (uint32_t x = 0; x < size; ++x)
            {
                const bool right = x >= size / 2;
                const bool depthEdge = y >= 6 && y <= 8 && right;
                const bool normalEdge = y >= 10 && y <= 12 && right;
                const std::array<float, 4> value =
                    g == 0 ? std::array<float, 4>{depthEdge ? .8f : .5f, 0, 0, 0}
                           : std::array<float, 4>{normalEdge ? 1.f : 0.f, 0, normalEdge ? 0.f : 1.f,
                                                  .5f};
                std::memcpy(guideUploads[g]->Map().data() + y * pitch + x * 16, value.data(), 16);
            }
    }
    Binding binding{0};
    binding.buffer = output;
    binding.size = bytes;
    binding.stride = 16;
    auto commands = runtime.CreateCommands();
    commands->Barrier(upload, State::HostWrite, State::CopySource);
    commands->Barrier(texture, State::Undefined, State::CopyDest);
    commands->CopyTexture(upload, texture, pitch, true);
    commands->Barrier(texture, State::CopyDest, State::Sampled);
    for (uint32_t g = 0; g < 2; ++g)
    {
        commands->Barrier(guideUploads[g], State::HostWrite, State::CopySource);
        commands->Barrier(guides[g], State::Undefined, State::CopyDest);
        commands->CopyTexture(guideUploads[g], guides[g], pitch, true);
        commands->Barrier(guides[g], State::CopyDest, State::Sampled);
    }
    commands->Barrier(output, State::Undefined, State::Storage);
    commands->Bind(pipeline);
    commands->Bind(runtime.CreateGroup(desc.layout, {binding,
                                                     {1, runtime.CreateView(texture)},
                                                     {2, runtime.CreateView(guides[0])},
                                                     {3, runtime.CreateView(guides[1])}}));
    commands->Compute(1, 1);
    commands->Barrier(output, State::Storage, State::CopySource);
    commands->Copy(output, readback, bytes);
    commands->Barrier(readback, State::CopyDest, State::HostRead);
    runtime.Wait(runtime.Submit(std::move(commands)));
    std::array<float, (5 + 128) * 4> values{};
    std::memcpy(values.data(), readback->Map().data(), bytes);
    std::cout << "Small=" << values[0] << " large=" << values[1] << " blocked=" << values[2]
              << " open=" << values[3] << " near=" << values[4] << " far=" << values[5]
              << " self=" << values[6] << " corrected=" << values[7] << '\n';
    Require(values[0] < values[1] && values[1] > 0 && values[1] < 1 && values[2] == 0 &&
                values[3] == 1 && values[4] > values[5] && values[6] < 1 && values[7] == 1 &&
                std::abs(values[8]) < 1e-6 && std::abs(values[9] - .001f) < 1e-6,
            "Area shadow filter failed size, separation, endpoints or receiver-plane correction");
    float maxStep = 0;
    for (size_t i = 1; i < 64; ++i)
        maxStep = std::max(maxStep, std::abs(values[(3 + i) * 4] - values[(2 + i) * 4]));
    std::cout << "Maximum subtexel shadow step=" << maxStep << '\n';
    Require(maxStep < .12f, "Area shadow edge jumps during subtexel receiver motion");
    float maxBlockerStep = 0;
    for (size_t i = 1; i < 64; ++i)
        maxBlockerStep =
            std::max(maxBlockerStep, std::abs(values[(67 + i) * 4] - values[(66 + i) * 4]));
    std::cout << "Maximum blocker-depth shadow step=" << maxBlockerStep << '\n';
    Require(maxBlockerStep < .04f, "Area shadow jumps across blocker-depth transitions");
    const auto base = 131 * 4;
    Require(std::abs(values[base] - .25f) < 1e-6f && std::abs(values[base + 1] - .75f) < 1e-6f &&
                values[base + 2] == 0 && values[base + 3] == 1 && values[base + 4] == 0 &&
                values[base + 5] == 1 && std::abs(values[base + 6] - .25f) < 1e-6f &&
                std::abs(values[base + 7] - .75f) < 1e-6f,
            "Area visibility filter failed smoothing or crossed a surface boundary");
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
        Logger logger(std::filesystem::path(argv[3]) / "area-shadow-filter.log");
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
            Require(runtime->GetMemoryStats().used == 0, "Area shadow filter resources leaked");
            Require(runtime->GetDescriptorStats().inUse == 0,
                    "Area shadow filter descriptors leaked");
        }
        device->PollDiagnostics();
        device->Shutdown();
        Require(device->GetValidationErrorCount() == 0, "Area shadow filter validation errors");
        std::cout << "Area shadow filter contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
