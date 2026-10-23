#include "rendergraph/detail/graph_impl.hpp"

namespace renderlab::rendergraph
{
using detail::Require;
std::shared_ptr<rhi::Texture> PrepareContext::Texture(TextureHandle handle) const
{
    auto& g = *graph_.impl_;
    g.Scope(pass_, true);
    return g.Resolve(pass_, handle.graph, handle.resource, handle.version, true).texture;
}
std::shared_ptr<rhi::TextureView> PrepareContext::View(TextureHandle handle) const
{
    auto& g = *graph_.impl_;
    g.Scope(pass_, true);
    return g.Resolve(pass_, handle.graph, handle.resource, handle.version, true).view;
}
std::shared_ptr<rhi::Buffer> PrepareContext::Buffer(BufferHandle handle) const
{
    auto& g = *graph_.impl_;
    g.Scope(pass_, true);
    return g.Resolve(pass_, handle.graph, handle.resource, handle.version, false).buffer;
}
rhi::Runtime& PrepareContext::Device() const
{
    auto& g = *graph_.impl_;
    g.Scope(pass_, true);
    return g.pool->Device();
}
void ExecuteContext::Bind(std::shared_ptr<rhi::Pipeline> pipeline)
{
    auto& g = *graph_.impl_;
    g.Scope(pass_, false);
    Require(pipeline && ((g.passes[pass_].kind == PassKind::Compute && pipeline->desc.compute) ||
                         (g.passes[pass_].kind == PassKind::Raster && !pipeline->desc.compute)),
            "Pipeline kind does not match pass");
    commands_.Bind(std::move(pipeline));
}
void ExecuteContext::Bind(std::shared_ptr<rhi::BindingGroup> group)
{
    auto& g = *graph_.impl_;
    g.Scope(pass_, false);
    Require(bool(group), "Null binding group");
    g.ValidateBindings(pass_, *group);
    commands_.Bind(std::move(group));
}
void ExecuteContext::Geometry(BufferHandle vertices, BufferHandle indices)
{
    auto& g = *graph_.impl_;
    g.Scope(pass_, false);
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
void ExecuteContext::Draw(uint32_t count, uint32_t first)
{
    graph_.impl_->Scope(pass_, false);
    commands_.DrawVertices(count, first);
}
void ExecuteContext::DrawIndexed(uint32_t count, uint32_t first)
{
    graph_.impl_->Scope(pass_, false);
    commands_.DrawIndexed(count, first);
}
void ExecuteContext::DrawIndirect(BufferHandle arguments, uint64_t offset)
{
    auto& g = *graph_.impl_;
    g.Scope(pass_, false);
    g.CheckBufferRange(pass_, arguments, offset, sizeof(rhi::DrawArguments));
    commands_.DrawFrom(g.Resolve(pass_, arguments.graph, arguments.resource, arguments.version,
                                 false, Use::Indirect)
                           .buffer,
                       offset);
}
void ExecuteContext::Dispatch(uint32_t x, uint32_t y, uint32_t z)
{
    auto& g = *graph_.impl_;
    g.Scope(pass_, false);
    Require(g.passes[pass_].kind == PassKind::Compute, "Dispatch outside compute pass");
    commands_.Compute(x, y, z);
}
void ExecuteContext::Copy(BufferHandle source, BufferHandle destination, uint64_t size,
                          uint64_t sourceOffset, uint64_t destinationOffset)
{
    auto& g = *graph_.impl_;
    g.Scope(pass_, false);
    Require(g.passes[pass_].kind == PassKind::Copy, "Copy outside copy pass");
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
void ExecuteContext::CopyTexture(BufferHandle buffer, TextureHandle texture, uint32_t rowPitch,
                                 bool upload, uint64_t offset)
{
    auto& g = *graph_.impl_;
    g.Scope(pass_, false);
    Require(g.passes[pass_].kind == PassKind::Copy, "Texture copy outside copy pass");
    auto b = g.Resolve(pass_, buffer.graph, buffer.resource, buffer.version, false,
                       upload ? Use::CopySource : Use::CopyDestination)
                 .buffer;
    auto t = g.Resolve(pass_, texture.graph, texture.resource, texture.version, true,
                       upload ? Use::CopyDestination : Use::CopySource)
                 .texture;
    g.CheckBufferRange(pass_, buffer, offset, uint64_t(rowPitch) * t->desc.height);
    commands_.CopyTexture(std::move(b), std::move(t), rowPitch, upload, offset);
}
} // namespace renderlab::rendergraph
