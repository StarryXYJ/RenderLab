#include "rendergraph/pool.hpp"

#include <algorithm>
#include <stdexcept>

namespace renderlab::rendergraph
{
Pool::Pool(rhi::Runtime& runtime, size_t maxResources, uint64_t maxEstimatedBytes)
    : runtime_(runtime), maxResources_(maxResources), maxBytes_(maxEstimatedBytes)
{
    if (!maxResources || !maxEstimatedBytes)
        throw std::invalid_argument("Graph pool bounds must be nonzero");
}
bool Pool::Available(const std::shared_ptr<detail::Allocation>& entry) const
{
    if (entry.use_count() != 1 || entry->completion > runtime_.Completed())
        return false;
    if (!entry->texture)
        return entry->buffer.use_count() == 1;
    // Allocation and its default View own the Texture; external views/commands must be absent.
    return entry->texture.use_count() == (entry->view ? 2 : 1) &&
           (!entry->view || entry->view.use_count() == 1);
}
void Pool::CheckOwner() const
{
    if (std::this_thread::get_id() != owner_)
        throw std::logic_error("Graph pool/preparation/submission requires owner thread");
}
void Pool::MakeRoom(uint64_t bytes)
{
    if (bytes > maxBytes_)
        throw std::runtime_error("Graph resource exceeds pool byte limit");
    for (auto it = entries_.begin();
         it != entries_.end() &&
         (entries_.size() >= maxResources_ || stats_.estimatedBytes > maxBytes_ - bytes);)
        if (Available(*it))
        {
            stats_.estimatedBytes -= (*it)->estimatedBytes;
            it = entries_.erase(it);
        }
        else
            ++it;
    if (entries_.size() >= maxResources_ || stats_.estimatedBytes > maxBytes_ - bytes)
        throw std::runtime_error(
            "Graph pool is full; release frame leases and wait/collect completed work");
}
std::shared_ptr<detail::Allocation> Pool::Acquire(const rhi::TextureDesc& desc)
{
    CheckOwner();
    runtime_.Collect();
    for (auto& entry : entries_)
        if (Available(entry) && entry->texture)
        {
            const auto& key = entry->texture->desc;
            if (key.width == desc.width && key.height == desc.height && key.format == desc.format &&
                key.storage == desc.storage && key.attachment == desc.attachment)
            {
                ++stats_.reuses;
                return entry;
            }
        }
    const auto bytes = uint64_t(desc.width) * desc.height * rhi::FormatBytes(desc.format);
    MakeRoom(bytes);
    auto entry = std::make_shared<detail::Allocation>();
    entry->texture = runtime_.CreateTexture(desc);
    entry->estimatedBytes = bytes;
    entries_.push_back(entry);
    ++stats_.allocations;
    stats_.estimatedBytes += bytes;
    return entry;
}
std::shared_ptr<detail::Allocation> Pool::Acquire(const rhi::BufferDesc& desc)
{
    CheckOwner();
    runtime_.Collect();
    for (auto& entry : entries_)
        if (Available(entry) && entry->buffer)
        {
            const auto& key = entry->buffer->desc;
            if (key.size == desc.size && key.memory == desc.memory && key.storage == desc.storage &&
                key.deviceAddress == desc.deviceAddress &&
                key.accelerationStructureInput == desc.accelerationStructureInput &&
                key.accelerationStructureStorage == desc.accelerationStructureStorage &&
                key.shaderTable == desc.shaderTable)
            {
                ++stats_.reuses;
                return entry;
            }
        }
    MakeRoom(desc.size);
    auto entry = std::make_shared<detail::Allocation>();
    entry->buffer = runtime_.CreateBuffer(desc);
    entry->state = desc.memory == rhi::Memory::Readback ? rhi::State::CopyDest
                   : desc.memory == rhi::Memory::Upload ? rhi::State::HostWrite
                                                        : rhi::State::Undefined;
    entry->estimatedBytes = desc.size;
    entries_.push_back(entry);
    ++stats_.allocations;
    stats_.estimatedBytes += desc.size;
    return entry;
}
void Pool::Trim()
{
    CheckOwner();
    runtime_.Collect();
    std::erase_if(entries_,
                  [&](const auto& entry)
                  {
                      if (!Available(entry))
                          return false;
                      stats_.estimatedBytes -= entry->estimatedBytes;
                      return true;
                  });
}
PoolStats Pool::Stats() const
{
    CheckOwner();
    auto result = stats_;
    result.cached = entries_.size();
    return result;
}
} // namespace renderlab::rendergraph
