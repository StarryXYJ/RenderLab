#pragma once

#include "rhi/resources.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace renderlab::rhi
{
inline constexpr uint32_t MaxTimestampSlots = 64;
struct BufferBarrier
{
    std::shared_ptr<Buffer> buffer;
    State before, after;
};
struct TextureBarrier
{
    std::shared_ptr<Texture> texture;
    State before, after;
};
struct BufferCopy
{
    std::shared_ptr<Buffer> source, destination;
    uint64_t sourceOffset, destinationOffset, size;
};
struct TextureCopy
{
    std::shared_ptr<Buffer> buffer;
    std::shared_ptr<Texture> texture;
    uint64_t offset;
    uint32_t rowPitch;
    bool upload;
};
struct TextureToTextureCopy
{
    std::shared_ptr<Texture> source, destination;
};
struct BeginRendering
{
    std::vector<std::shared_ptr<TextureView>> colors;
    std::shared_ptr<TextureView> depth;
    std::array<float, 4> clear;
    LoadOp colorLoad = LoadOp::Clear, depthLoad = LoadOp::Clear, stencilLoad = LoadOp::Clear;
    uint32_t clearStencil = 0;
};
struct EndRendering
{
};
struct SetPipeline
{
    std::shared_ptr<Pipeline> pipeline;
};
struct SetStencilReference
{
    uint32_t reference;
};
struct SetBindings
{
    std::shared_ptr<BindingGroup> group;
};
struct SetGeometry
{
    std::shared_ptr<Buffer> vertex, index;
};
struct Draw
{
    uint32_t count, first;
    bool indexed;
    uint32_t instances = 1, firstInstance = 0;
    int32_t vertexOffset = 0;
};
struct DrawIndirect
{
    std::shared_ptr<Buffer> arguments;
    uint64_t offset;
    uint32_t maxCount = 1;
    bool indexed = false;
    std::shared_ptr<Buffer> countBuffer;
    uint64_t countOffset = 0;
};
struct Dispatch
{
    uint32_t x, y, z;
};
struct TraceRays
{
    std::shared_ptr<ShaderTable> table;
    uint32_t width, height, depth;
};
struct BuildAccelerationStructure
{
    std::shared_ptr<AccelerationStructure> accelerationStructure;
    std::shared_ptr<Buffer> scratch;
    bool update;
};
struct Marker
{
    std::string name;
    bool begin;
};
struct Timestamp
{
    uint32_t index;
};
struct ResolveTimestamps
{
    std::shared_ptr<Buffer> destination;
    uint32_t count;
};
using Command =
    std::variant<BufferBarrier, TextureBarrier, BufferCopy, TextureCopy, TextureToTextureCopy,
                 BeginRendering, EndRendering, SetPipeline, SetStencilReference, SetBindings,
                 SetGeometry, Draw, DrawIndirect, Dispatch, TraceRays, BuildAccelerationStructure,
                 Marker, Timestamp, ResolveTimestamps>;

/**
 * @brief Single-threaded, one-shot command recording. Packets own every referenced object.
 * Explicit barriers are never inferred; Storage->Storage emits a memory dependency.
 * Cancel releases uses immediately; Submit transfers recording to the completion queue.
 */
class CommandList final
{
public:
    explicit CommandList(const void* owner, IndirectSupport indirect = {})
        : owner_(owner), indirect_(indirect)
    {
    }
    void Barrier(std::shared_ptr<Buffer> buffer, State before, State after);
    void Barrier(std::shared_ptr<Texture> texture, State before, State after);
    void Copy(std::shared_ptr<Buffer> source, std::shared_ptr<Buffer> destination, uint64_t size,
              uint64_t sourceOffset = 0, uint64_t destinationOffset = 0);
    /// Color formats only; rowPitch multiple of 256, offset multiple of 512 on both backends.
    void CopyTexture(std::shared_ptr<Buffer> buffer, std::shared_ptr<Texture> texture,
                     uint32_t rowPitch, bool upload, uint64_t offset = 0);
    void CopyTexture(std::shared_ptr<Texture> source, std::shared_ptr<Texture> destination);
    void Begin(std::vector<std::shared_ptr<TextureView>> colors,
               std::shared_ptr<TextureView> depth = {},
               std::array<float, 4> clear = {0.02f, 0.02f, 0.02f, 1},
               LoadOp colorLoad = LoadOp::Clear, LoadOp depthLoad = LoadOp::Clear,
               LoadOp stencilLoad = LoadOp::Clear, uint32_t clearStencil = 0);
    void Begin(std::shared_ptr<TextureView> color, std::shared_ptr<TextureView> depth = {},
               std::array<float, 4> clear = {0.02f, 0.02f, 0.02f, 1},
               LoadOp colorLoad = LoadOp::Clear, LoadOp depthLoad = LoadOp::Clear,
               LoadOp stencilLoad = LoadOp::Clear, uint32_t clearStencil = 0);
    void End();
    void StencilReference(uint32_t reference);
    void Bind(std::shared_ptr<Pipeline> pipeline);
    void Bind(std::shared_ptr<BindingGroup> group);
    void Geometry(std::shared_ptr<Buffer> vertex, std::shared_ptr<Buffer> index = {});
    void DrawVertices(uint32_t count, uint32_t first = 0, uint32_t instances = 1,
                      uint32_t firstInstance = 0);
    void DrawIndexed(uint32_t count, uint32_t first = 0, uint32_t instances = 1,
                     uint32_t firstInstance = 0, int32_t vertexOffset = 0);
    /// Tightly packed arguments; GPU count is clamped to maxCount. No CPU count readback.
    void DrawFrom(std::shared_ptr<Buffer> arguments, uint64_t offset = 0, uint32_t maxCount = 1,
                  bool indexed = false, std::shared_ptr<Buffer> countBuffer = {},
                  uint64_t countOffset = 0);
    void Compute(uint32_t x, uint32_t y, uint32_t z = 1);
    void Trace(std::shared_ptr<ShaderTable> table, uint32_t width, uint32_t height,
               uint32_t depth = 1);
    void Build(std::shared_ptr<AccelerationStructure> accelerationStructure, bool update = false);
    void BeginMarker(std::string name);
    void EndMarker();
    void WriteTimestamp(uint32_t index);
    void ReadTimestamps(std::shared_ptr<Buffer> destination, uint32_t count = 2);
    void Cancel();
    /// Closed, self-contained pass boundary; subsequent draws must rebind their state.
    void RecordingBoundary();
    void SetRecordingThreads(uint32_t count);
    /// Immutable packet ranges, split only at explicit closed boundaries, in submission order.
    std::vector<std::pair<size_t, size_t>> RecordingRanges() const;
    const std::vector<Command>& GetCommands() const
    {
        return commands_;
    }

private:
    friend class Runtime;
    void Check(const std::shared_ptr<Object>& object) const;
    void Recording() const;
    void DrawReady(bool indexed) const;
    const void* owner_;
    IndirectSupport indirect_;
    std::vector<Command> commands_;
    std::vector<size_t> recordingEnds_;
    uint32_t recordingThreads_ = 1;
    bool consumed_ = false;
    bool rendering_ = false;
    bool compute_ = false;
    std::vector<Format> colorFormats_;
    std::optional<Format> depthStencilFormat_;
    uint32_t markers_ = 0;
    uint64_t timestampMask_ = 0;
    std::shared_ptr<Pipeline> pipeline_;
    std::shared_ptr<BindingGroup> group_;
    std::shared_ptr<Buffer> vertex_, index_;
};
} // namespace renderlab::rhi
