#include "rendergraph/detail/graph_impl.hpp"

#include <algorithm>

namespace renderlab::rendergraph
{
using detail::Require;
rhi::State StateFor(Use use)
{
    switch (use)
    {
    case Use::Sampled:
        return rhi::State::Sampled;
    case Use::Uniform:
        return rhi::State::UniformRead;
    case Use::ShaderRead:
        return rhi::State::ShaderRead;
    case Use::Storage:
        return rhi::State::Storage;
    case Use::Color:
        return rhi::State::ColorAttachment;
    case Use::DepthRead:
    case Use::Depth:
        return rhi::State::DepthWrite;
    case Use::CopySource:
        return rhi::State::CopySource;
    case Use::CopyDestination:
        return rhi::State::CopyDest;
    case Use::Vertex:
        return rhi::State::Vertex;
    case Use::Index:
        return rhi::State::Index;
    case Use::Indirect:
        return rhi::State::Indirect;
    }
    throw std::invalid_argument("Unknown graph usage");
}
bool IsReadOnly(Use use)
{
    return use != Use::Storage && use != Use::Color && use != Use::Depth &&
           use != Use::CopyDestination;
}
void Graph::Impl::ValidateState(const detail::Resource& r, rhi::State state) const
{
    bool valid = false;
    if (r.isTexture)
    {
        valid = state == rhi::State::Undefined || state == rhi::State::CopySource ||
                state == rhi::State::CopyDest ||
                (state == rhi::State::Storage && r.textureDesc.storage) ||
                (state == rhi::State::Sampled && r.textureDesc.format != rhi::Format::D32) ||
                (state == rhi::State::ColorAttachment && r.textureDesc.attachment &&
                 r.textureDesc.format != rhi::Format::D32) ||
                (state == rhi::State::DepthWrite && r.textureDesc.attachment &&
                 r.textureDesc.format == rhi::Format::D32) ||
                (state == rhi::State::Present && r.imported);
    }
    else if (r.bufferDesc.memory == rhi::Memory::Upload)
        valid = state == rhi::State::HostWrite || state == rhi::State::CopySource ||
                state == rhi::State::UniformRead || state == rhi::State::ShaderRead;
    else if (r.bufferDesc.memory == rhi::Memory::Readback)
        valid = state == rhi::State::CopyDest || state == rhi::State::HostRead;
    else
        valid = state == rhi::State::Undefined || state == rhi::State::CopySource ||
                state == rhi::State::CopyDest || state == rhi::State::UniformRead ||
                state == rhi::State::ShaderRead || state == rhi::State::Vertex ||
                state == rhi::State::Index || state == rhi::State::Indirect ||
                (state == rhi::State::Storage && r.bufferDesc.storage);
    Require(valid, "Invalid import/export state for " + r.Name());
}
void Graph::Impl::ValidateUse(uint32_t index, const detail::Resource& r, Use use, bool write) const
{
    const auto& pass = passes[index];
    const auto suffix = " in " + pass.name + " (" + pass.source + ") on " + r.Name();
    Require(!write || !IsReadOnly(use), "Write to read-only use" + suffix);
    Require(write || (use != Use::CopyDestination && use != Use::Color && use != Use::Depth),
            "Read requires a read usage" + suffix);
    Require(pass.kind != PassKind::Copy || use == Use::CopySource || use == Use::CopyDestination,
            "Copy pass contains shader/raster access" + suffix);
    Require(pass.kind != PassKind::Compute ||
                (use != Use::Color && use != Use::Depth && use != Use::DepthRead &&
                 use != Use::Vertex && use != Use::Index && use != Use::Indirect),
            "Compute pass contains raster access" + suffix);
    if (r.isTexture)
    {
        Require(use == Use::Sampled || use == Use::Storage || use == Use::Color ||
                    use == Use::Depth || use == Use::DepthRead || use == Use::CopySource ||
                    use == Use::CopyDestination,
                "Buffer usage on texture" + suffix);
        Require(use != Use::Storage || r.textureDesc.storage,
                "Texture lacks storage usage" + suffix);
        Require(use != Use::Color ||
                    (r.textureDesc.attachment && r.textureDesc.format != rhi::Format::D32),
                "Invalid color attachment" + suffix);
        Require((use != Use::Depth && use != Use::DepthRead) ||
                    (r.textureDesc.attachment && r.textureDesc.format == rhi::Format::D32),
                "Invalid depth attachment" + suffix);
        Require(use != Use::Sampled || r.textureDesc.format != rhi::Format::D32,
                "Depth sampling is not implemented in current RHI" + suffix);
    }
    else
    {
        Require(use != Use::Sampled && use != Use::Color && use != Use::Depth &&
                    use != Use::DepthRead,
                "Texture usage on buffer" + suffix);
        Require(use != Use::Storage || r.bufferDesc.storage, "Buffer lacks storage usage" + suffix);
        Require(r.bufferDesc.memory != rhi::Memory::Readback ||
                    (write && use == Use::CopyDestination),
                "Readback buffer is a copy destination only" + suffix);
        Require(r.bufferDesc.memory != rhi::Memory::Upload ||
                    (!write &&
                     (use == Use::CopySource || use == Use::Uniform || use == Use::ShaderRead)),
                "Upload buffer is a GPU read source only" + suffix);
    }
}
void Graph::Impl::Scope(uint32_t pass) const
{
    Require(active == pass && phase == detail::Phase::Recording,
            "Resource context escaped its callback scope");
}
detail::Resource& Graph::Impl::Resolve(uint32_t pass, uint64_t graph, uint32_t resource,
                                       uint32_t version, bool texture, std::optional<Use> use)
{
    auto& r = Validate(graph, resource, version, texture);
    const auto& accesses = passes[pass].accesses;
    Require(std::ranges::any_of(accesses,
                                [&](const auto& a)
                                {
                                    return a.resource == resource &&
                                           ((a.read && a.input == version) ||
                                            (a.write && a.output == version)) &&
                                           (!use || a.use == *use);
                                }),
            "Undeclared resource/version/use in " + passes[pass].name + " (" + passes[pass].source +
                "): " + r.Name());
    return r;
}
void Graph::Impl::ValidateBindings(uint32_t pass, const rhi::BindingGroup& group)
{
    for (const auto& entry : group.layout->entries)
    {
        if (entry.kind == rhi::BindingKind::Sampler)
            continue;
        const auto found =
            std::ranges::find_if(group.bindings, [&](const auto& b)
                                 { return b.space == entry.space && b.slot == entry.slot; });
        Require(found != group.bindings.end(), "Incomplete binding group");
        const auto use = entry.kind == rhi::BindingKind::ConstantBuffer   ? Use::Uniform
                         : entry.kind == rhi::BindingKind::ReadOnlyBuffer ? Use::ShaderRead
                         : entry.kind == rhi::BindingKind::SampledTexture ? Use::Sampled
                                                                          : Use::Storage;
        const auto resource =
            std::ranges::find_if(resources,
                                 [&](auto& r)
                                 {
                                     return found->buffer
                                                ? r.buffer == found->buffer
                                                : found->view && r.texture == found->view->texture;
                                 });
        Require(resource != resources.end(),
                "Binding references a resource outside the graph in " + passes[pass].name);
        const auto index = static_cast<uint32_t>(resource - resources.begin());
        Require(std::ranges::any_of(passes[pass].accesses,
                                    [&](auto& a)
                                    {
                                        return a.resource == index && a.use == use &&
                                               ((entry.kind != rhi::BindingKind::StorageBuffer &&
                                                 entry.kind != rhi::BindingKind::StorageTexture) ||
                                                a.write);
                                    }),
                "Binding access was not declared in " + passes[pass].name + ": " +
                    resource->Name());
        if (found->buffer)
        {
            const auto& a =
                *std::ranges::find(passes[pass].accesses, index, &detail::Access::resource);
            Require(found->offset >= a.bufferRange.offset &&
                        found->offset - a.bufferRange.offset <= a.bufferRange.size &&
                        found->size <= a.bufferRange.size - (found->offset - a.bufferRange.offset),
                    "Binding exceeds declared buffer range in " + passes[pass].name);
        }
    }
}
void Graph::Impl::CheckBufferRange(uint32_t pass, BufferHandle handle, uint64_t offset,
                                   uint64_t size)
{
    Resolve(pass, handle.graph, handle.resource, handle.version, false);
    const auto& a =
        *std::ranges::find(passes[pass].accesses, handle.resource, &detail::Access::resource);
    Require(size && offset >= a.bufferRange.offset &&
                offset - a.bufferRange.offset <= a.bufferRange.size &&
                size <= a.bufferRange.size - (offset - a.bufferRange.offset),
            "Command exceeds declared buffer range in " + passes[pass].name);
}
uint64_t detail::EstimatedBytes(const Resource& r)
{
    return r.isTexture ? uint64_t(r.textureDesc.width) * r.textureDesc.height *
                             rhi::FormatBytes(r.textureDesc.format)
                       : r.bufferDesc.size;
}
} // namespace renderlab::rendergraph
