#include "rhi/commands.hpp"

#include "rhi/detail/validation.hpp"

namespace renderlab::rhi
{
using detail::Range;
using detail::Require;

void CommandList::Recording() const
{
    Require(!consumed_, "Command list already consumed");
}
void CommandList::Check(const std::shared_ptr<Object>& object) const
{
    Recording();
    Require(object && object->owner == owner_, "Null object or object from another runtime");
}
void CommandList::Barrier(std::shared_ptr<Buffer> buffer, State before, State after)
{
    Check(buffer);
    Require(!rendering_, "Barriers must be outside rendering");
    const auto valid = [](State state)
    {
        return state == State::Undefined || state == State::CopySource ||
               state == State::CopyDest || state == State::Vertex || state == State::Index ||
               state == State::Indirect || state == State::HostWrite || state == State::HostRead ||
               state == State::UniformRead || state == State::ShaderRead || state == State::Storage;
    };
    Require(valid(before) && valid(after) && after != State::Undefined, "Invalid M1 buffer state");
    Require(after != State::Storage || buffer->desc.storage, "Buffer lacks storage usage");
    if (buffer->desc.memory == Memory::Upload)
        Require((before == State::HostWrite || before == State::CopySource ||
                 before == State::UniformRead || before == State::ShaderRead) &&
                    (after == State::CopySource || after == State::UniformRead ||
                     after == State::ShaderRead),
                "Upload buffers allow host-write to GPU-read or transitions between GPU-read uses");
    if (buffer->desc.memory == Memory::Readback)
        Require((before == State::CopyDest || before == State::HostRead) &&
                    (after == State::CopyDest || after == State::HostRead) &&
                    (before != after || after == State::CopyDest),
                "Readback buffers allow CopyDest/HostRead dependencies and repeated copies");
    commands_.emplace_back(BufferBarrier{std::move(buffer), before, after});
}
void CommandList::Barrier(std::shared_ptr<Texture> texture, State before, State after)
{
    Check(texture);
    Require(!rendering_ && after != State::Undefined, "Invalid texture barrier");
    const auto valid = [](State state)
    {
        return state == State::Undefined || state == State::CopySource ||
               state == State::CopyDest || state == State::Storage || state == State::Sampled ||
               state == State::ColorAttachment || state == State::DepthWrite ||
               state == State::Present;
    };
    Require(valid(before) && valid(after), "Invalid texture state");
    Require(after != State::Storage || texture->desc.storage, "Texture is not storage-capable");
    Require(after != State::ColorAttachment ||
                (texture->desc.attachment && texture->desc.format != Format::D32),
            "Texture is not a color attachment");
    Require(after != State::DepthWrite || texture->desc.format == Format::D32,
            "Texture is not a depth attachment");
    commands_.emplace_back(TextureBarrier{std::move(texture), before, after});
}
void CommandList::Copy(std::shared_ptr<Buffer> source, std::shared_ptr<Buffer> destination,
                       uint64_t size, uint64_t sourceOffset, uint64_t destinationOffset)
{
    Check(source);
    Check(destination);
    Require(!rendering_ && source != destination, "Invalid buffer copy");
    Range(sourceOffset, size, source->desc.size);
    Range(destinationOffset, size, destination->desc.size);
    commands_.emplace_back(BufferCopy{std::move(source), std::move(destination), sourceOffset,
                                      destinationOffset, size});
}
void CommandList::CopyTexture(std::shared_ptr<Buffer> buffer, std::shared_ptr<Texture> texture,
                              uint32_t rowPitch, bool upload, uint64_t offset)
{
    Check(buffer);
    Check(texture);
    Require(!rendering_ && texture->desc.format != Format::D32, "Invalid texture copy");
    Require(rowPitch >= uint64_t(texture->desc.width) * 4 && rowPitch % 256 == 0 &&
                offset % 512 == 0,
            "Texture copy pitch/offset alignment invalid");
    Range(offset, uint64_t(rowPitch) * texture->desc.height, buffer->desc.size);
    commands_.emplace_back(
        TextureCopy{std::move(buffer), std::move(texture), offset, rowPitch, upload});
}
void CommandList::CopyTexture(std::shared_ptr<Texture> source, std::shared_ptr<Texture> destination)
{
    Check(source);
    Check(destination);
    Require(!rendering_ && source != destination, "Invalid texture-to-texture copy");
    const auto& a = source->desc;
    const auto& b = destination->desc;
    Require(a.format != Format::D32 && a.format == b.format && a.width == b.width &&
                a.height == b.height,
            "Texture copy dimensions/format mismatch");
    commands_.emplace_back(TextureToTextureCopy{std::move(source), std::move(destination)});
}
void CommandList::Begin(std::shared_ptr<TextureView> color, std::shared_ptr<TextureView> depth,
                        std::array<float, 4> clear, LoadOp colorLoad, LoadOp depthLoad)
{
    Check(color);
    Require(!rendering_ && color->texture->desc.attachment &&
                color->texture->desc.format != Format::D32,
            "Invalid color attachment");
    if (depth)
    {
        Check(depth);
        Require(depth->texture->desc.format == Format::D32 &&
                    depth->texture->desc.width == color->texture->desc.width &&
                    depth->texture->desc.height == color->texture->desc.height,
                "Depth attachment mismatch");
    }
    rendering_ = true;
    colorFormat_ = color->texture->desc.format;
    hasDepth_ = bool(depth);
    commands_.emplace_back(
        BeginRendering{std::move(color), std::move(depth), clear, colorLoad, depthLoad});
}
void CommandList::End()
{
    Recording();
    Require(rendering_, "No active rendering");
    rendering_ = false;
    commands_.emplace_back(EndRendering{});
}
void CommandList::Bind(std::shared_ptr<Pipeline> pipeline)
{
    Check(pipeline);
    pipeline_ = pipeline;
    compute_ = bool(pipeline->desc.compute);
    group_.reset();
    vertex_.reset();
    index_.reset();
    commands_.emplace_back(SetPipeline{std::move(pipeline)});
}
void CommandList::Bind(std::shared_ptr<BindingGroup> group)
{
    Check(group);
    Require(pipeline_ && pipeline_->desc.layout == group->layout, "Pipeline/group layout mismatch");
    group_ = group;
    commands_.emplace_back(SetBindings{std::move(group)});
}
void CommandList::Geometry(std::shared_ptr<Buffer> vertex, std::shared_ptr<Buffer> index)
{
    Check(vertex);
    Require(pipeline_ && !compute_, "Bind a graphics pipeline before geometry");
    if (index)
        Check(index);
    vertex_ = vertex;
    index_ = index;
    commands_.emplace_back(SetGeometry{std::move(vertex), std::move(index)});
}
void CommandList::DrawVertices(uint32_t count, uint32_t first)
{
    DrawReady(false);
    Range(uint64_t(first) * pipeline_->desc.vertexStride,
          uint64_t(count) * pipeline_->desc.vertexStride, vertex_->desc.size);
    commands_.emplace_back(Draw{count, first, false});
}
void CommandList::DrawIndexed(uint32_t count, uint32_t first)
{
    DrawReady(true);
    Range(uint64_t(first) * 4, uint64_t(count) * 4, index_->desc.size);
    commands_.emplace_back(Draw{count, first, true});
}
void CommandList::DrawFrom(std::shared_ptr<Buffer> arguments, uint64_t offset)
{
    Check(arguments);
    DrawReady(false);
    Require(offset % 4 == 0, "Indirect offset must be 4-byte aligned");
    Range(offset, sizeof(DrawArguments), arguments->desc.size);
    commands_.emplace_back(DrawIndirect{std::move(arguments), offset});
}
void CommandList::DrawReady(bool indexed) const
{
    Recording();
    Require(rendering_ && pipeline_ && !compute_ && group_ && vertex_ && (!indexed || index_),
            "Draw state incomplete");
    Require(pipeline_->desc.color == colorFormat_ && pipeline_->desc.depth == hasDepth_,
            "Pipeline attachment formats do not match rendering");
}
void CommandList::Compute(uint32_t x, uint32_t y, uint32_t z)
{
    Recording();
    Require(!rendering_ && pipeline_ && compute_ && group_ && x && y && z && x <= 65535 &&
                y <= 65535 && z <= 65535,
            "Invalid dispatch state or dimensions");
    commands_.emplace_back(Dispatch{x, y, z});
}
void CommandList::BeginMarker(std::string name)
{
    Recording();
    ++markers_;
    commands_.emplace_back(Marker{std::move(name), true});
}
void CommandList::EndMarker()
{
    Recording();
    Require(markers_ > 0, "Unbalanced marker");
    --markers_;
    commands_.emplace_back(Marker{{}, false});
}
void CommandList::WriteTimestamp(uint32_t index)
{
    Recording();
    Require(index < 2, "M1 supports two timestamp slots per command list");
    Require((timestampMask_ & (1u << index)) == 0, "Timestamp slot already written");
    timestampMask_ |= 1u << index;
    commands_.emplace_back(Timestamp{index});
}
void CommandList::ReadTimestamps(std::shared_ptr<Buffer> destination)
{
    Check(destination);
    Require(!rendering_, "Resolve timestamps outside rendering");
    Require(timestampMask_ == 3, "Both timestamps must be written before resolve");
    Require(destination->desc.memory == Memory::Readback && destination->desc.size >= 16,
            "Timestamp destination requires 16 readback bytes");
    commands_.emplace_back(ResolveTimestamps{std::move(destination)});
}
void CommandList::Cancel()
{
    Recording();
    consumed_ = true;
    commands_.clear();
    pipeline_.reset();
    group_.reset();
    vertex_.reset();
    index_.reset();
}
} // namespace renderlab::rhi
