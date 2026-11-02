#include "rhi/resources.hpp"

#include <algorithm>
#include <exception>
#include <format>
#include <stdexcept>

namespace renderlab::rhi
{
uint32_t FormatBytes(Format format)
{
    switch (format)
    {
    case Format::D32S8:
        return 8;
    case Format::Rgba32Float:
        return 16;
    case Format::Rgba8:
    case Format::Bgra8:
    case Format::Rgba8Srgb:
    case Format::Bgra8Srgb:
    case Format::D32:
    case Format::R32Float:
        return 4;
    }
    throw std::invalid_argument("Unknown texture format");
}
void AllocationLedger::Add(uint64_t bytes)
{
    ReserveBlock(bytes);
    AddAllocation(bytes);
}
void AllocationLedger::Remove(uint64_t bytes) noexcept
{
    RemoveAllocation(bytes);
    ReleaseBlock(bytes);
}
void AllocationLedger::ReserveBlock(uint64_t bytes)
{
    if (!bytes || stats.reserved > stats.budget || bytes > stats.budget - stats.reserved)
        throw std::runtime_error(std::format(
            "RHI backing budget exceeded: requested={} bytes, used={} bytes, limit={} bytes, "
            "reserved={} bytes",
            bytes, stats.used, stats.budget, stats.reserved));
    stats.reserved += bytes;
    ++stats.blocks;
    stats.reservedHighWater = std::max(stats.reservedHighWater, stats.reserved);
    stats.blockHighWater = std::max(stats.blockHighWater, stats.blocks);
}
void AllocationLedger::ReleaseBlock(uint64_t bytes) noexcept
{
    if (!bytes || !stats.blocks || bytes > stats.reserved || stats.reserved - bytes < stats.used)
        std::terminate();
    stats.reserved -= bytes;
    --stats.blocks;
}
void AllocationLedger::AddAllocation(uint64_t bytes)
{
    if (!bytes || stats.used > stats.reserved || bytes > stats.reserved - stats.used)
        throw std::runtime_error(std::format(
            "RHI allocation exceeds reserved backing: requested={} bytes, used={} bytes, "
            "reserved={} bytes",
            bytes, stats.used, stats.reserved));
    stats.used += bytes;
    ++stats.allocations;
    stats.highWater = std::max(stats.highWater, stats.used);
    stats.allocationHighWater = std::max(stats.allocationHighWater, stats.allocations);
}
void AllocationLedger::RemoveAllocation(uint64_t bytes) noexcept
{
    if (!bytes || !stats.allocations || bytes > stats.used)
        std::terminate();
    stats.used -= bytes;
    --stats.allocations;
}
} // namespace renderlab::rhi
