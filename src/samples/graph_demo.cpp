#include "samples/graph_demo.hpp"

#include "core/file_io.hpp"
#include "platform/text.hpp"
#include "renderer/pipeline.hpp"
#include "rhi/upload.hpp"
#include "samples/detail/graph_features.hpp"
#include "samples/shader_io.hpp"

namespace renderlab::samples
{
GraphDemo::GraphDemo(rhi::Runtime& runtime, const std::filesystem::path& shaders)
    : runtime_(runtime), pool_(runtime), geometry_(detail::CreateDemoGeometry(runtime))
{
    const auto shader = [&](rhi::ShaderStage stage, const char* file, const char* entry)
    {
        return runtime.CreateShader(
            stage, LoadShader(platform::WideToUtf8((shaders / file).wstring())), entry);
    };
    vertex_ = shader(rhi::ShaderStage::Vertex, "vertex.bin", "VSMain");
    pixel_ = shader(rhi::ShaderStage::Pixel, "pixel.bin", "PSMain");
    computeLayout_ = runtime.CreateLayout({{0, rhi::BindingKind::StorageTexture}});
    rasterLayout_ = runtime.CreateLayout(
        {{1, rhi::BindingKind::SampledTexture}, {2, rhi::BindingKind::Sampler}});
    sampler_ = runtime.CreateSampler();
    rhi::PipelineDesc desc;
    desc.layout = computeLayout_;
    desc.compute = shader(rhi::ShaderStage::Compute, "noise.bin", "CSMain");
    initialize_ = runtime.CreatePipeline(desc);
    desc.compute = shader(rhi::ShaderStage::Compute, "noise_update.bin", "CSMain");
    update_ = runtime.CreatePipeline(desc);
}
void GraphDemo::Prepare(rhi::Format format)
{
    if (pipelines_.contains(format))
        return;
    rhi::PipelineDesc desc;
    desc.layout = rasterLayout_;
    desc.vertex = vertex_;
    desc.pixel = pixel_;
    desc.color = format;
    desc.depth = true;
    desc.blend = true;
    desc.vertexStride = sizeof(detail::DemoVertex);
    desc.attributes = {{"POSITION", 0, rhi::VertexFormat::Float3, 0},
                       {"COLOR", 1, rhi::VertexFormat::Float4, 12},
                       {"TEXCOORD", 2, rhi::VertexFormat::Float2, 28}};
    pipelines_.emplace(format, runtime_.CreatePipeline(std::move(desc)));
}
GraphReadback GraphDemo::Render(std::shared_ptr<rhi::Texture> target, rhi::State initial,
                                rhi::Swapchain* presentation, bool readback,
                                GraphFrameSettings settings, const std::filesystem::path& dump)
{
    Prepare(target->desc.format);
    rendergraph::Graph graph;
    const detail::NoisePrograms noisePrograms{initialize_, update_};
    graph.ImportTexture("CameraColor", target, {initial, initial != rhi::State::Undefined, 0});
    graph.CreateTexture("CameraDepth",
                        {target->desc.width, target->desc.height, rhi::Format::D32, false, true});
    auto noise = std::make_shared<detail::NoiseFeature>(noisePrograms, "Noise", 257, 131,
                                                        settings.updateNoise);
    auto scene = std::make_shared<detail::DemoSceneFeature>(
        detail::RasterPrograms{pipelines_.at(target->desc.format), sampler_}, geometry_);
    auto pipeline = renderer::CreatePipeline(renderer::PipelineType::Custom);
    pipeline.AddFeature(scene).AddFeature(noise);
    if (settings.unusedFeature)
        pipeline.AddFeature(
            std::make_shared<detail::NoiseFeature>(noisePrograms, "UnusedNoise", 8, 8, false));
    GraphReadback result;
    if (readback)
    {
        result.noise = runtime_.CreateBuffer({uint64_t(rhi::TextureRowPitch(257)) * 131,
                                              rhi::Memory::Readback, "Graph.Noise.Readback"});
        result.color = runtime_.CreateBuffer(
            {uint64_t(rhi::TextureRowPitch(target->desc.width)) * target->desc.height,
             rhi::Memory::Readback, "Graph.Color.Readback"});
        pipeline.AddFeature(std::make_shared<detail::ReadbackFeature>("Noise", result.noise,
                                                                      rhi::TextureRowPitch(257)));
        pipeline.AddFeature(std::make_shared<detail::ReadbackFeature>(
            "CameraColor", result.color, rhi::TextureRowPitch(target->desc.width)));
    }
    const auto created = pipeline.Create();
    created.Build(graph, {});
    graph.Export("CameraColor", presentation ? rhi::State::Present : rhi::State::CopySource);
    graph.Compile({settings.cull});
    result.completion = graph.Execute(pool_, presentation).completion;
    if (!dump.empty())
    {
        const auto text = graph.GetPlan().ToJson();
        WriteFileAtomically(dump, std::as_bytes(std::span{text.data(), text.size()}));
    }
    return result;
}
} // namespace renderlab::samples
