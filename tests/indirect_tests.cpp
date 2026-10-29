#include "core/log.hpp"
#include "rendergraph/graph.hpp"
#include "rendergraph/pool.hpp"
#include "rhi/commands.hpp"
#include "rhi/device.hpp"
#include "rhi/runtime.hpp"
#include "shader/dxc_compiler.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

namespace
{
using namespace renderlab;
using namespace rhi;
using namespace rendergraph;
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
    Require(rejected, "Invalid indirect command was accepted");
}
const char* Source = R"(
#include "DrawParameters.hlsli"
StructuredBuffer<float4> Objects : register(t0);
struct P { float4 position : SV_Position; float4 color : COLOR; };
P VSMain(float2 position : POSITION, DrawParameters draw)
{
    uint instance = GetInstanceIndex(draw);
    float4 object = Objects[instance];
    P p;
    p.position = float4(position + object.xy, 0.5, 1);
    p.color = float4(object.zw, instance / 8.0, 1);
    return p;
}
float4 PSMain(P p) : SV_Target { return p.color; }
StructuredBuffer<uint> Input : register(t1);
RWStructuredBuffer<uint> Arguments : register(u2);
RWStructuredBuffer<uint> Count : register(u3);
[numthreads(32, 1, 1)]
void CSMain(uint id : SV_DispatchThreadID)
{
    if (id < 16) Arguments[id] = Input[id];
    if (id == 0) Count[1] = Input[16];
}
)";
class Fixture
{
public:
    Fixture(Runtime& runtime, shader::Target target, const std::filesystem::path& shaders)
        : runtime(runtime), pool(runtime)
    {
        Require(runtime.GetIndirectSupport().maxDrawCount >= 3 &&
                    runtime.GetIndirectSupport().countBuffer &&
                    runtime.GetIndirectSupport().firstInstance &&
                    runtime.GetIndirectSupport().shaderInstanceIndex,
                "Device lacks required multi-draw/count/firstInstance support");
        shader::DxcCompiler compiler;
        auto compile = [&](ShaderStage stage, const char* entry)
        {
            shader::ProgramRequest request;
            request.entry = {stage, "indirect.hlsl", entry};
            request.target = target;
            request.sourceSnapshot = Source;
            request.includeDirectories = {shaders};
            if (target == shader::Target::Dxil && stage == ShaderStage::Vertex)
                request.targetProfile = "vs_6_8";
            return runtime.CreateShader(stage, compiler.Compile(request), entry);
        };
        PipelineDesc desc;
        desc.layout = runtime.CreateLayout({{0, BindingKind::ReadOnlyBuffer}});
        desc.vertex = compile(ShaderStage::Vertex, "VSMain");
        desc.pixel = compile(ShaderStage::Pixel, "PSMain");
        desc.colorFormats = {Format::Rgba8};
        desc.vertexStride = 8;
        desc.attributes = {{"POSITION", 0, VertexFormat::Float2, 0}};
        desc.raster.cull = CullMode::None;
        pipeline = runtime.CreatePipeline(desc);
        desc = {};
        desc.layout = runtime.CreateLayout({{1, BindingKind::ReadOnlyBuffer},
                                            {2, BindingKind::StorageBuffer},
                                            {3, BindingKind::StorageBuffer}});
        desc.compute = compile(ShaderStage::Compute, "CSMain");
        compute = runtime.CreatePipeline(desc);
        // Padding makes firstVertex, firstIndex and baseVertex observable.
        const std::array<float, 14> points{9,     9, 9,    9, -.18f, -.20f, .18f,
                                           -.20f, 0, .20f, 9, 9,     9,     9};
        vertices = Upload(std::span(points), State::Vertex);
        const std::array<uint32_t, 6> elements{3, 4, 5, 1, 2, 3};
        indices = Upload(std::span(elements), State::Index);
        const std::array<std::array<float, 4>, 8> objects{{{9, 9, 0, 0},
                                                           {.5f, .45f, 1, 0},
                                                           {-.5f, .45f, 0, 1},
                                                           {0, 0, 1, 1},
                                                           {-.5f, -.45f, .25f, .75f},
                                                           {.5f, -.45f, .75f, .25f},
                                                           {9, 9, 0, 0},
                                                           {9, 9, 0, 0}}};
        instances = Upload(std::span(objects), State::ShaderRead);
    }
    template <class T, size_t N>
    std::shared_ptr<Buffer> Upload(std::span<T, N> values, State final = State::Indirect)
    {
        const auto bytes = std::as_bytes(values);
        auto staging = runtime.CreateBuffer({bytes.size(), Memory::Upload, "Indirect.Staging"});
        std::memcpy(staging->Map().data(), bytes.data(), bytes.size());
        auto buffer = runtime.CreateBuffer({bytes.size(), Memory::Device, "Indirect.Input"});
        auto commands = runtime.CreateCommands();
        commands->Barrier(staging, State::HostWrite, State::CopySource);
        commands->Barrier(buffer, State::Undefined, State::CopyDest);
        commands->Copy(staging, buffer, bytes.size());
        commands->Barrier(buffer, State::CopyDest, final);
        runtime.Wait(runtime.Submit(std::move(commands)));
        return buffer;
    }
    std::vector<std::byte> Render(bool indexed, bool indirect, uint32_t count,
                                  bool generated = false, bool negative = false,
                                  bool useCount = true, bool negativeBase = false)
    {
        std::array<uint32_t, 17> data{};
        constexpr std::array<uint32_t, 3> first{4, 1, 2};
        const auto stride = indexed ? sizeof(DrawIndexedArguments) : sizeof(DrawArguments);
        for (uint32_t i = 0; i < 3; ++i)
        {
            if (indexed)
            {
                const DrawIndexedArguments args{3, i == 0 ? 2u : 1u, negativeBase ? 0u : 3u,
                                                negativeBase ? -1 : 1, first[i]};
                std::memcpy(reinterpret_cast<std::byte*>(data.data()) + 4 + i * stride, &args,
                            stride);
            }
            else
            {
                const DrawArguments args{3, i == 0 ? 2u : 1u, 2, first[i]};
                std::memcpy(reinterpret_cast<std::byte*>(data.data()) + 4 + i * stride, &args,
                            stride);
            }
        }
        data[16] = count;
        auto input = Upload(std::span(data));
        const std::array<uint32_t, 2> counts{123, count};
        auto countBuffer = Upload(std::span(counts));
        Graph graph;
        auto arguments = graph.ImportBuffer("Args", input, {State::Indirect, true});
        auto counter = graph.ImportBuffer("Count", countBuffer, {State::Indirect, true});
        if (generated)
        {
            const auto source = arguments;
            arguments = graph.CreateBuffer("GPU.Args", {64, Memory::Device, "", true});
            counter = graph.CreateBuffer("GPU.Count", {8, Memory::Device, "", true});
            auto pass = graph.AddComputePass("Generate");
            pass.ReadBuffer(source, Use::ShaderRead);
            arguments = pass.WriteBuffer(arguments);
            counter = pass.WriteBuffer(counter);
            pass.Execute(
                [=, this](ComputeContext& context)
                {
                    context.Bind(compute);
                    context.BindBuffer(1, source, 0, 68, 4);
                    context.BindBuffer(2, arguments, 0, 64, 4);
                    context.BindBuffer(3, counter, 0, 8, 4);
                    context.Dispatch(1, 1);
                });
        }
        const auto vertex = graph.ImportBuffer("Vertices", vertices, {State::Vertex, true});
        const auto index = graph.ImportBuffer("Indices", indices, {State::Index, true});
        const auto object = graph.ImportBuffer("Objects", instances, {State::ShaderRead, true});
        auto color = graph.CreateTexture("Color", {64, 64, Format::Rgba8, false, true});
        auto pass = graph.AddRasterPass("Indirect");
        color = pass.ColorTarget(color, LoadOp::Clear, {0, 0, 0, 1});
        pass.ReadBuffer(vertex, Use::Vertex);
        if (indexed)
            pass.ReadBuffer(index, Use::Index);
        pass.ReadBuffer(object, Use::ShaderRead);
        if (indirect)
        {
            pass.ReadBuffer(arguments, Use::Indirect, {4, 3 * stride});
            if (!negative && useCount)
                pass.ReadBuffer(counter, Use::Indirect, {4, 4});
        }
        pass.Execute(
            [=, this](RasterContext& context)
            {
                context.Bind(pipeline);
                context.BindBuffer(0, object, 0, instances->desc.size, 16);
                context.Geometry(vertex, indexed ? index : BufferHandle{});
                if (indirect)
                    context.DrawIndirect(arguments, 4, 3, indexed,
                                         useCount ? counter : BufferHandle{}, useCount ? 4 : 0);
                else
                    for (uint32_t i = 0; i < std::min(count, 3u); ++i)
                        if (indexed)
                            context.DrawIndexed(3, negativeBase ? 0 : 3, i == 0 ? 2 : 1, first[i],
                                                negativeBase ? -1 : 1);
                        else
                            context.Draw(3, 2, i == 0 ? 2 : 1, first[i]);
            });
        auto readback = runtime.CreateBuffer({256 * 64, Memory::Readback, "Indirect.Pixels"});
        auto pixels = graph.ImportBuffer("Pixels", readback, {State::CopyDest, false});
        auto copy = graph.AddCopyPass("Readback");
        copy.ReadTexture(color);
        pixels = copy.WriteBuffer(pixels);
        copy.Execute([=](CopyContext& context) { context.CopyTexture(pixels, color, 256, false); });
        graph.Export(pixels, State::HostRead);
        graph.Compile();
        runtime.Wait(graph.Execute(pool).completion);
        const auto mapped = readback->Map();
        return {mapped.begin(), mapped.end()};
    }
    void Validation()
    {
        auto color = runtime.CreateTexture({4, 4, Format::Rgba8, false, true});
        Binding binding{};
        binding.slot = 0;
        binding.buffer = instances;
        binding.size = instances->desc.size;
        binding.stride = 16;
        auto group = runtime.CreateGroup(pipeline->desc.layout, {binding});
        auto commands = runtime.CreateCommands();
        commands->Begin(runtime.CreateView(color));
        commands->Bind(pipeline);
        commands->Bind(group);
        commands->Geometry(vertices, indices);
        auto arguments = runtime.CreateBuffer({64, Memory::Upload, "Args"});
        auto counter = runtime.CreateBuffer({8, Memory::Upload, "Count"});
        Reject([&] { commands->DrawFrom(arguments, 1); });
        Reject([&] { commands->DrawFrom(arguments, 0, 0); });
        Reject([&] { commands->DrawFrom(arguments, 8, 3, true); });
        Reject([&] { commands->DrawFrom(arguments, 0, 1, true, counter, 2); });
        Reject([&] { commands->DrawFrom(arguments, 0, 1, true, counter, 8); });
        Reject([&] { commands->DrawFrom(arguments, 0, 1, true, {}, 4); });
        const std::weak_ptr<Buffer> weakArguments = arguments, weakCount = counter;
        commands->DrawFrom(arguments, 4, 3, true, counter, 4);
        arguments.reset();
        counter.reset();
        Require(!weakArguments.expired() && !weakCount.expired(), "Recording lost indirect inputs");
        commands->Cancel();
        Require(weakArguments.expired() && weakCount.expired(), "Cancel retained indirect inputs");
        const std::array<DrawArguments, 1> emptyDraw{{{3, 0, 2, 0}}};
        const std::array<uint32_t, 1> one{1};
        arguments = Upload(std::span(emptyDraw));
        counter = Upload(std::span(one));
        const std::weak_ptr<Buffer> submittedArguments = arguments, submittedCount = counter;
        commands = runtime.CreateCommands();
        commands->Barrier(color, State::Undefined, State::ColorAttachment);
        commands->Begin(runtime.CreateView(color));
        commands->Bind(pipeline);
        commands->Bind(group);
        commands->Geometry(vertices);
        commands->DrawFrom(arguments, 0, 1, false, counter);
        commands->End();
        const auto completion = runtime.Submit(std::move(commands));
        arguments.reset();
        counter.reset();
        Require(!submittedArguments.expired() && !submittedCount.expired(),
                "Submission lost indirect inputs");
        runtime.Wait(completion);
        Require(submittedArguments.expired() && submittedCount.expired(),
                "Completed submission retained indirect inputs");
        CommandList limited(&runtime);
        limited.Begin(runtime.CreateView(color));
        limited.Bind(pipeline);
        limited.Bind(group);
        limited.Geometry(vertices, indices);
        Reject([&] { limited.DrawFrom(instances, 0, 2); });
        Reject([&] { limited.DrawFrom(instances, 0, 1, false, instances); });
        limited.Cancel();
        Reject([&] { Render(true, true, 2, false, true); });
    }
    Runtime& runtime;
    Pool pool;
    std::shared_ptr<Pipeline> pipeline, compute;
    std::shared_ptr<Buffer> vertices, indices, instances;
};
void Gpu(Runtime& runtime, shader::Target target, const std::filesystem::path& shaders)
{
    Fixture fixture(runtime, target, shaders);
    for (bool indexed : {false, true})
        for (uint32_t count : {0u, 1u, 2u, 3u, 99u})
        {
            const auto reference = fixture.Render(indexed, false, count);
            Require(reference == fixture.Render(indexed, true, count) &&
                        reference == fixture.Render(indexed, true, count, true),
                    "Direct/CPU indirect/Compute indirect pixels differ");
            std::array<bool, 256> ids{};
            for (size_t i = 0; i < reference.size(); i += 4)
                ids[std::to_integer<uint8_t>(reference[i + 2])] = true;
            std::cout << "indexed=" << indexed << " count=" << count << " blue:";
            for (size_t i = 0; i < ids.size(); ++i)
                if (ids[i])
                    std::cout << ' ' << i;
            std::cout << '\n';
            Require((ids[127] || ids[128]) == (count > 0) && ids[159] == (count > 0) &&
                        ids[32] == (count > 1) && ids[64] == (count > 2),
                    "GetInstanceIndex must include firstInstance for every draw");
        }
    Require(fixture.Render(false, false, 3) == fixture.Render(true, false, 3),
            "Indexed baseVertex/firstIndex mismatch");
    Require(fixture.Render(true, false, 3) ==
                fixture.Render(true, true, 3, true, false, true, true),
            "Negative indexed baseVertex mismatch");
    for (bool indexed : {false, true})
        Require(fixture.Render(indexed, false, 3) ==
                    fixture.Render(indexed, true, 3, false, false, false),
                "Fixed multi-draw differs from direct drawing");
    fixture.Validation();
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 4, "Expected backend, shader directory and output directory");
        const std::filesystem::path output(argv[3]);
        std::filesystem::create_directories(output);
        Logger logger(output / "indirect.log");
        DeviceOptions options;
        options.backend = std::string(argv[1]) == "dx12" ? Backend::Dx12 : Backend::Vulkan;
        options.validation = ValidationMode::On;
        auto device = CreateDevice(options, logger);
        {
            auto runtime = device->CreateRuntime();
            Gpu(*runtime,
                options.backend == Backend::Dx12 ? shader::Target::Dxil : shader::Target::Spirv,
                argv[2]);
            runtime->WaitIdle();
            Require(runtime->GetMemoryStats().used == 0, "Indirect resources leaked");
        }
        device->PollDiagnostics();
        device->Shutdown();
        Require(device->GetValidationErrorCount() == 0, "Indirect validation errors");
        std::cout << "Indirect contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
