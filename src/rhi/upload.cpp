#include "rhi/upload.hpp"

#include "rhi/detail/validation.hpp"
#include "rhi/runtime.hpp"

#include <algorithm>
#include <cstring>

namespace renderlab::rhi
{
using detail::Range;
using detail::Require;

UploadArena::UploadArena(Runtime& runtime, uint64_t capacity)
    : buffer_(runtime.CreateBuffer({capacity, Memory::Upload, "Frame.UploadArena"}))
{
}
UploadArena::Slice UploadArena::Write(std::span<const std::byte> bytes, uint64_t alignment)
{
    Require(alignment && (alignment & (alignment - 1)) == 0, "Alignment must be a power of two");
    const auto offset = (cursor_ + alignment - 1) & ~(alignment - 1);
    Range(offset, bytes.size(), buffer_->desc.size);
    std::memcpy(buffer_->Map().data() + offset, bytes.data(), bytes.size());
    cursor_ = offset + bytes.size();
    highWater_ = std::max(cursor_, highWater_);
    return {buffer_, offset, bytes.size()};
}
UploadPool::UploadPool(Runtime& runtime, uint64_t bytesPerPage, size_t maxPages)
    : runtime_(runtime), bytesPerPage_(bytesPerPage), maxPages_(maxPages)
{
    Require(bytesPerPage && maxPages, "Upload pool bounds must be nonzero");
}
UploadPool::Batch::Batch(UploadPool& pool) : pool_(pool), beganAfter_(pool.runtime_.LastSubmitted())
{
    pool_.runtime_.Collect();
    ++pool_.batches_;
}
UploadPool::Batch::~Batch()
{
    for (const auto index : pages_)
        pool_.pages_[index]->active = false;
}
UploadArena::Slice UploadPool::Batch::Write(std::span<const std::byte> bytes, uint64_t alignment)
{
    Require(!sealed_, "Cannot write a sealed upload batch");
    Require(!bytes.empty() && bytes.size() <= pool_.bytesPerPage_, "Upload slice exceeds page");
    Require(alignment && (alignment & (alignment - 1)) == 0 && alignment <= pool_.bytesPerPage_,
            "Invalid upload alignment");
    const auto fits = [&](const Page& page)
    {
        const auto cursor = page.arena.cursor_;
        const auto padding = (alignment - cursor % alignment) % alignment;
        return padding <= pool_.bytesPerPage_ - cursor &&
               bytes.size() <= pool_.bytesPerPage_ - cursor - padding;
    };
    if (!pages_.empty() && fits(*pool_.pages_[pages_.back()]))
    {
        auto& arena = pool_.pages_[pages_.back()]->arena;
        const auto previous = arena.Used();
        auto slice = arena.Write(bytes, alignment);
        used_ += arena.Used() - previous;
        pool_.highWater_ = std::max(pool_.highWater_, used_);
        return slice;
    }
    size_t index = 0;
    for (; index < pool_.pages_.size(); ++index)
    {
        const auto& page = *pool_.pages_[index];
        // Arena is the only internal Buffer owner. Commands and CPU snapshots add references.
        if (!page.active && page.completion <= pool_.runtime_.Completed() &&
            page.arena.buffer_.use_count() == 1)
            break;
    }
    if (index == pool_.pages_.size())
    {
        Require(index < pool_.maxPages_, "Upload pool exhausted: release snapshots or retire work");
        pool_.pages_.push_back(
            std::make_unique<Page>(Page{UploadArena(pool_.runtime_, pool_.bytesPerPage_)}));
    }
    pages_.push_back(index);
    auto& page = *pool_.pages_[index];
    page.arena.Reset();
    page.active = true;
    auto slice = page.arena.Write(bytes, alignment);
    used_ += page.arena.Used();
    pool_.highWater_ = std::max(pool_.highWater_, used_);
    return slice;
}
void UploadPool::Batch::Seal(uint64_t completion)
{
    Require(!sealed_ && completion > beganAfter_ && completion <= pool_.runtime_.LastSubmitted(),
            "Upload batch requires a successful new submission");
    for (const auto index : pages_)
        pool_.pages_[index]->completion = completion;
    sealed_ = true;
}
UploadPool::Statistics UploadPool::Stats() const
{
    return {pages_.size(), pages_.size() * bytesPerPage_, highWater_, batches_};
}
UploadRing::UploadRing(Runtime& runtime, uint64_t bytesPerFrame) : runtime_(runtime)
{
    for (auto& segment : segments_)
        segment = std::make_unique<UploadArena>(runtime, bytesPerFrame);
}
UploadArena& UploadRing::BeginFrame()
{
    Require(!active_, "Upload frame already active");
    runtime_.Wait(completions_[index_]);
    segments_[index_]->Reset();
    active_ = true;
    return *segments_[index_];
}
void UploadRing::EndFrame(uint64_t completion)
{
    Require(active_ && completion > completions_[index_] && completion <= runtime_.LastSubmitted(),
            "Upload frame requires an actual new submission point");
    completions_[index_] = completion;
    active_ = false;
    index_ = (index_ + 1) % segments_.size();
}
uint32_t TextureRowPitch(uint32_t width, Format format)
{
    Require(width && width <= 16384, "Invalid texture row width");
    return (width * FormatBytes(format) + 255) & ~255u;
}
} // namespace renderlab::rhi
