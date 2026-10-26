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
    return {buffer_, offset};
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
