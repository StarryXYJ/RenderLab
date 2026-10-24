#pragma once

#include <cstdint>
#include <stdexcept>

namespace renderlab::rhi::detail
{
inline void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::invalid_argument(message);
}
inline void Range(uint64_t offset, uint64_t size, uint64_t capacity)
{
    Require(size > 0 && offset <= capacity && size <= capacity - offset,
            "Buffer range out of bounds");
}
} // namespace renderlab::rhi::detail
