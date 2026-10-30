#include "renderer/deferred.hpp"

#include "renderer/frame.hpp"
#include "rendergraph/graph.hpp"

#include <array>
#include <stdexcept>
#include <utility>
#include <vector>

namespace renderlab::renderer
{
namespace
{
using namespace rendergraph;
struct ResolvedDraw
{
    DrawPacket draw;
    BufferHandle vertices, indices, indirect, count;
    std::vector<TextureHandle> textures;
    std::vector<BufferHandle> buffers;
};
ResolvedDraw Declare(RasterPassBuilder& pass, const DrawPacket& draw)
{
    if (!draw.pipeline || draw.pipeline->desc.compute)
        throw std::invalid_argument("Deferred draws require a raster pipeline");
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
        if (binding.use != Use::Uniform && binding.use != Use::ShaderRead)
            throw std::invalid_argument("Deferred draw buffers must be uniform or shader-read");
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
        const auto& binding = draw.buffers[i];
        context.BindBuffer(binding.slot, resolved.buffers[i], binding.range.offset,
                           binding.range.size, binding.stride, binding.space);
    }
    for (const auto& binding : draw.samplers)
        context.BindSampler(binding.slot, binding.sampler, binding.space);
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

void GBufferFeature::Build(rendergraph::Graph& graph, const RenderFrame& frame)
{
    const auto camera = graph.GetTextureDesc(graph.GetTexture("CameraColor"));
    for (const auto& name :
         {"GBuffer.AlbedoMetallic", "GBuffer.NormalRoughness", "GBuffer.EmissionOcclusion"})
        graph.CreateTexture(
            name, {camera.width, camera.height, rhi::Format::Rgba32Float, false, true, name});
    auto pass = graph.AddRasterPass("Deferred.GBuffer");
    pass.Category(PassCategory::Opaque);
    // Pixels with a cleared depth value are sky and ignored by DeferredLighting.
    constexpr std::array<float, 4> clear{.02f, .02f, .02f, 1};
    pass.ColorTarget("GBuffer.AlbedoMetallic", rhi::LoadOp::Clear, clear);
    pass.ColorTarget("GBuffer.NormalRoughness", rhi::LoadOp::Clear, clear);
    pass.ColorTarget("GBuffer.EmissionOcclusion", rhi::LoadOp::Clear, clear);
    pass.DepthTarget("CameraDepth");
    std::vector<ResolvedDraw> draws;
    draws.reserve(frame.gbuffer.size());
    for (const auto& draw : frame.gbuffer)
        draws.push_back(Declare(pass, draw));
    pass.Execute(
        [draws = std::move(draws)](RasterContext& context)
        {
            for (const auto& draw : draws)
                Record(context, draw);
        });
}

void DeferredLightingFeature::Build(rendergraph::Graph& graph, const RenderFrame& frame)
{
    auto pass = graph.AddRasterPass("Deferred.Lighting");
    pass.Category(PassCategory::Fullscreen);
    pass.ColorTarget("CameraColor", rhi::LoadOp::Clear, frame.clearColor);
    auto draw = Declare(pass, frame.deferredLighting);
    pass.Execute([draw = std::move(draw)](RasterContext& context) { Record(context, draw); });
}
} // namespace renderlab::renderer
