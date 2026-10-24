#include "renderer/forward.hpp"

#include "renderer/frame.hpp"
#include "rendergraph/graph.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace renderlab::renderer
{
namespace
{
using namespace rendergraph;
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::invalid_argument(message);
}
struct ResolvedDraw
{
    DrawPacket draw;
    BufferHandle vertices, indices, indirect;
    std::vector<TextureHandle> textures;
    std::vector<BufferHandle> buffers;
};
ResolvedDraw Declare(RasterPassBuilder& pass, const DrawPacket& draw, bool transparent)
{
    Require(draw.pipeline && !draw.pipeline->desc.compute && draw.pipeline->desc.depth,
            "Forward draws require a raster pipeline with depth testing");
    Require(transparent ? draw.pipeline->desc.blend && !draw.pipeline->desc.depthWrite
                        : !draw.pipeline->desc.blend && draw.pipeline->desc.depthWrite,
            "Forward opaque requires depth writes without blend; transparent requires blend "
            "without depth writes");
    Require(draw.kind == DrawKind::Vertices || draw.kind == DrawKind::Indexed ||
                draw.kind == DrawKind::Indirect,
            "Unknown forward draw kind");
    ResolvedDraw result;
    result.draw = draw;
    result.vertices = pass.ReadBuffer(draw.vertices, Use::Vertex);
    if (draw.kind == DrawKind::Indexed)
        result.indices = pass.ReadBuffer(draw.indices, Use::Index);
    if (draw.kind == DrawKind::Indirect)
        result.indirect = pass.ReadBuffer(draw.indirect, Use::Indirect,
                                          {draw.indirectOffset, sizeof(rhi::DrawArguments)});
    for (const auto& binding : draw.textures)
        result.textures.push_back(pass.ReadTexture(binding.texture));
    for (const auto& binding : draw.buffers)
    {
        Require(binding.use == Use::Uniform || binding.use == Use::ShaderRead,
                "Forward draw buffer bindings require Uniform or ShaderRead");
        result.buffers.push_back(pass.ReadBuffer(binding.buffer, binding.use, binding.range));
    }
    return result;
}
void Record(RasterContext& context, const ResolvedDraw& resolved)
{
    const auto& draw = resolved.draw;
    context.Bind(draw.pipeline);
    for (size_t i = 0; i < draw.textures.size(); ++i)
        context.BindTexture(draw.textures[i].slot, resolved.textures[i], draw.textures[i].space);
    for (size_t i = 0; i < draw.buffers.size(); ++i)
    {
        const auto& b = draw.buffers[i];
        context.BindBuffer(b.slot, resolved.buffers[i], b.range.offset, b.range.size, b.stride,
                           b.space);
    }
    for (const auto& b : draw.samplers)
        context.BindSampler(b.slot, b.sampler, b.space);
    context.Geometry(resolved.vertices, resolved.indices);
    switch (draw.kind)
    {
    case DrawKind::Vertices:
        context.Draw(draw.count, draw.first);
        break;
    case DrawKind::Indexed:
        context.DrawIndexed(draw.count, draw.first);
        break;
    case DrawKind::Indirect:
        context.DrawIndirect(resolved.indirect, draw.indirectOffset);
        break;
    }
}
} // namespace
void OpaqueFeature::Build(rendergraph::Graph& graph, const RenderFrame& frame)
{
    const auto color = graph.GetTextureDesc(graph.GetTexture("CameraColor"));
    const auto depth = graph.GetTextureDesc(graph.GetTexture("CameraDepth"));
    Require(color.attachment && color.format != rhi::Format::D32 && depth.attachment &&
                depth.format == rhi::Format::D32 && color.width == depth.width &&
                color.height == depth.height,
            "Forward requires matching color and D32 attachments");
    auto pass = graph.AddRasterPass("Forward.Opaque");
    pass.ColorTarget("CameraColor", rhi::LoadOp::Clear, frame.clearColor);
    pass.DepthTarget("CameraDepth");
    std::vector<ResolvedDraw> draws;
    for (const auto& draw : frame.opaque)
        draws.push_back(Declare(pass, draw, false));
    pass.Execute(
        [draws = std::move(draws)](RasterContext& context)
        {
            for (const auto& draw : draws)
                Record(context, draw);
        });
}
void TransparentFeature::Build(rendergraph::Graph& graph, const RenderFrame& frame)
{
    if (frame.transparent.empty())
        return;
    auto input = frame.transparent;
    for (const auto& draw : input)
        Require(std::isfinite(draw.distance) && draw.distance >= 0,
                "Transparent draw distance must be finite and nonnegative");
    std::stable_sort(input.begin(), input.end(),
                     [](const auto& a, const auto& b) { return a.distance > b.distance; });
    auto pass = graph.AddRasterPass("Forward.Transparent");
    pass.ColorTarget("CameraColor", rhi::LoadOp::Load);
    pass.DepthTarget("CameraDepth", rhi::LoadOp::Load, DepthAccess::ReadOnly);
    std::vector<ResolvedDraw> draws;
    for (const auto& draw : input)
        draws.push_back(Declare(pass, draw, true));
    pass.Execute(
        [draws = std::move(draws)](RasterContext& context)
        {
            for (const auto& draw : draws)
                Record(context, draw);
        });
}
void ColorSnapshotFeature::Build(rendergraph::Graph& graph, const RenderFrame&)
{
    const auto source = graph.GetTexture(source_);
    auto desc = graph.GetTextureDesc(source);
    Require(desc.format != rhi::Format::D32, "Color snapshot cannot copy depth");
    desc.attachment = false;
    desc.storage = false;
    auto destination = graph.CreateTexture(destination_, desc);
    auto pass = graph.AddCopyPass("Snapshot." + destination_);
    pass.ReadTexture(source);
    destination = pass.WriteTexture(destination);
    pass.Execute([source, destination](CopyContext& context)
                 { context.CopyTexture(source, destination); });
}
} // namespace renderlab::renderer
