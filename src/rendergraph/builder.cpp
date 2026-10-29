#include "rendergraph/detail/graph_impl.hpp"

#include <algorithm>
#include <type_traits>

namespace renderlab::rendergraph
{
void ComputePassBuilder::AllowAsyncCompute()
{
    auto& g = *graph_.impl_;
    g.Builder(pass_);
    g.passes[pass_].asyncEligible = true;
}
using detail::Require;
namespace
{
void Range(const detail::Resource& r, TextureRange range)
{
    Require(range.firstMip == 0 && range.mipCount == 1 && range.firstLayer == 0 &&
                range.layerCount == 1,
            "Texture mip/layer range exceeds current single-subresource RHI");
    const auto aspect = rhi::HasStencil(r.textureDesc.format)      ? Aspect::DepthStencil
                        : rhi::IsDepthFormat(r.textureDesc.format) ? Aspect::Depth
                                                                   : Aspect::Color;
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
                    found->use == access.use,
                "Conflicting access in pass " + passes[pass].name +
                    "; declare one range/use or preserving write for ReadWrite");
        if (!resources[access.resource].isTexture)
        {
            const auto end = std::max(found->bufferRange.offset + found->bufferRange.size,
                                      access.bufferRange.offset + access.bufferRange.size);
            found->bufferRange.offset =
                std::min(found->bufferRange.offset, access.bufferRange.offset);
            found->bufferRange.size = end - found->bufferRange.offset;
        }
        return;
    }
    accesses.push_back(access);
}
TextureHandle PassBuilder::ReadTexture(TextureHandle handle, std::optional<Use> requested,
                                       TextureRange range)
{
    auto& g = *graph_.impl_;
    g.Builder(pass_);
    auto& r = g.Validate(handle.graph, handle.resource, handle.version, true);
    Range(r, range);
    const auto use = requested.value_or(DefaultRead(r.isTexture));
    g.ValidateUse(pass_, r, use, false);
    g.AddAccess(pass_, {handle.resource, handle.version, handle.version, use, true, false});
    return handle;
}
BufferHandle PassBuilder::ReadBuffer(BufferHandle handle, std::optional<Use> requested,
                                     BufferRange range)
{
    auto& g = *graph_.impl_;
    g.Builder(pass_);
    auto& r = g.Validate(handle.graph, handle.resource, handle.version, false);
    Range(r, range);
    const auto use = requested.value_or(DefaultRead(r.isTexture));
    g.ValidateUse(pass_, r, use, false);
    if (!range.size)
        range.size = r.bufferDesc.size - range.offset;
    Require((use != Use::Vertex && use != Use::Index) ||
                (range.offset == 0 && range.size == r.bufferDesc.size),
            "Geometry bindings currently require whole-buffer declarations");
    g.AddAccess(pass_, {handle.resource, handle.version, handle.version, use, true, false, range});
    return handle;
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
    Require(pass.kind == PassKind::Raster && pass.colors.size() < 8,
            "Raster pass supports at most eight color attachments");
    Require(std::ranges::none_of(pass.colors, [&](const auto& attachment)
                                 { return attachment.resource == handle.resource; }),
            "Raster pass cannot attach one texture more than once");
    Require(load != rhi::LoadOp::Discard,
            "Use Clear or Load; discard with partial raster coverage is not initialized");
    auto next = Write(handle, Use::Color,
                      load == rhi::LoadOp::Load ? Coverage::Preserve : Coverage::Full, {});
    pass.colors.push_back(detail::Attachment{handle.resource, load, clear});
    return next;
}
TextureHandle PassBuilder::Depth(TextureHandle handle, AttachmentAspect depth,
                                 AttachmentAspect stencil, uint32_t clearStencil)
{
    auto& g = *graph_.impl_;
    g.Builder(pass_);
    auto& resource = g.Validate(handle.graph, handle.resource, handle.version, true);
    auto& pass = g.passes[pass_];
    const bool hasStencil = rhi::HasStencil(resource.textureDesc.format);
    Require(pass.kind == PassKind::Raster && !pass.depth,
            "Raster pass supports one depth/stencil attachment");
    Require(clearStencil <= 255, "Stencil clear must be in [0,255]");
    const auto validate = [](AttachmentAspect aspect)
    {
        Require(aspect.load != rhi::LoadOp::Discard, "Depth/stencil requires Clear or Load");
        Require(aspect.access != AttachmentAccess::ReadOnly || aspect.load == rhi::LoadOp::Load,
                "Read-only depth/stencil requires Load");
    };
    validate(depth);
    if (hasStencil)
        validate(stencil);
    const bool readOnly = depth.access == AttachmentAccess::ReadOnly &&
                          (!hasStencil || stencil.access == AttachmentAccess::ReadOnly);
    const bool preserve =
        depth.load == rhi::LoadOp::Load || (hasStencil && stencil.load == rhi::LoadOp::Load);
    // Clearing one plane must not discard the other plane's producer or initialization.
    auto next = readOnly
                    ? ReadTexture(handle, Use::DepthRead)
                    : Write(handle, Use::Depth, preserve ? Coverage::Preserve : Coverage::Full, {});
    pass.depthReadOnly = depth.access == AttachmentAccess::ReadOnly;
    pass.stencilReadOnly = !hasStencil || stencil.access == AttachmentAccess::ReadOnly;
    pass.stencilLoad = stencil.load;
    pass.clearStencil = clearStencil;
    pass.depth = detail::Attachment{handle.resource, depth.load, {}};
    return next;
}
void PassBuilder::SideEffect()
{
    auto& g = *graph_.impl_;
    g.Builder(pass_);
    g.passes[pass_].sideEffect = true;
}

