#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace renderlab::rhi
{
enum class Memory
{
    Device,
    Upload,
    Readback
};
enum class Format
{
    Rgba8,
    Bgra8,
    Rgba8Srgb,
    Bgra8Srgb,
    D32
};
enum class State
{
    Undefined,
    CopySource,
    CopyDest,
    Vertex,
    Index,
    Indirect,
    Storage,
    Sampled,
    ColorAttachment,
    DepthWrite,
    Present,
    HostRead,
    HostWrite,
    UniformRead,
    ShaderRead
};
enum class BindingKind
{
    StorageTexture,
    SampledTexture,
    Sampler,
    ConstantBuffer,
    ReadOnlyBuffer,
    StorageBuffer
};
enum class ShaderStage
{
    Vertex,
    Pixel,
    Compute
};
enum class VertexFormat
{
    Float2,
    Float3,
    Float4
};
struct VertexAttribute
{
    std::string semantic;
    uint32_t location = 0;
    VertexFormat format = VertexFormat::Float3;
    uint32_t offset = 0;
    bool operator==(const VertexAttribute&) const = default;
};

struct MemoryStats
{
    uint64_t used = 0;
    uint64_t highWater = 0;
    uint64_t budget = 256ull * 1024 * 1024;
};

/** @brief Dedicated allocations in M1; budget counts native allocation sizes, not requested sizes.
 */
struct AllocationLedger
{
    MemoryStats stats;
    void Add(uint64_t bytes);
    void Remove(uint64_t bytes) noexcept;
};

/** @brief GPU objects must be released before their Runtime and Device. Commands retain uses. */
struct Object
{
    explicit Object(const void* owner) : owner(owner) {}
    virtual ~Object() = default;
    const void* const owner;
};
struct BufferDesc
{
    uint64_t size = 0;
    Memory memory = Memory::Device;
    std::string name;
    bool storage = false;
};
struct TextureDesc
{
    uint32_t width = 0;
    uint32_t height = 0;
    Format format = Format::Rgba8;
    bool storage = false;
    bool attachment = false;
    std::string name;
};
struct Buffer : Object
{
    Buffer(const void* owner, BufferDesc desc) : Object(owner), desc(std::move(desc)) {}
    const BufferDesc desc;
    /// Upload/Readback mapping only. Readback requires a completed submission and HostRead barrier.
    virtual std::span<std::byte> Map() = 0;
};
struct Texture : Object
{
    Texture(const void* owner, TextureDesc desc) : Object(owner), desc(std::move(desc)) {}
    const TextureDesc desc;
};
struct TextureView : Object
{
    TextureView(const std::shared_ptr<Texture>& texture) : Object(texture->owner), texture(texture)
    {
    }
    const std::shared_ptr<Texture> texture;
};
struct Sampler : Object
{
    using Object::Object;
};
struct BindingDesc
{
    uint32_t slot;
    BindingKind kind;
    uint32_t space = 0;
};
struct BindingLayout : Object
{
    BindingLayout(const void* owner, std::vector<BindingDesc> entries)
        : Object(owner), entries(std::move(entries))
    {
    }
    const std::vector<BindingDesc> entries;
};
struct Binding
{
    uint32_t slot;
    std::shared_ptr<TextureView> view;
    std::shared_ptr<Sampler> sampler;
    std::shared_ptr<Buffer> buffer;
    uint64_t offset = 0;
    uint64_t size = 0;
    uint32_t stride = 0;
    uint32_t space = 0;
};
struct BindingGroup : Object
{
    BindingGroup(std::shared_ptr<BindingLayout> layout, std::vector<Binding> bindings)
        : Object(layout->owner), layout(std::move(layout)), bindings(std::move(bindings))
    {
    }
    const std::shared_ptr<BindingLayout> layout;
    const std::vector<Binding> bindings;
};
struct ShaderModule : Object
{
    ShaderModule(const void* owner, ShaderStage stage, std::vector<std::byte> code,
                 std::string entry)
        : Object(owner), stage(stage), code(std::move(code)), entry(std::move(entry))
    {
    }
    const ShaderStage stage;
    const std::vector<std::byte> code;
    const std::string entry;
};
struct PipelineDesc
{
    std::shared_ptr<BindingLayout> layout;
    std::shared_ptr<ShaderModule> vertex;
    std::shared_ptr<ShaderModule> pixel;
    std::shared_ptr<ShaderModule> compute;
    Format color = Format::Rgba8;
    bool depth = false;
    bool blend = false;
    bool cull = true;
    uint32_t vertexStride = 0;
    std::vector<VertexAttribute> attributes;
};
struct Pipeline : Object
{
    Pipeline(const void* owner, PipelineDesc desc) : Object(owner), desc(std::move(desc)) {}
    const PipelineDesc desc;
};

/// Common D3D/Vulkan non-indexed indirect ABI; one command, no count buffer in M1.
struct DrawArguments
{
    uint32_t vertexCount, instanceCount, firstVertex, firstInstance;
};
static_assert(sizeof(DrawArguments) == 16);
} // namespace renderlab::rhi
