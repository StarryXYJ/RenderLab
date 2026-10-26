#include "samples/rhi/rhi_demo.hpp"

#include "core/log.hpp"
#include "platform/text.hpp"
#include "rhi/commands.hpp"
#include "rhi/runtime.hpp"
#include "rhi/upload.hpp"
#include "samples/common/check.hpp"
#include "samples/common/demo_geometry.hpp"
#include "samples/common/shader_io.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace renderlab::samples
{
using detail::Require;

namespace
{
constexpr uint32_t NoiseWidth = 257, NoiseHeight = 131;
} // namespace

RhiDemo::RhiDemo(rhi::Runtime& runtime, const std::filesystem::path& shaderDirectory,
                 Logger& logger)
    : runtime_(runtime)
{
    using namespace rhi;
    const auto shader = [&](ShaderStage stage, const char* name)
    {
        return runtime_.CreateShader(
            stage, LoadShader(platform::WideToUtf8((shaderDirectory / name).wstring())),
            stage == ShaderStage::Vertex  ? "VSMain"
            : stage == ShaderStage::Pixel ? "PSMain"
                                          : "CSMain");
    };
    vertex_ = shader(ShaderStage::Vertex, "vertex.bin");
    pixel_ = shader(ShaderStage::Pixel, "pixel.bin");
    graphicsLayout_ =
        runtime_.CreateLayout({{1, BindingKind::SampledTexture}, {2, BindingKind::Sampler}});
    noise_ = runtime_.CreateTexture(
        {NoiseWidth, NoiseHeight, Format::Rgba8, true, false, "Compute.Noise257x131"});
    const auto view = runtime_.CreateView(noise_);
    graphicsGroup_ =
        runtime_.CreateGroup(graphicsLayout_, {{1, view, {}}, {2, {}, runtime_.CreateSampler()}});
    auto computeLayout = runtime_.CreateLayout({{0, BindingKind::StorageTexture}});
    auto computeGroup = runtime_.CreateGroup(computeLayout, {{0, view, {}}});
    PipelineDesc computeDesc;
    computeDesc.layout = computeLayout;
    computeDesc.compute = shader(ShaderStage::Compute, "noise.bin");
    auto initialize = runtime_.CreatePipeline(computeDesc);
    computeDesc.compute = shader(ShaderStage::Compute, "noise_update.bin");
    auto update = runtime_.CreatePipeline(computeDesc);
    const auto geometry = detail::CreateDemoGeometry(runtime_);
    vertices_ = geometry.vertices;
    indices_ = geometry.indices;
    indirect_ = geometry.indirect;
    auto commands = runtime_.CreateCommands();
    commands->BeginMarker("Noise.Init -> UAV dependency -> Noise.Update");
    commands->Barrier(noise_, State::Undefined, State::Storage);
    commands->Bind(initialize);
    commands->Bind(computeGroup);
    commands->Compute((NoiseWidth + 7) / 8, (NoiseHeight + 7) / 8);
    commands->Barrier(noise_, State::Storage, State::Storage);
    commands->Bind(update);
    commands->Bind(computeGroup);
    commands->Compute((NoiseWidth + 7) / 8, (NoiseHeight + 7) / 8);
    commands->Barrier(noise_, State::Storage, State::Sampled);
    commands->EndMarker();
    // Release the application's compute objects before submission. Packets retain the dependency
    // chain.
    std::weak_ptr<Pipeline> weak = update;
    initialize.reset();
    update.reset();
    computeGroup.reset();
    computeLayout.reset();
    Require(!weak.expired(), "pipeline not retained by recording");
    runtime_.Wait(runtime_.Submit(std::move(commands)));
    Require(weak.expired(), "compute pipeline not retired after completion");
    graphicsRgba_ = Graphics(Format::Rgba8Srgb);
    logger.Write(LogLevel::Info, "M1 scene prepared: indexed triangle, two compute passes, "
                                 "sampled noise, reversed-Z, blend and indirect draw");
}
std::shared_ptr<rhi::Pipeline> RhiDemo::Graphics(rhi::Format format)
{
    Require(format == rhi::Format::Rgba8Srgb || format == rhi::Format::Bgra8Srgb,
            "demo requires sRGB color attachment");
    auto& cached = format == rhi::Format::Rgba8Srgb ? graphicsRgba_ : graphicsBgra_;
    if (!cached)
    {
        rhi::PipelineDesc desc;
        desc.layout = graphicsLayout_;
        desc.vertex = vertex_;
        desc.pixel = pixel_;
        desc.color = format;
        desc.depthStencilFormat = rhi::Format::D32;
        desc.depth = {true, true};
        desc.blend.enabled = true;
        desc.vertexStride = sizeof(detail::DemoVertex);
        desc.attributes = {{"POSITION", 0, rhi::VertexFormat::Float3, 0},
                           {"COLOR", 1, rhi::VertexFormat::Float4, 12},
                           {"TEXCOORD", 2, rhi::VertexFormat::Float2, 28}};
        cached = runtime_.CreatePipeline(desc);
    }
    return cached;
}
void RhiDemo::Prepare(rhi::Format format)
{
    Graphics(format);
}
void RhiDemo::Render(rhi::CommandList& commands, std::shared_ptr<rhi::TextureView> color,
                     std::shared_ptr<rhi::TextureView> depth)
{
    const auto pipeline =
        color->texture->desc.format == rhi::Format::Rgba8Srgb ? graphicsRgba_ : graphicsBgra_;
    Require(bool(pipeline), "prepare the attachment format before recording");
    commands.BeginMarker("Triangle + sampled compute noise");
    commands.Begin(std::move(color), std::move(depth));
    commands.Bind(pipeline);
    commands.Bind(graphicsGroup_);
    commands.Geometry(vertices_, indices_);
    commands.DrawIndexed(3);
    commands.DrawVertices(6, 3);
    commands.DrawFrom(indirect_);
    commands.DrawVertices(6, 12);
    commands.End();
    commands.EndMarker();
}
} // namespace renderlab::samples
