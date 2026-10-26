#include "renderer/forward.hpp"
#include "renderer/pipeline.hpp"

#include "core/file_io.hpp"
#include "platform/text.hpp"
#include "rendergraph/graph.hpp"
#include "rendergraph/pool.hpp"
#include "rhi/runtime.hpp"
#include "rhi/upload.hpp"
#include "samples/common/check.hpp"
#include "samples/graph/graph_features.hpp"
#include "samples/rhi/rhi_demo.hpp"
#include "samples/common/shader_io.hpp"

#include <cstring>
#include <filesystem>

namespace renderlab::samples
{
namespace
{
class UnusedFeature final : public renderer::RenderFeature
{
public:
    UnusedFeature() : RenderFeature("Unused.Feature") {}
    void Build(rendergraph::Graph& graph, const renderer::RenderFrame&) override
    {
        graph.CreateTexture("Unused.StageOutput", {4, 4, rhi::Format::Rgba8, true, false});
        auto pass = graph.AddComputePass(Name());
        pass.WriteTexture("Unused.StageOutput");
        pass.Execute([](rendergraph::ComputeContext&) {});
    }
};
class InspectOpaqueFeature final : public renderer::RenderFeature
{
public:
    InspectOpaqueFeature() : RenderFeature("Inspect.Opaque", {"OpaqueColor"}) {}
    void Build(rendergraph::Graph& graph, const renderer::RenderFrame&) override
    {
        auto pass = graph.AddCopyPass(Name());
        pass.ReadTexture("OpaqueColor");
        pass.SideEffect();
        pass.Execute([](rendergraph::CopyContext&) {});
    }
};
} // namespace
void VerifyForward(rhi::Runtime& runtime, const std::filesystem::path& shaders,
                   const std::filesystem::path& output, Logger& logger,
                   const std::shared_ptr<rhi::Buffer>& baseline)
{
    using namespace rhi;
    using namespace rendergraph;
    using namespace renderer;
    constexpr uint32_t width = 640, height = 360;
    RhiDemo source(runtime, shaders, logger);
    auto geometry = detail::CreateDemoGeometry(runtime);
    const auto shader = [&](ShaderStage stage, const char* file, const char* entry)
    {
        return runtime.CreateShader(
            stage, LoadShader(platform::WideToUtf8((shaders / file).wstring())), entry);
    };
    PipelineDesc desc;
    desc.layout =
        runtime.CreateLayout({{1, BindingKind::SampledTexture}, {2, BindingKind::Sampler}});
    desc.vertex = shader(ShaderStage::Vertex, "vertex.bin", "VSMain");
    desc.pixel = shader(ShaderStage::Pixel, "pixel.bin", "PSMain");
    desc.color = Format::Rgba8Srgb;
    desc.depthStencilFormat = rhi::Format::D32;
    desc.depth = {true, true};
    desc.vertexStride = sizeof(detail::DemoVertex);
    desc.attributes = {{"POSITION", 0, VertexFormat::Float3, 0},
                       {"COLOR", 1, VertexFormat::Float4, 12},
                       {"TEXCOORD", 2, VertexFormat::Float2, 28}};
    const auto opaque = runtime.CreatePipeline(desc);
    desc.blend.enabled = true;
    desc.depth.writeEnabled = false;
    const auto transparent = runtime.CreatePipeline(desc);
    detail::Require(opaque != transparent, "Depth/blend variants must have separate pipelines");
    Graph graph;
    Pool pool(runtime);
    RenderFrame frame;
    graph.CreateTexture("CameraColor", {width, height, Format::Rgba8Srgb, false, true});
    graph.CreateTexture("CameraDepth", {width, height, Format::D32, false, true});
    graph.ImportTexture("Noise", source.GetNoiseTexture(), {State::Sampled, true});
    DrawPacket draw;
    draw.pipeline = opaque;
    graph.ImportBuffer("Vertices", geometry.vertices, {State::Vertex, true});
    graph.ImportBuffer("Indices", geometry.indices, {State::Index, true});
    graph.ImportBuffer("Indirect", geometry.indirect, {State::Indirect, true});
    draw.vertices = "Vertices";
    draw.indices = "Indices";
    draw.indirect = "Indirect";
    draw.textures = {{1, "Noise"}};
    draw.samplers = {{2, runtime.CreateSampler()}};
    draw.kind = DrawKind::Indexed;
    draw.count = 3;
    frame.opaque.push_back(draw);
    draw.kind = DrawKind::Vertices;
    draw.count = 6;
    draw.first = 3;
    frame.opaque.push_back(draw);
    draw.kind = DrawKind::Indirect;
    frame.opaque.push_back(draw);
    draw.kind = DrawKind::Vertices;
    draw.pipeline = transparent;
    draw.first = 12;
    draw.distance = 1;
    frame.transparent.push_back(draw);
    auto builder = CreatePipeline(PipelineType::Forward, {true, true});
    builder.AddFeature(std::make_shared<UnusedFeature>(), RenderStage::AfterTransparent)
        .AddFeature(std::make_shared<InspectOpaqueFeature>(), RenderStage::BeforeTransparent);
    const auto readback = runtime.CreateBuffer(
        {uint64_t(TextureRowPitch(width)) * height, Memory::Readback, "Forward.Readback"});
    const auto snapshot = runtime.CreateBuffer(
        {uint64_t(TextureRowPitch(width)) * height, Memory::Readback, "Snapshot.Readback"});
    const auto before = runtime.CreateBuffer(
        {uint64_t(TextureRowPitch(width)) * height, Memory::Readback, "Opaque.Readback"});
    builder
        .AddFeature(std::make_shared<detail::ReadbackFeature>(
                        "CameraColor", readback, TextureRowPitch(width), "Forward.Transparent"),
                    RenderStage::AfterRendering)
        .AddFeature(std::make_shared<detail::ReadbackFeature>("AfterTransparentColor", snapshot,
                                                              TextureRowPitch(width)),
                    RenderStage::AfterRendering)
        .AddFeature(std::make_shared<detail::ReadbackFeature>("OpaqueColor", before,
                                                              TextureRowPitch(width)),
                    RenderStage::AfterRendering);
    auto pipeline = builder.Create();
    pipeline.Build(graph, frame);
    graph.Compile();
    const auto submitted = graph.Execute(pool);
    runtime.Wait(submitted.completion);
    const auto actual = readback->Map(), expected = baseline->Map(), after = snapshot->Map();
    detail::Require(actual.size() == expected.size() &&
                        std::memcmp(actual.data(), expected.data(), actual.size()) == 0,
                    "Ordinary forward and direct RHI images differ");
    detail::Require(after.size() == actual.size() &&
                        std::memcmp(actual.data(), after.data(), actual.size()) == 0,
                    "AfterTransparentColor must be an exact physical snapshot");
    const auto old = before->Map();
    detail::Require(old.size() == actual.size() &&
                        std::memcmp(old.data(), actual.data(), actual.size()) != 0,
                    "OpaqueColor must survive subsequent transparent rendering");
    const auto& plan = graph.GetPlan();
    for (const auto& pass : plan.passes)
        if (pass.name == "Unused.Feature")
            detail::Require(!pass.live, "Stage order must not retain unused features");
    const auto json = plan.ToJson();
    WriteFileAtomically(output / "forward-plan.json",
                        std::as_bytes(std::span{json.data(), json.size()}));
}
} // namespace renderlab::samples
