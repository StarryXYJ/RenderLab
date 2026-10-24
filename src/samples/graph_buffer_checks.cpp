#include "samples/graph_demo.hpp"

#include "core/file_io.hpp"
#include "platform/text.hpp"
#include "samples/detail/check.hpp"
#include "samples/shader_io.hpp"

#include <array>
#include <cstring>

namespace renderlab::samples
{
void VerifyGraphBuffers(rhi::Runtime& runtime, const std::filesystem::path& shaders,
                        const std::filesystem::path& output)
{
    using namespace rendergraph;
    using detail::Require;
    auto constant = runtime.CreateBuffer({256, rhi::Memory::Upload, "Graph.Constants"});
    const uint32_t scale = 3;
    std::memcpy(constant->Map().data(), &scale, 4);
    std::array<uint32_t, 64> inputData{};
    for (uint32_t i = 0; i < inputData.size(); ++i)
        inputData[i] = i;
    auto upload = runtime.CreateBuffer({256, rhi::Memory::Upload, "Graph.Input.Upload"});
    std::memcpy(upload->Map().data(), inputData.data(), 256);
    auto readback = runtime.CreateBuffer({256, rhi::Memory::Readback, "Graph.Buffer.Readback"});
    auto layout = runtime.CreateLayout({{0, rhi::BindingKind::ConstantBuffer},
                                        {1, rhi::BindingKind::ReadOnlyBuffer},
                                        {2, rhi::BindingKind::StorageBuffer}});
    rhi::PipelineDesc desc;
    desc.layout = layout;
    desc.compute = runtime.CreateShader(
        rhi::ShaderStage::Compute,
        LoadShader(platform::WideToUtf8((shaders / "graph_buffers.bin").wstring())), "CSMain");
    auto pipeline = runtime.CreatePipeline(std::move(desc));
    Pool pool(runtime);
    Graph graph;
    auto source = graph.Import(upload, {rhi::State::HostWrite, true, 0});
    auto constants = graph.Import(constant, {rhi::State::HostWrite, true, 0});
    auto input = graph.Create(rhi::BufferDesc{256, rhi::Memory::Device, "Graph.Input"});
    struct CopyData
    {
        BufferHandle source, destination;
    };
    graph.AddPass<CopyData>(
        "Buffer.Upload", PassKind::Copy,
        [&](PassBuilder& b, CopyData& d)
        {
            b.Read(source, Use::CopySource);
            input = b.Write(input, Use::CopyDestination);
            d.source = source;
            d.destination = input;
        },
        [](const CopyData& d, ExecuteContext& c) { c.Copy(d.source, d.destination, 256); });
    struct ComputeData
    {
        BufferHandle constants, input, output;
        std::shared_ptr<rhi::Pipeline> pipeline;
        std::shared_ptr<rhi::BindingGroup> bindings;
    };
    for (uint32_t iteration = 0; iteration < 2; ++iteration)
    {
        auto result =
            graph.Create(rhi::BufferDesc{256, rhi::Memory::Device, "Graph.Transform", true});
        graph.AddPass<ComputeData>(
            "Buffer.Transform", PassKind::Compute,
            [&](PassBuilder& b, ComputeData& d)
            {
                b.Read(constants, Use::Uniform);
                b.Read(input, Use::ShaderRead);
                result = b.Write(result, Use::Storage);
                d.constants = constants;
                d.input = input;
                d.output = result;
                d.pipeline = pipeline;
            },
            [](const ComputeData& d, ExecuteContext& c)
            {
                c.Bind(d.pipeline);
                c.Bind(d.bindings);
                c.Dispatch(1, 1);
            },
            [](ComputeData& d, PrepareContext& c)
            {
                d.bindings = c.Device().CreateGroup(d.pipeline->desc.layout,
                                                    {{0, {}, {}, c.Buffer(d.constants), 0, 256},
                                                     {1, {}, {}, c.Buffer(d.input), 0, 256, 16},
                                                     {2, {}, {}, c.Buffer(d.output), 0, 256, 16}});
            });
        input = result;
    }
    auto destination = graph.Import(readback, {rhi::State::CopyDest, false, 0});
    graph.AddPass<CopyData>(
        "Buffer.Readback", PassKind::Copy,
        [&](PassBuilder& b, CopyData& d)
        {
            b.Read(input, Use::CopySource);
            destination = b.Write(destination, Use::CopyDestination);
            d.source = input;
            d.destination = destination;
        },
        [](const CopyData& d, ExecuteContext& c) { c.Copy(d.source, d.destination, 256); });
    graph.Export(destination, rhi::State::HostRead);
    graph.Compile();
    graph.Prepare(pool);
    graph.Record();
    const auto plan = graph.GetPlan().ToJson();
    WriteFileAtomically(output / "buffer-plan.json",
                        std::as_bytes(std::span{plan.data(), plan.size()}));
    runtime.Wait(graph.Submit().completion);
    std::array<uint32_t, 64> actual{};
    std::memcpy(actual.data(), readback->Map().data(), 256);
    for (uint32_t i = 0; i < actual.size(); ++i)
        Require(actual[i] == (i * 3 + (i % 4 + 1)) * 3 + (i % 4 + 1),
                "Graph cbuffer/structured buffer dependency mismatch");
}
} // namespace renderlab::samples
