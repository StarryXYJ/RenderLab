#include "rhi/resources.hpp"

#include <algorithm>
#include <stdexcept>

namespace renderlab::rhi
{
uint32_t FormatBytes(Format format)
{
    switch (format)
    {
    case Format::Rgba32Float:
        return 16;
    case Format::Rgba8:
    case Format::Bgra8:
    case Format::Rgba8Srgb:
    case Format::Bgra8Srgb:
    case Format::D32:
        return 4;
    }
    throw std::invalid_argument("Unknown texture format");
}
void AllocationLedger::Add(uint64_t bytes)
{
    if (bytes > stats.budget - stats.used)
        throw std::runtime_error("RHI allocation budget exceeded");
    stats.used += bytes;
    stats.highWater = std::max(stats.highWater, stats.used);
}
void AllocationLedger::Remove(uint64_t bytes) noexcept
{
    stats.used -= bytes;
}
} // namespace renderlab::rhi
