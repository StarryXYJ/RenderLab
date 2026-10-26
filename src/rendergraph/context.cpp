#include "rendergraph/detail/graph_impl.hpp"

#include <algorithm>

namespace renderlab::rendergraph
{
using detail::Require;
namespace
{
rhi::BindingKind Kind(const std::shared_ptr<rhi::Pipeline>& pipeline, uint32_t slot, uint32_t space)
{
    Require(bool(pipeline), "Bind a pipeline before resources");
    auto found = std::ranges::find_if(pipeline->desc.layout->entries, [&](const auto& e)
                                      { return e.slot == slot && e.space == space; });
    Require(found != pipeline->desc.layout->entries.end(),
            "Binding slot is absent from pipeline layout");
    return found->kind;
}
} // namespace
void BindingContext::Bind(std::shared_ptr<rhi::Pipeline> pipeline)
{
    auto& g = *graph_.impl_;
    g.Scope(pass_);
    Require(pipeline && ((g.passes[pass_].kind == PassKind::Compute && pipeline->desc.compute) ||
                         (g.passes[pass_].kind == PassKind::Raster && !pipeline->desc.compute)),
            "Pipeline kind does not match pass");
    Require(!g.passes[pass_].depthReadOnly || !pipeline->desc.depth.writeEnabled,
            "Read-only depth attachment requires depth writes disabled");
    Require(!g.passes[pass_].stencilReadOnly || !pipeline->desc.stencil.Writes(),
            "Read-only stencil attachment requires stencil writes disabled");
    commands_.Bind(pipeline);
    pipeline_ = std::move(pipeline);
    bindings_.clear();
    dirty_ = true;
}
void BindingContext::SetBinding(rhi::Binding binding)
{
    auto found =
        std::ranges::find_if(bindings_, [&](const auto& b)
                             { return b.slot == binding.slot && b.space == binding.space; });
    if (found == bindings_.end())
        bindings_.push_back(std::move(binding));
    else
        *found = std::move(binding);
    dirty_ = true;
}
void BindingContext::BindTexture(uint32_t slot, TextureHandle h, uint32_t space)
{
    auto& g = *graph_.impl_;
    g.Scope(pass_);
    const auto kind = Kind(pipeline_, slot, space);
    Require(kind == rhi::BindingKind::SampledTexture || kind == rhi::BindingKind::StorageTexture,
            "Texture binding kind mismatch");
    const auto use = kind == rhi::BindingKind::SampledTexture ? Use::Sampled : Use::Storage;
    auto& r = g.Resolve(pass_, h.graph, h.resource, h.version, true, use);
    rhi::Binding b{slot};
    b.view = g.pool->Device().CreateView(r.texture, rhi::TextureViewUsage::Sampled);
    b.space = space;
    SetBinding(std::move(b));
}
void BindingContext::BindBuffer(uint32_t slot, BufferHandle h, uint64_t offset, uint64_t size,
                                uint32_t stride, uint32_t space)
{
    auto& g = *graph_.impl_;
    g.Scope(pass_);
    const auto kind = Kind(pipeline_, slot, space);
    Require(kind == rhi::BindingKind::ConstantBuffer || kind == rhi::BindingKind::ReadOnlyBuffer ||
                kind == rhi::BindingKind::StorageBuffer,
            "Buffer binding kind mismatch");
    const auto use = kind == rhi::BindingKind::ConstantBuffer   ? Use::Uniform
                     : kind == rhi::BindingKind::ReadOnlyBuffer ? Use::ShaderRead
                                                                : Use::Storage;
    auto& r = g.Resolve(pass_, h.graph, h.resource, h.version, false, use);
    Require(offset < r.bufferDesc.size, "Buffer binding offset outside resource");
    if (!size)
        size = r.bufferDesc.size - offset;
    g.CheckBufferRange(pass_, h, offset, size);
    rhi::Binding b{slot};
    b.buffer = r.buffer;
    b.offset = offset;
    b.size = size;
    b.stride = stride;
    b.space = space;
    SetBinding(std::move(b));
}
void BindingContext::BindSampler(uint32_t slot, std::shared_ptr<rhi::Sampler> sampler,
                                 uint32_t space)
{
    graph_.impl_->Scope(pass_);
    Require(Kind(pipeline_, slot, space) == rhi::BindingKind::Sampler,
            "Sampler binding kind mismatch");
    rhi::Binding b{slot};
    b.sampler = std::move(sampler);
    b.space = space;
    SetBinding(std::move(b));
}
void BindingContext::FlushBindings()
{
    auto& g = *graph_.impl_;
    g.Scope(pass_);
    Require(bool(pipeline_), "Bind a pipeline before commands");
    if (dirty_)
    {
        auto group = g.pool->Device().CreateGroup(pipeline_->desc.layout, bindings_);
        g.ValidateBindings(pass_, *group);
        commands_.Bind(std::move(group));
        dirty_ = false;
    }
}
void RasterContext::StencilReference(uint32_t reference)
{
    graph_.impl_->Scope(pass_);
    commands_.StencilReference(reference);
}
void RasterContext::Geometry(BufferHandle vertices, BufferHandle indices)
{
    auto& g = *graph_.impl_;
    g.Scope(pass_);
    auto vertex =
        g.Resolve(pass_, vertices.graph, vertices.resource, vertices.version, false, Use::Vertex)
            .buffer;
    std::shared_ptr<rhi::Buffer> index;
    if (indices)
        index =
            g.Resolve(pass_, indices.graph, indices.resource, indices.version, false, Use::Index)
                .buffer;
    commands_.Geometry(std::move(vertex), std::move(index));
}
void RasterContext::Draw(uint32_t count, uint32_t first)
{
    FlushBindings();
    commands_.DrawVertices(count, first);
}
void RasterContext::DrawIndexed(uint32_t count, uint32_t first)
{
    FlushBindings();
    commands_.DrawIndexed(count, first);
}
void RasterContext::DrawIndirect(BufferHandle arguments, uint64_t offset)
{
    auto& g = *graph_.impl_;
    g.Scope(pass_);
    g.CheckBufferRange(pass_, arguments, offset, sizeof(rhi::DrawArguments));
    auto buffer = g.Resolve(pass_, arguments.graph, arguments.resource, arguments.version, false,
                            Use::Indirect)
                      .buffer;
    FlushBindings();
    commands_.DrawFrom(std::move(buffer), offset);
}
void ComputeContext::Dispatch(uint32_t x, uint32_t y, uint32_t z)
{
    FlushBindings();
    commands_.Compute(x, y, z);
}
void CopyContext::Copy(BufferHandle source, BufferHandle destination, uint64_t size,
                       uint64_t sourceOffset, uint64_t destinationOffset)
{
    auto& g = *graph_.impl_;
    g.Scope(pass_);
    g.CheckBufferRange(pass_, source, sourceOffset, size);
    g.CheckBufferRange(pass_, destination, destinationOffset, size);
    auto src =
        g.Resolve(pass_, source.graph, source.resource, source.version, false, Use::CopySource)
            .buffer;
    auto dst = g.Resolve(pass_, destination.graph, destination.resource, destination.version, false,
                         Use::CopyDestination)
                   .buffer;
    commands_.Copy(std::move(src), std::move(dst), size, sourceOffset, destinationOffset);
}
void CopyContext::CopyTexture(BufferHandle buffer, TextureHandle texture, uint32_t rowPitch,
                              bool upload, uint64_t offset)
{
    auto& g = *graph_.impl_;
    g.Scope(pass_);
    auto b = g.Resolve(pass_, buffer.graph, buffer.resource, buffer.version, false,
                       upload ? Use::CopySource : Use::CopyDestination)
                 .buffer;
    auto t = g.Resolve(pass_, texture.graph, texture.resource, texture.version, true,
                       upload ? Use::CopyDestination : Use::CopySource)
                 .texture;
    g.CheckBufferRange(pass_, buffer, offset, uint64_t(rowPitch) * t->desc.height);
    commands_.CopyTexture(std::move(b), std::move(t), rowPitch, upload, offset);
}
void CopyContext::CopyTexture(TextureHandle source, TextureHandle destination)
{
    auto& g = *graph_.impl_;
    g.Scope(pass_);
    auto src =
        g.Resolve(pass_, source.graph, source.resource, source.version, true, Use::CopySource)
            .texture;
    auto dst = g.Resolve(pass_, destination.graph, destination.resource, destination.version, true,
                         Use::CopyDestination)
                   .texture;
    commands_.CopyTexture(std::move(src), std::move(dst));
}
} // namespace renderlab::rendergraph
