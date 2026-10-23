#pragma once

#include "rhi/resources.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace renderlab::rhi
{
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
struct BeginRendering
{
    std::shared_ptr<TextureView> color, depth;
    std::array<float, 4> clear;
};
struct EndRendering
{
};
struct SetPipeline
{
    std::shared_ptr<Pipeline> pipeline;
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
};
struct DrawIndirect
{
    std::shared_ptr<Buffer> arguments;
    uint64_t offset;
};
struct Dispatch
{
    uint32_t x, y, z;
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
};
using Command = std::variant<BufferBarrier, TextureBarrier, BufferCopy, TextureCopy, BeginRendering,
                             EndRendering, SetPipeline, SetBindings, SetGeometry, Draw,
                             DrawIndirect, Dispatch, Marker, Timestamp, ResolveTimestamps>;

/**
 * @brief Single-threaded, one-shot command recording. Packets own every referenced object.
 * Explicit barriers are never inferred; Storage->Storage emits a memory dependency.
 * Cancel releases uses immediately; Submit transfers recording to the completion queue.
 */
class CommandList final
{
public:
    explicit CommandList(const void* owner) : owner_(owner) {}
    void Barrier(std::shared_ptr<Buffer> buffer, State before, State after);
    void Barrier(std::shared_ptr<Texture> texture, State before, State after);
    void Copy(std::shared_ptr<Buffer> source, std::shared_ptr<Buffer> destination, uint64_t size,
              uint64_t sourceOffset = 0, uint64_t destinationOffset = 0);
    /// RGBA8/BGRA8 only; rowPitch multiple of 256, offset multiple of 512 on both backends.
    void CopyTexture(std::shared_ptr<Buffer> buffer, std::shared_ptr<Texture> texture,
                     uint32_t rowPitch, bool upload, uint64_t offset = 0);
    void Begin(std::shared_ptr<TextureView> color, std::shared_ptr<TextureView> depth = {},
               std::array<float, 4> clear = {0.02f, 0.02f, 0.02f, 1});
    void End();
    void Bind(std::shared_ptr<Pipeline> pipeline);
    void Bind(std::shared_ptr<BindingGroup> group);
    void Geometry(std::shared_ptr<Buffer> vertex, std::shared_ptr<Buffer> index = {});
    void DrawVertices(uint32_t count, uint32_t first = 0);
    void DrawIndexed(uint32_t count, uint32_t first = 0);
    void DrawFrom(std::shared_ptr<Buffer> arguments, uint64_t offset = 0);
    void Compute(uint32_t x, uint32_t y, uint32_t z = 1);
    void BeginMarker(std::string name);
    void EndMarker();
    void WriteTimestamp(uint32_t index);
    void ReadTimestamps(std::shared_ptr<Buffer> destination);
    void Cancel();
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
    std::vector<Command> commands_;
    bool consumed_ = false;
    bool rendering_ = false;
    bool compute_ = false;
    Format colorFormat_ = Format::Rgba8;
    bool hasDepth_ = false;
    uint32_t markers_ = 0;
    uint32_t timestampMask_ = 0;
    std::shared_ptr<Pipeline> pipeline_;
    std::shared_ptr<BindingGroup> group_;
    std::shared_ptr<Buffer> vertex_, index_;
};
} // namespace renderlab::rhi
