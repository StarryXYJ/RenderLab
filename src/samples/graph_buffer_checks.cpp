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
    auto source = graph.ImportBuffer("Upload", upload, {rhi::State::HostWrite, true, 0});
    auto constants = graph.ImportBuffer("Constants", constant, {rhi::State::HostWrite, true, 0});
    graph.CreateBuffer("Input", {256, rhi::Memory::Device});
    auto copy = graph.AddCopyPass("Buffer.Upload");
    copy.ReadBuffer(source);
    auto input = copy.WriteBuffer("Input");
    copy.Execute([source, input](CopyContext& context) { context.Copy(source, input, 256); });

    for (uint32_t iteration = 0; iteration < 2; ++iteration)
    {
        auto result = graph.CreateBuffer("Transform." + std::to_string(iteration),
                                         {256, rhi::Memory::Device, {}, true});
        auto pass = graph.AddComputePass("Buffer.Transform");
        pass.ReadBuffer(constants, Use::Uniform);
        pass.ReadBuffer(input);
        result = pass.WriteBuffer(result);
        pass.Execute(
            [constants, input, result, pipeline](ComputeContext& context)
            {
                context.Bind(pipeline);
                context.BindBuffer(0, constants, 0, 256);
                context.BindBuffer(1, input, 0, 256, 16);
                context.BindBuffer(2, result, 0, 256, 16);
                context.Dispatch(1, 1);
            });
        input = result;
    }
    auto destination = graph.ImportBuffer("Readback", readback, {rhi::State::CopyDest, false, 0});
    auto read = graph.AddCopyPass("Buffer.Readback");
    read.ReadBuffer(input);
    destination = read.WriteBuffer(destination);
    read.Execute([input, destination](CopyContext& context)
                 { context.Copy(input, destination, 256); });
    graph.Export(destination, rhi::State::HostRead);
    graph.Compile();
    auto submission = graph.Execute(pool);
    const auto plan = graph.GetPlan().ToJson();
    WriteFileAtomically(output / "buffer-plan.json",
                        std::as_bytes(std::span{plan.data(), plan.size()}));
    runtime.Wait(submission.completion);
    std::array<uint32_t, 64> actual{};
    std::memcpy(actual.data(), readback->Map().data(), 256);
    for (uint32_t i = 0; i < actual.size(); ++i)
        Require(actual[i] == (i * 3 + (i % 4 + 1)) * 3 + (i % 4 + 1),
                "Graph cbuffer/structured buffer dependency mismatch");
}
} // namespace renderlab::samples
