#include "core/log.hpp"
#include "rendergraph/graph.hpp"
#include "rendergraph/pool.hpp"
#include "rhi/commands.hpp"
#include "rhi/device.hpp"
#include "rhi/runtime.hpp"
#include "shader/dxc_compiler.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <span>
#include <stdexcept>
#include <vector>

namespace
{
using namespace renderlab;
using namespace rhi;
using namespace rendergraph;
constexpr uint32_t TableSize = 64;
using Color = std::array<float, 4>;
using Table = std::array<std::shared_ptr<TextureView>, TableSize>;
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void Reject(F&& action)
{
    bool rejected = false;
    try
    {
        action();
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Require(rejected, "Invalid descriptor array was accepted");
}
constexpr const char* Source = R"(
StructuredBuffer<uint> Indices : register(t0);
RWStructuredBuffer<float4> Output : register(u1);
Texture2D<float4> Textures[64] : register(t16, space1);
[numthreads(64, 1, 1)]
void CSMain(uint id : SV_DispatchThreadID)
{
    uint index = Indices[id];
    Output[id] = Textures[NonUniformResourceIndex(index)].Load(int3(0, 0, 0));
}
)";
class Fixture
{
public:
    Fixture(Runtime& runtime, shader::Target target) : runtime(runtime), pool(runtime)
    {
        Require(runtime.SampledTextureArrayLimit() >= TableSize,
                "Device lacks the required bounded nonuniform texture-array support");
        shader::DxcCompiler compiler;
        shader::ProgramRequest request;
        request.entry = {ShaderStage::Compute, "descriptor_array.hlsl", "CSMain"};
        request.target = target;
        request.sourceSnapshot = Source;
        PipelineDesc desc;
        desc.compute =
            runtime.CreateShader(ShaderStage::Compute, compiler.Compile(request), "CSMain");
        desc.layout = runtime.CreateLayout({{0, BindingKind::ReadOnlyBuffer},
                                            {1, BindingKind::StorageBuffer},
                                            {16, BindingKind::SampledTexture, 1, TableSize}});
        pipeline = runtime.CreatePipeline(desc);
        std::array<uint32_t, TableSize> values{};
        for (uint32_t i = 0; i < TableSize; ++i)
            values[i] = (i * 17 + 3) % TableSize;
        input = runtime.CreateBuffer({sizeof(values), Memory::Upload, "Array.Indices"});
        std::memcpy(input->Map().data(), values.data(), sizeof(values));
    }
    std::shared_ptr<TextureView> TextureColor(Color value)
    {
        auto staging = runtime.CreateBuffer({256, Memory::Upload, "Array.Color"});
        auto mapped = staging->Map();
        for (uint32_t c = 0; c < 4; ++c)
            mapped[c] = std::byte(static_cast<uint8_t>(value[c] * 255));
        auto texture = runtime.CreateTexture({1, 1, Format::Rgba8, false, false, "Array.Texture"});
        auto commands = runtime.CreateCommands();
        commands->Barrier(staging, State::HostWrite, State::CopySource);
        commands->Barrier(texture, State::Undefined, State::CopyDest);
        commands->CopyTexture(staging, texture, 256, true);
        commands->Barrier(texture, State::CopyDest, State::Sampled);
        runtime.Wait(runtime.Submit(std::move(commands)));
        return runtime.CreateView(texture);
    }
    struct Result
    {
        uint64_t completion;
        std::shared_ptr<Buffer> pixels;
    };
    Result Dispatch(const Table& table, bool missingAccess = false, bool badElement = false)
    {
        Graph graph;
        std::map<const Texture*, TextureHandle> unique;
        std::array<TextureHandle, TableSize> textures;
        for (uint32_t i = 0; i < TableSize; ++i)
        {
            const auto& texture = table[i]->texture;
            if (!unique.contains(texture.get()))
                unique[texture.get()] = graph.ImportTexture("Texture." + std::to_string(i), texture,
                                                            {State::Sampled, true});
            textures[i] = unique.at(texture.get());
        }
        const auto indices = graph.ImportBuffer("Indices", input, {State::HostWrite, true});
        auto output = graph.CreateBuffer("Output", {TableSize * 16, Memory::Device, "", true});
        auto pass = graph.AddComputePass("Array.Nonuniform");
        pass.ReadBuffer(indices, Use::ShaderRead);
        output = pass.WriteBuffer(output);
        for (const auto& [texture, handle] : unique)
            if (!missingAccess || texture != table.back()->texture.get())
                pass.ReadTexture(handle);
        pass.Execute(
            [=, this](ComputeContext& context)
            {
                context.Bind(pipeline);
                context.BindBuffer(0, indices, 0, TableSize * 4, 4);
                context.BindBuffer(1, output, 0, TableSize * 16, 16);
                for (uint32_t i = 0; i < TableSize; ++i)
                    context.BindTexture(16, textures[i], 1, badElement && i == 63 ? 64 : i);
                context.Dispatch(1, 1);
            });
        auto pixels = runtime.CreateBuffer({TableSize * 16, Memory::Readback, "Array.Readback"});
        auto readback = graph.ImportBuffer("Readback", pixels, {State::CopyDest, false});
        auto copy = graph.AddCopyPass("Readback");
        copy.ReadBuffer(output);
        readback = copy.WriteBuffer(readback);
        copy.Execute([=](CopyContext& context) { context.Copy(output, readback, TableSize * 16); });
        graph.Export(readback, State::HostRead);
        graph.Compile();
        return {graph.Execute(pool).completion, std::move(pixels)};
    }
    void Check(const Result& result, const std::array<Color, TableSize>& colors)
    {
        runtime.Wait(result.completion);
        const auto bytes = result.pixels->Map();
        for (uint32_t i = 0; i < TableSize; ++i)
        {
            Color actual;
            std::memcpy(actual.data(), bytes.data() + i * 16, 16);
            const auto& expected = colors[(i * 17 + 3) % TableSize];
            for (uint32_t c = 0; c < 4; ++c)
                Require(std::abs(actual[c] - expected[c]) < .0001f,
                        "Nonuniform texture-table index selected the wrong resource");
        }
    }
    void Validation(const Table& table)
    {
        Reject([&] { runtime.CreateLayout({{16, BindingKind::SampledTexture, 1, 0}}); });
        Reject([&] { runtime.CreateLayout({{16, BindingKind::SampledTexture, 1, 65}}); });
        Reject([&] { runtime.CreateLayout({{16, BindingKind::StorageTexture, 1, 2}}); });
        Reject(
            [&]
            {
                runtime.CreateLayout({{16, BindingKind::SampledTexture, 1, 4},
                                      {19, BindingKind::ReadOnlyBuffer, 1}});
            });
        auto scratch = runtime.CreateBuffer({16, Memory::Device, "Array.Validation", true});
        std::vector<Binding> bindings;
        Binding buffer{};
        buffer.buffer = input;
        buffer.size = TableSize * 4;
        buffer.stride = 4;
        bindings.push_back(buffer);
        buffer.slot = 1;
        buffer.buffer = scratch;
        buffer.size = 16;
        buffer.stride = 16;
        bindings.push_back(buffer);
        for (uint32_t i = 0; i < TableSize; ++i)
        {
            Binding binding{};
            binding.slot = 16;
            binding.space = 1;
            binding.arrayElement = i;
            binding.view = table[i];
            bindings.push_back(binding);
        }
        auto invalid = bindings;
        invalid.pop_back();
        Reject([&] { runtime.CreateGroup(pipeline->desc.layout, invalid); });
        invalid = bindings;
        invalid.back().arrayElement = 0;
        Reject([&] { runtime.CreateGroup(pipeline->desc.layout, invalid); });
        invalid.back().arrayElement = 64;
        Reject([&] { runtime.CreateGroup(pipeline->desc.layout, invalid); });
        invalid = bindings;
        invalid.back().view.reset();
        Reject([&] { runtime.CreateGroup(pipeline->desc.layout, invalid); });
        const auto before = runtime.GetDescriptorStats().inUse;
        {
            const auto group = runtime.CreateGroup(pipeline->desc.layout, bindings);
            Require(runtime.GetDescriptorStats().inUse == before + TableSize + 2,
                    "Descriptor statistics count bindings instead of array elements");
        }
        Require(runtime.GetDescriptorStats().inUse == before, "Array descriptor range leaked");
        Reject([&] { Dispatch(table, true); });
        Reject([&] { Dispatch(table, false, true); });
    }
    Runtime& runtime;
    Pool pool;
    std::shared_ptr<Pipeline> pipeline;
    std::shared_ptr<Buffer> input;
};
void Gpu(Runtime& runtime, shader::Target target)
{
    Fixture fixture(runtime, target);
    constexpr std::array<Color, 4> palette{
        {{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}, {1, 1, 0, 1}}};
    std::array<std::shared_ptr<TextureView>, 4> views;
    for (uint32_t i = 0; i < views.size(); ++i)
        views[i] = fixture.TextureColor(palette[i]);
    Table table;
    std::array<Color, TableSize> colors;
    for (uint32_t i = 0; i < TableSize; ++i)
    {
        table[i] = views[(i * 3) % 4];
        colors[i] = palette[(i * 3) % 4];
    }
    fixture.Check(fixture.Dispatch(table), colors);
    fixture.Validation(table);
    // Fresh versions share one layout but must never update descriptors of recorded work.
    auto oldView = fixture.TextureColor(palette[0]);
    auto newView = fixture.TextureColor(palette[1]);
    Table oldTable, newTable;
    oldTable.fill(oldView);
    newTable.fill(newView);
    const std::weak_ptr<Texture> oldTexture = oldView->texture;
    const auto oldResult = fixture.Dispatch(oldTable);
    oldTable.fill({});
    oldView.reset();
    Require(!oldTexture.expired(), "Recorded array lost texture residency");
    const auto newResult = fixture.Dispatch(newTable);
    colors.fill(palette[0]);
    fixture.Check(oldResult, colors);
    colors.fill(palette[1]);
    fixture.Check(newResult, colors);
    Require(oldTexture.expired(), "Retired array still owns old textures");
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "Expected backend and output directory");
        const std::filesystem::path output(argv[2]);
        std::filesystem::create_directories(output);
        Logger logger(output / "descriptor_array.log");
        DeviceOptions options;
        options.backend = std::string(argv[1]) == "dx12" ? Backend::Dx12 : Backend::Vulkan;
        options.validation = ValidationMode::On;
        auto device = CreateDevice(options, logger);
        {
            auto runtime = device->CreateRuntime();
            Gpu(*runtime,
                options.backend == Backend::Dx12 ? shader::Target::Dxil : shader::Target::Spirv);
            runtime->WaitIdle();
            Require(runtime->GetMemoryStats().used == 0, "Array resources leaked");
            Require(runtime->GetDescriptorStats().inUse == 0, "Array descriptors leaked");
        }
        device->PollDiagnostics();
        device->Shutdown();
        Require(device->GetValidationErrorCount() == 0, "Array validation errors");
        std::cout << "Descriptor array contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
