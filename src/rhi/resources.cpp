#include "rhi/resources.hpp"

#include <algorithm>
#include <stdexcept>

namespace renderlab::rhi
{
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
