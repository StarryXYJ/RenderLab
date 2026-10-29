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
    BufferHandle vertices, indices, indirect, count;
    std::vector<TextureHandle> textures;
    std::vector<BufferHandle> buffers;
};
ResolvedDraw Declare(RasterPassBuilder& pass, const DrawPacket& draw)
{
    Require(draw.pipeline && !draw.pipeline->desc.compute,
            "Forward draws require a raster pipeline");
    Require(draw.kind == DrawKind::Vertices || draw.kind == DrawKind::Indexed ||
                draw.kind == DrawKind::Indirect || draw.kind == DrawKind::IndexedIndirect ||
                draw.kind == DrawKind::IndexedSequence,
            "Unknown forward draw kind");
    ResolvedDraw result;
    result.draw = draw;
    result.vertices = pass.ReadBuffer(draw.vertices, Use::Vertex);
    if (draw.kind == DrawKind::Indexed || draw.kind == DrawKind::IndexedIndirect ||
        draw.kind == DrawKind::IndexedSequence)
        result.indices = pass.ReadBuffer(draw.indices, Use::Index);
    if (draw.kind == DrawKind::Indirect || draw.kind == DrawKind::IndexedIndirect)
        result.indirect =
            pass.ReadBuffer(draw.indirect, Use::Indirect,
                            {draw.indirectOffset, uint64_t(draw.indirectMaxCount) *
                                                      (draw.kind == DrawKind::IndexedIndirect
                                                           ? sizeof(rhi::DrawIndexedArguments)
                                                           : sizeof(rhi::DrawArguments))});
    if (!draw.indirectCount.empty())
        result.count =
            pass.ReadBuffer(draw.indirectCount, Use::Indirect, {draw.indirectCountOffset, 4});
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
    context.StencilReference(draw.stencilReference);
    for (size_t i = 0; i < draw.textures.size(); ++i)
        context.BindTexture(draw.textures[i].slot, resolved.textures[i], draw.textures[i].space,
                            draw.textures[i].arrayElement);
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
        context.Draw(draw.count, draw.first, draw.instances, draw.firstInstance);
        break;
    case DrawKind::Indexed:
        context.DrawIndexed(draw.count, draw.first, draw.instances, draw.firstInstance,
                            draw.vertexOffset);
        break;
    case DrawKind::IndexedSequence:
        for (const auto& argument : draw.indexedDraws)
            context.DrawIndexed(argument.indexCount, argument.firstIndex, argument.instanceCount,
                                argument.firstInstance, argument.vertexOffset);
        break;
    case DrawKind::IndexedIndirect:
    case DrawKind::Indirect:
        context.DrawIndirect(resolved.indirect, draw.indirectOffset, draw.indirectMaxCount,
                             draw.kind == DrawKind::IndexedIndirect, resolved.count,
                             draw.indirectCountOffset);
        break;
    }
}
} // namespace
void ShadowFeature::Build(rendergraph::Graph& graph, const RenderFrame& frame)
{
    const auto size = frame.shadowResolution;
    graph.CreateTexture("MainLightShadow", {size, size, rhi::Format::Rgba32Float, false, true});
    graph.CreateTexture("MainLightShadowDepth", {size, size, frame.shadowDepthFormat, false, true});
    auto pass = graph.AddRasterPass("MainLight.Shadow");
    pass.ColorTarget("MainLightShadow", rhi::LoadOp::Clear, {0, 0, 0, 0});
    pass.DepthTarget("MainLightShadowDepth");
    std::vector<ResolvedDraw> draws;
    for (const auto& draw : frame.shadowCasters)
        draws.push_back(Declare(pass, draw));
    pass.Execute(
        [draws = std::move(draws)](RasterContext& context)
        {
            for (const auto& draw : draws)
                Record(context, draw);
        });
}
void OpaqueFeature::Build(rendergraph::Graph& graph, const RenderFrame& frame)
{
    const auto color = graph.GetTextureDesc(graph.GetTexture("CameraColor"));
    const auto depth = graph.GetTextureDesc(graph.GetTexture("CameraDepth"));
    Require(color.attachment && !rhi::IsDepthFormat(color.format) && depth.attachment &&
                rhi::IsDepthFormat(depth.format) && color.width == depth.width &&
                color.height == depth.height,
            "Forward requires matching color and depth/stencil attachments");
    auto pass = graph.AddRasterPass("Forward.Opaque");
    pass.ColorTarget("CameraColor", rhi::LoadOp::Clear, frame.clearColor);
    pass.DepthTarget("CameraDepth");
    std::vector<ResolvedDraw> draws;
    auto input = frame.opaque;
    for (const auto& draw : input)
        Require(std::isfinite(draw.distance) && draw.distance >= 0,
                "Opaque draw distance must be finite and nonnegative");
    std::stable_sort(input.begin(), input.end(), [](const auto& a, const auto& b)
                     { return a.queue == b.queue ? a.distance < b.distance : a.queue < b.queue; });
    for (const auto& draw : input)
        draws.push_back(Declare(pass, draw));
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
    std::stable_sort(input.begin(), input.end(), [](const auto& a, const auto& b)
                     { return a.queue == b.queue ? a.distance > b.distance : a.queue < b.queue; });
    auto pass = graph.AddRasterPass("Forward.Transparent");
    pass.ColorTarget("CameraColor", rhi::LoadOp::Load);
    const auto depthAccess =
        std::ranges::any_of(input, [](const auto& draw)
                            { return draw.pipeline && draw.pipeline->desc.depth.writeEnabled; })
            ? AttachmentAccess::ReadWrite
            : AttachmentAccess::ReadOnly;
    const auto stencilAccess =
        std::ranges::any_of(input, [](const auto& draw)
                            { return draw.pipeline && draw.pipeline->desc.stencil.Writes(); })
            ? AttachmentAccess::ReadWrite
            : AttachmentAccess::ReadOnly;
    const auto depth = graph.GetTexture("CameraDepth");
    if (rhi::HasStencil(graph.GetTextureDesc(depth).format))
        pass.DepthStencilTarget(depth, {rhi::LoadOp::Load, depthAccess},
                                {rhi::LoadOp::Load, stencilAccess});
    else
        pass.DepthTarget(depth, rhi::LoadOp::Load, depthAccess);
    std::vector<ResolvedDraw> draws;
    for (const auto& draw : input)
        draws.push_back(Declare(pass, draw));
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
    Require(!rhi::IsDepthFormat(desc.format), "Color snapshot cannot copy depth");
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
