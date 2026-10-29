#include "rendergraph/pool.hpp"

#include <algorithm>
#include <stdexcept>

namespace renderlab::rendergraph
{
namespace
{
bool Compatible(const rhi::TextureDesc& a, const rhi::TextureDesc& b)
{
    return a.width == b.width && a.height == b.height && a.format == b.format &&
           a.storage == b.storage && a.attachment == b.attachment;
}
} // namespace
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
bool Pool::Available(const std::shared_ptr<detail::AliasGroup>& group) const
{
    return group.use_count() == 1 && std::ranges::all_of(group->members, [&](const auto& member)
                                                         { return Available(member); });
}
void Pool::MakeRoom(uint64_t bytes, size_t resources)
{
    if (bytes > maxBytes_ || resources > maxResources_)
        throw std::runtime_error("Graph resource exceeds pool byte limit");
    const auto full = [&]
    {
        return Stats().cached > maxResources_ - resources ||
               stats_.estimatedBytes > maxBytes_ - bytes;
    };
    for (auto it = entries_.begin(); it != entries_.end() && full();)
        if (Available(*it))
        {
            stats_.estimatedBytes -= (*it)->estimatedBytes;
            it = entries_.erase(it);
        }
        else
            ++it;
    for (auto it = aliasGroups_.begin(); it != aliasGroups_.end() && full();)
        if (Available(*it))
        {
            stats_.estimatedBytes -= (*it)->bytes;
            it = aliasGroups_.erase(it);
        }
        else
            ++it;
    if (full())
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
            if (Compatible(key, desc))
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
    std::erase_if(aliasGroups_,
                  [&](const auto& group)
                  {
                      if (!Available(group))
                          return false;
                      stats_.estimatedBytes -= group->bytes;
                      return true;
                  });
}
PoolStats Pool::Stats() const
{
    CheckOwner();
    auto result = stats_;
    result.cached = entries_.size();
    for (const auto& group : aliasGroups_)
        result.cached += group->members.size();
    return result;
}
std::shared_ptr<detail::AliasGroup> Pool::AcquireAliases(std::span<const rhi::TextureDesc> descs)
{
    CheckOwner();
    runtime_.Collect();
    for (auto& group : aliasGroups_)
        if (Available(group) && group->members.size() == descs.size())
        {
            bool matches = true;
            for (size_t i = 0; i < descs.size(); ++i)
                matches &= Compatible(group->members[i]->texture->desc, descs[i]);
            if (matches)
            {
                stats_.reuses += descs.size();
                return group;
            }
        }
    const auto bytes = runtime_.QueryAliasedTextureBytes(descs);
    MakeRoom(bytes, descs.size());
    auto group = std::make_shared<detail::AliasGroup>();
    const auto textures = runtime_.CreateAliasedTextures(descs);
    group->bytes = bytes;
    for (const auto& texture : textures)
    {
        auto entry = std::make_shared<detail::Allocation>();
        entry->texture = texture;
        group->members.push_back(std::move(entry));
    }
    aliasGroups_.push_back(group);
    stats_.allocations += descs.size();
    stats_.estimatedBytes += bytes;
    return group;
}
} // namespace renderlab::rendergraph