Use PassBuilder::DefaultRead(bool texture) const
{
    auto& g = *graph_.impl_;
    g.Builder(pass_);
    return g.passes[pass_].kind == PassKind::Copy ? Use::CopySource
           : texture                              ? Use::Sampled
                                                  : Use::ShaderRead;
}
Use PassBuilder::DefaultWrite() const
{
    auto& g = *graph_.impl_;
    g.Builder(pass_);
    return g.passes[pass_].kind == PassKind::Copy ? Use::CopyDestination : Use::Storage;
}
TextureHandle PassBuilder::ReadTexture(std::string_view name, std::optional<Use> use,
                                       TextureRange range)
{
    return ReadTexture(graph_.GetTexture(name), use, range);
}
BufferHandle PassBuilder::ReadBuffer(std::string_view name, std::optional<Use> use,
                                     BufferRange range)
{
    return ReadBuffer(graph_.GetBuffer(name), use, range);
}
TextureHandle PassBuilder::WriteTexture(TextureHandle h, std::optional<Use> use, TextureRange range)
{
    return Write(h, use.value_or(DefaultWrite()), Coverage::Full, range);
}
TextureHandle PassBuilder::WriteTexture(std::string_view name, std::optional<Use> use,
                                        TextureRange range)
{
    return WriteTexture(graph_.GetTexture(name), use, range);
}
BufferHandle PassBuilder::WriteBuffer(BufferHandle h, std::optional<Use> use, BufferRange range)
{
    return Write(h, use.value_or(DefaultWrite()), Coverage::Full, range);
}
BufferHandle PassBuilder::WriteBuffer(std::string_view name, std::optional<Use> use,
                                      BufferRange range)
{
    return WriteBuffer(graph_.GetBuffer(name), use, range);
}
TextureHandle PassBuilder::ReadWriteTexture(TextureHandle h, TextureRange range)
{
    return Write(h, Use::Storage, Coverage::Preserve, range);
}
TextureHandle PassBuilder::ReadWriteTexture(std::string_view name, TextureRange range)
{
    return ReadWriteTexture(graph_.GetTexture(name), range);
}
BufferHandle PassBuilder::ReadWriteBuffer(BufferHandle h, BufferRange range)
{
    return Write(h, Use::Storage, Coverage::Preserve, range);
}
BufferHandle PassBuilder::ReadWriteBuffer(std::string_view name, BufferRange range)
{
    return ReadWriteBuffer(graph_.GetBuffer(name), range);
}
BufferHandle PassBuilder::ReadWriteBuffer(BufferHandle handle, Use use, BufferRange range)
{
    Require(use == Use::Storage || use == Use::AccelerationStructureBuildWrite,
            "ReadWriteBuffer requires storage or acceleration-structure build access");
    return Write(handle, use, Coverage::Preserve, range);
}
BufferHandle PassBuilder::ReadWriteBuffer(std::string_view name, Use use, BufferRange range)
{
    return ReadWriteBuffer(graph_.GetBuffer(name), use, range);
}
PassHandle PassBuilder::Handle() const
{
    return graph_.GetPass(pass_);
}
PassHandle PassBuilder::Finish(detail::Callback callback)
{
    auto& g = *graph_.impl_;
    g.Builder(pass_);
    Require(callback.index() == static_cast<size_t>(g.passes[pass_].kind) + 1,
            "Callback kind does not match pass");
    const bool valid = std::visit(
        [](const auto& fn)
        {
            if constexpr (std::is_same_v<std::decay_t<decltype(fn)>, std::monostate>)
                return false;
            else
                return bool(fn);
        },
        callback);
    Require(valid, "Empty Execute callback");
    g.passes[pass_].callback = std::move(callback);
    g.active = UINT32_MAX;
    return {g.id, pass_};
}
TextureHandle RasterPassBuilder::ColorTarget(TextureHandle h, rhi::LoadOp load,
                                             std::array<float, 4> clear)
{
    return Color(h, load, clear);
}
TextureHandle RasterPassBuilder::ColorTarget(std::string_view name, rhi::LoadOp load,
                                             std::array<float, 4> clear)
{
    return ColorTarget(graph_.GetTexture(name), load, clear);
}
TextureHandle RasterPassBuilder::DepthTarget(TextureHandle h, rhi::LoadOp load,
                                             AttachmentAccess access)
{
    return Depth(h, {load, access}, {load, access}, 0);
}
TextureHandle RasterPassBuilder::DepthTarget(std::string_view name, rhi::LoadOp load,
                                             AttachmentAccess access)
{
    return DepthTarget(graph_.GetTexture(name), load, access);
}
TextureHandle RasterPassBuilder::DepthStencilTarget(TextureHandle h, AttachmentAspect depth,
                                                    AttachmentAspect stencil, uint32_t clearStencil)
{
    Require(rhi::HasStencil(graph_.GetTextureDesc(h).format),
            "DepthStencilTarget requires a stencil format");
    return Depth(h, depth, stencil, clearStencil);
}
TextureHandle RasterPassBuilder::DepthStencilTarget(std::string_view name, AttachmentAspect depth,
                                                    AttachmentAspect stencil, uint32_t clearStencil)
{
    return DepthStencilTarget(graph_.GetTexture(name), depth, stencil, clearStencil);
}
PassHandle RasterPassBuilder::Execute(std::function<void(RasterContext&)> fn)
{
    return Finish(std::move(fn));
}
PassHandle ComputePassBuilder::Execute(std::function<void(ComputeContext&)> fn)
{
    return Finish(std::move(fn));
}
PassHandle CopyPassBuilder::Execute(std::function<void(CopyContext&)> fn)
{
    return Finish(std::move(fn));
}
PassHandle RayTracingPassBuilder::Execute(std::function<void(RayTracingContext&)> callback)
{
    return Finish(std::move(callback));
}
} // namespace renderlab::rendergraph
