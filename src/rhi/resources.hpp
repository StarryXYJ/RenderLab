#pragma once

#include "rhi/render_state.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
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
    Rgba32Float,
    D32,
    D32S8,
    R32Float
};
/// @brief Uncompressed texel size used by upload/readback footprints.
uint32_t FormatBytes(Format format);
constexpr bool IsDepthFormat(Format format)
{
    return format == Format::D32 || format == Format::D32S8;
}
constexpr bool HasStencil(Format format)
{
    return format == Format::D32S8;
}
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
    ShaderRead,
    AccelerationStructureBuildInput,
    AccelerationStructureBuildWrite,
    AccelerationStructureRead,
    ShaderTableRead
};
enum class BindingKind
{
    StorageTexture,
    SampledTexture,
    Sampler,
    ConstantBuffer,
    ReadOnlyBuffer,
    StorageBuffer,
    AccelerationStructure
};
enum class ShaderStage
{
    Vertex,
    Pixel,
    Compute,
    RayGeneration,
    Miss,
    ClosestHit
};
enum class VertexFormat
{
    Float2,
    Float3,
    Float4
};
enum class LoadOp
{
    Clear,
    Load,
    Discard
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

/// Descriptor slots, not allocation bytes. Shared bookkeeping outlives the allocator's leases.
struct DescriptorStats
{
    uint64_t pages = 0, capacity = 0, inUse = 0, highWater = 0, creations = 0;
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
    bool deviceAddress = false;
    bool accelerationStructureInput = false;
    bool accelerationStructureStorage = false;
    bool shaderTable = false;
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
struct TextureView;
struct Texture : Object
{
    Texture(const void* owner, TextureDesc desc) : Object(owner), desc(std::move(desc)) {}
    const TextureDesc desc;
    /// Owner-thread cache of the immutable default view; weak to avoid a Texture/View cycle.
    std::weak_ptr<TextureView> defaultView;
    /// Depth sampling needs a distinct native view from the attachment view.
    std::weak_ptr<TextureView> sampledView;
};
enum class TextureViewUsage
{
    Default,
    Sampled
};
struct TextureView : Object
{
    TextureView(const std::shared_ptr<Texture>& texture, TextureViewUsage usage)
        : Object(texture->owner), texture(texture), usage(usage)
    {
    }
    const std::shared_ptr<Texture> texture;
    const TextureViewUsage usage;
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
    uint32_t count = 1;
};
struct BindingLayout : Object
{
    BindingLayout(const void* owner, std::vector<BindingDesc> entries)
        : Object(owner), entries(std::move(entries))
    {
    }
    const std::vector<BindingDesc> entries;
    uint32_t DescriptorCount() const
    {
        uint32_t count = 0;
        for (const auto& entry : entries)
            count += entry.count;
        return count;
    }
};
struct AccelerationStructure;
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
    std::shared_ptr<AccelerationStructure> accelerationStructure;
    uint32_t arrayElement = 0;
};
struct AccelerationStructureGeometry
{
    std::shared_ptr<Buffer> vertices;
    std::shared_ptr<Buffer> indices;
    uint64_t vertexOffset = 0, indexOffset = 0;
    uint32_t vertexCount = 0, indexCount = 0, vertexStride = 0;
};
struct AccelerationStructureInstance
{
    std::shared_ptr<AccelerationStructure> bottomLevel;
    /// Row-major 3x4 affine transform required by the native instance formats.
    std::array<float, 12> transform{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    uint32_t instanceId = 0, mask = 0xff;
};
struct AccelerationStructureDesc
{
    std::string name;
    bool topLevel = false;
    bool allowUpdate = false;
    std::vector<AccelerationStructureGeometry> geometries;
    std::vector<AccelerationStructureInstance> instances;
};
struct AccelerationStructureSizes
{
    uint64_t storage = 0, scratch = 0, updateScratch = 0;
};
struct AccelerationStructure : Object
{
    AccelerationStructure(const void* owner, AccelerationStructureDesc desc,
                          AccelerationStructureSizes sizes, std::shared_ptr<Buffer> storage,
                          std::shared_ptr<Buffer> scratch, std::shared_ptr<Buffer> instanceData)
        : Object(owner), desc(std::move(desc)), sizes(sizes), storage(std::move(storage)),
          scratch(std::move(scratch)), instanceData(std::move(instanceData))
    {
    }
    const AccelerationStructureDesc desc;
    const AccelerationStructureSizes sizes;
    const std::shared_ptr<Buffer> storage;
    /// May be released after submission; recorded build packets retain their scratch allocation.
    std::shared_ptr<Buffer> scratch;
    const std::shared_ptr<Buffer> instanceData;
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
    std::shared_ptr<ShaderModule> rayGeneration, miss, closestHit;
    uint32_t rayPayloadSize = 16, rayAttributeSize = 8, rayRecursionDepth = 1;
    std::vector<Format> colorFormats{Format::Rgba8};
    std::optional<Format> depthStencilFormat;
    RasterState raster;
    DepthState depth;
    BlendState blend;
    StencilState stencil;
    uint32_t vertexStride = 0;
    std::vector<VertexAttribute> attributes;
};
struct Pipeline : Object
{
    Pipeline(const void* owner, PipelineDesc desc) : Object(owner), desc(std::move(desc)) {}
    const PipelineDesc desc;
};

/** @brief Immutable three-record table for one generic triangle hit group. Retains its PSO. */
struct ShaderTable : Object
{
    ShaderTable(std::shared_ptr<Pipeline> pipeline, std::shared_ptr<Buffer> buffer, uint64_t offset,
                uint64_t stride)
        : Object(pipeline->owner), pipeline(std::move(pipeline)), buffer(std::move(buffer)),
          offset(offset), stride(stride)
    {
    }
    const std::shared_ptr<Pipeline> pipeline;
    const std::shared_ptr<Buffer> buffer;
    const uint64_t offset, stride;
};

/// Tightly packed D3D/Vulkan indirect ABIs. Counts are bounded by the recorded maximum.
struct DrawArguments
{
    uint32_t vertexCount, instanceCount, firstVertex, firstInstance;
};
static_assert(sizeof(DrawArguments) == 16);
struct DrawIndexedArguments
{
    uint32_t indexCount, instanceCount, firstIndex;
    int32_t vertexOffset;
    uint32_t firstInstance;
};
static_assert(sizeof(DrawIndexedArguments) == 20);
struct IndirectSupport
{
    uint32_t maxDrawCount = 1;
    bool countBuffer = false;
    bool firstInstance = false;
    /// Common shader object index: DXIL needs SV_StartInstanceLocation (SM 6.8).
    bool shaderInstanceIndex = false;
};
} // namespace renderlab::rhi
