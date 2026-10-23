#pragma once

#include "rhi/resources.hpp"

#include <cstdint>
#include <memory>

namespace renderlab::rhi
{
struct AcquiredImage
{
    std::shared_ptr<Texture> texture;
    uint32_t index;
};
class Swapchain : public Object
{
public:
    using Object::Object;
    /// Empty on out-of-date; caller retries after Resize. Zero dimensions suspend the viewport.
    virtual AcquiredImage Acquire() = 0;
    virtual void Present() = 0;
    /// Caller waits all graphics work and releases acquired images/views before Resize.
    virtual void Resize(uint32_t width, uint32_t height) = 0;
    virtual Format GetFormat() const = 0;
};
} // namespace renderlab::rhi
