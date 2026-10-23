#pragma once

#include "rhi/resources.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace renderlab::rhi
{
class Runtime;

/** @brief Bounded per-frame upload arena; reset only after its frame completion point. */
class UploadArena
{
public:
    struct Slice
    {
        std::shared_ptr<Buffer> buffer;
        uint64_t offset;
    };
    UploadArena(Runtime& runtime, uint64_t capacity);
    Slice Write(std::span<const std::byte> bytes, uint64_t alignment = 512);
    void Reset()
    {
        cursor_ = 0;
    }
    uint64_t Used() const
    {
        return cursor_;
    }
    uint64_t HighWater() const
    {
        return highWater_;
    }

private:
    std::shared_ptr<Buffer> buffer_;
    uint64_t cursor_ = 0, highWater_ = 0;
};

/** @brief Two upload segments rotated by CPU frame, each reused after its own submission retires.
 */
class UploadRing final
{
public:
    UploadRing(Runtime& runtime, uint64_t bytesPerFrame);
    UploadArena& BeginFrame();
    /// Seal with the successful submission point that consumed this segment.
    void EndFrame(uint64_t completion);

private:
    Runtime& runtime_;
    std::array<std::unique_ptr<UploadArena>, 2> segments_;
    std::array<uint64_t, 2> completions_{};
    size_t index_ = 0;
    bool active_ = false;
};

uint32_t TextureRowPitch(uint32_t width);
} // namespace renderlab::rhi
