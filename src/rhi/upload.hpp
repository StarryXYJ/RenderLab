#pragma once

#include "rhi/resources.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

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
        uint64_t size = 0;
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
    friend class UploadPool;
    std::shared_ptr<Buffer> buffer_;
    uint64_t cursor_ = 0, highWater_ = 0;
};

/** @brief Owner-thread bounded upload pages. CPU snapshots and commands pin whole pages.
 * Runtime outlives the pool; a batch must be destroyed before its pool.
 */
class UploadPool final
{
public:
    struct Statistics
    {
        uint64_t pages = 0, reservedBytes = 0, highWaterBytes = 0, batches = 0;
    };
    class Batch final
    {
    public:
        explicit Batch(UploadPool& pool);
        ~Batch();
        Batch(const Batch&) = delete;
        Batch& operator=(const Batch&) = delete;
        /// Write all slices before recording; returned Buffer references pin their whole page.
        UploadArena::Slice Write(std::span<const std::byte> bytes, uint64_t alignment = 256);
        /// Seal once with the successful submission consuming this batch. Destruction cancels.
        void Seal(uint64_t completion);
        uint64_t BytesWritten() const
        {
            return written_;
        }

    private:
        UploadPool& pool_;
        std::vector<size_t> pages_;
        uint64_t beganAfter_, used_ = 0, written_ = 0;
        bool sealed_ = false;
    };
    explicit UploadPool(Runtime& runtime, uint64_t bytesPerPage = 65536, size_t maxPages = 64);
    UploadPool(const UploadPool&) = delete;
    UploadPool& operator=(const UploadPool&) = delete;
    Batch Begin()
    {
        return Batch(*this);
    }
    Statistics Stats() const;

private:
    struct Page
    {
        UploadArena arena;
        uint64_t completion = 0;
        bool active = false;
    };
    Runtime& runtime_;
    uint64_t bytesPerPage_;
    size_t maxPages_;
    uint64_t highWater_ = 0, batches_ = 0;
    std::vector<std::unique_ptr<Page>> pages_;
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

uint32_t TextureRowPitch(uint32_t width, Format format = Format::Rgba8);
} // namespace renderlab::rhi
