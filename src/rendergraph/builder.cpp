#include "rendergraph/detail/graph_impl.hpp"

#include <algorithm>

namespace renderlab::rendergraph
{
using detail::Require;
namespace
{
void Range(const detail::Resource& r, TextureRange range)
{
    Require(range.firstMip == 0 && range.mipCount == 1 && range.firstLayer == 0 &&
                range.layerCount == 1,
            "Texture mip/layer range exceeds current single-subresource RHI");
    const auto aspect = r.textureDesc.format == rhi::Format::D32 ? Aspect::Depth : Aspect::Color;
    Require(range.aspect == Aspect::Auto || range.aspect == aspect, "Texture aspect mismatch");
}
void Range(const detail::Resource& r, BufferRange range)
{
    Require(range.offset < r.bufferDesc.size &&
                (!range.size || range.size <= r.bufferDesc.size - range.offset),
            "Buffer access range outside resource");
}
} // namespace
void Graph::Impl::AddAccess(uint32_t pass, detail::Access access)
{
    auto& accesses = passes[pass].accesses;
    auto found = std::ranges::find(accesses, access.resource, &detail::Access::resource);
    if (found != accesses.end())
    {
        Require(!access.write && !found->write && found->input == access.input &&
                    found->use == access.use && found->bufferRange == access.bufferRange,
                "Conflicting access in pass " + passes[pass].name +
                    "; declare one range/use or preserving write for ReadWrite");
        return;
    }
    accesses.push_back(access);
}
void PassBuilder::Read(TextureHandle handle, Use use, TextureRange range)
{
    auto& g = *graph_.impl_;
    g.Builder(pass_);
    auto& r = g.Validate(handle.graph, handle.resource, handle.version, true);
    Range(r, range);
    g.ValidateUse(pass_, r, use, false);
    g.AddAccess(pass_, {handle.resource, handle.version, handle.version, use, true, false});
}
void PassBuilder::Read(BufferHandle handle, Use use, BufferRange range)
{
    auto& g = *graph_.impl_;
    g.Builder(pass_);
    auto& r = g.Validate(handle.graph, handle.resource, handle.version, false);
    Range(r, range);
    g.ValidateUse(pass_, r, use, false);
    if (!range.size)
        range.size = r.bufferDesc.size - range.offset;
    Require((use != Use::Vertex && use != Use::Index) ||
                (range.offset == 0 && range.size == r.bufferDesc.size),
            "Geometry bindings currently require whole-buffer declarations");
    g.AddAccess(pass_, {handle.resource, handle.version, handle.version, use, true, false, range});
}
TextureHandle PassBuilder::Write(TextureHandle handle, Use use, Coverage coverage,
                                 TextureRange range)
{
    auto& g = *graph_.impl_;
    g.Builder(pass_);
    auto& r = g.Validate(handle.graph, handle.resource, handle.version, true);
    Range(r, range);
    g.ValidateUse(pass_, r, use, true);
    Require(handle.version + 1 == r.versions.size() && !r.exported,
            "Stale write handle or write after Export");
    const auto next = handle.version + 1;
    g.AddAccess(pass_,
                {handle.resource, handle.version, next, use, coverage == Coverage::Preserve, true});
    r.versions.push_back({pass_, coverage == Coverage::Preserve});
    return {handle.graph, handle.resource, next};
}
BufferHandle PassBuilder::Write(BufferHandle handle, Use use, Coverage coverage, BufferRange range)
{
    auto& g = *graph_.impl_;
    g.Builder(pass_);
    auto& r = g.Validate(handle.graph, handle.resource, handle.version, false);
    Range(r, range);
    g.ValidateUse(pass_, r, use, true);
    Require(coverage != Coverage::Full ||
                (range.offset == 0 && (!range.size || range.size == r.bufferDesc.size)),
            "Partial buffer write cannot claim full resource coverage");
    Require(handle.version + 1 == r.versions.size() && !r.exported,
            "Stale write handle or write after Export");
    if (!range.size)
        range.size = r.bufferDesc.size - range.offset;
    const auto next = handle.version + 1;
    g.AddAccess(pass_, {handle.resource, handle.version, next, use, coverage == Coverage::Preserve,
                        true, range});
    r.versions.push_back({pass_, coverage == Coverage::Preserve});
    return {handle.graph, handle.resource, next};
}
TextureHandle PassBuilder::Color(TextureHandle handle, rhi::LoadOp load, std::array<float, 4> clear)
{
    auto& g = *graph_.impl_;
    g.Builder(pass_);
    auto& pass = g.passes[pass_];
    Require(pass.kind == PassKind::Raster && !pass.color,
            "Raster pass supports one color attachment");
    Require(load != rhi::LoadOp::Discard,
            "Use Clear or Load; discard with partial raster coverage is not initialized");
    auto next =
        Write(handle, Use::Color, load == rhi::LoadOp::Load ? Coverage::Preserve : Coverage::Full);
    pass.color = detail::Attachment{handle.resource, load, clear};
    return next;
}
TextureHandle PassBuilder::Depth(TextureHandle handle, rhi::LoadOp load)
{
    auto& g = *graph_.impl_;
    g.Builder(pass_);
    auto& pass = g.passes[pass_];
    Require(pass.kind == PassKind::Raster && !pass.depth,
            "Raster pass supports one depth attachment");
    Require(load != rhi::LoadOp::Discard, "Depth attachment requires Clear or Load");
    auto next =
        Write(handle, Use::Depth, load == rhi::LoadOp::Load ? Coverage::Preserve : Coverage::Full);
    pass.depth = detail::Attachment{handle.resource, load, {}};
    return next;
}
void PassBuilder::SideEffect()
{
    auto& g = *graph_.impl_;
    g.Builder(pass_);
    g.passes[pass_].sideEffect = true;
}
} // namespace renderlab::rendergraph
