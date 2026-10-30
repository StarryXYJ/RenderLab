#pragma once

#include "rendergraph/command_statistics.hpp"
#include "rhi/resources.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace renderlab::rhi
{
class Runtime;
}
namespace renderlab::rendergraph
{
struct TextureTag;
struct BufferTag;
template <class Tag> struct Handle
{
    uint64_t graph = 0;
    uint32_t resource = UINT32_MAX, version = 0;
    explicit operator bool() const
    {
        return graph && resource != UINT32_MAX;
    }
    bool operator==(const Handle&) const = default;
};
using TextureHandle = Handle<TextureTag>;
using BufferHandle = Handle<BufferTag>;
struct PassHandle
{
    uint64_t graph = 0;
    uint32_t index = UINT32_MAX;
    bool operator==(const PassHandle&) const = default;
};
enum class PassKind
{
    Raster,
    Compute,
    Copy,
    RayTracing
};
enum class PassCategory
{
    Other,
    Opaque,
    Shadow,
    Transparent,
    Fullscreen
};
enum class Use
{
    Sampled,
    Uniform,
    ShaderRead,
    Storage,
    Color,
    Depth,
    DepthRead,
    CopySource,
    CopyDestination,
    Vertex,
    Index,
    Indirect,
    AccelerationStructureBuildInput,
    AccelerationStructureBuildWrite,
    AccelerationStructureRead,
    ShaderTableRead
};
enum class AttachmentAccess
{
    ReadWrite,
    ReadOnly
};
/** @brief Per-aspect initialization and permissions; combined resources are tracked conservatively.
 */
struct AttachmentAspect
{
    rhi::LoadOp load = rhi::LoadOp::Clear;
    AttachmentAccess access = AttachmentAccess::ReadWrite;
};
enum class Coverage
{
    Full,
    Preserve
};
enum class Aspect
{
    Auto,
    Color,
    Depth,
    DepthStencil
};
struct TextureRange
{
    uint32_t firstMip = 0, mipCount = 1, firstLayer = 0, layerCount = 1;
    Aspect aspect = Aspect::Auto;
};
struct BufferRange
{
    uint64_t offset = 0, size = 0;
    bool operator==(const BufferRange&) const = default;
};
struct ImportInfo
{
    rhi::State state = rhi::State::Undefined;
    bool initialized = false;
    /// Same-runtime graphics queue completion; prior queued submissions are ordered without a CPU
    /// wait.
    uint64_t completion = 0;
};
struct CompileOptions
{
    bool cull = true;
    /// Adds two timestamp slots per live pass; maximum 32 passes per graph submission.
    bool profile = false;
    /// Native recording workers, including the caller. Callbacks/preparation stay on owner.
    uint32_t recordingThreads = 1;
    bool asyncCompute = false;
    bool memoryAlias = false;
};
struct Edge
{
    uint32_t before, after;
    std::string reason;
};
struct Barrier
{
    uint32_t resource;
    rhi::State before, after;
};
struct ScheduledPass
{
    uint32_t pass;
    std::vector<Barrier> barriers;
    rhi::QueueType queue = rhi::QueueType::Graphics;
    bool waitCompute = false;
};
struct AccessInfo
{
    uint32_t resource, inputVersion, outputVersion;
    Use use;
    bool read, write;
    BufferRange bufferRange;
    TextureRange textureRange;
};
struct PassInfo
{
    std::string name, source, reason;
    PassKind kind;
    bool live = false;
    std::vector<AccessInfo> accesses;
    uint32_t recordingChunk = UINT32_MAX;
    uint64_t firstCommand = 0, lastCommand = 0;
    PassCategory category = PassCategory::Other;
    CommandStatistics commands;
};
struct ResourceInfo
{
    std::string name;
    bool imported = false;
    uint32_t first = UINT32_MAX, last = 0;
    uint64_t estimatedBytes = 0;
    uint32_t versions = 0;
    rhi::State initial = rhi::State::Undefined, final = rhi::State::Undefined;
    uint32_t aliasGroup = UINT32_MAX;
    bool texture = false, exported = false;
    uint32_t width = 0, height = 0;
    rhi::Format format = rhi::Format::Rgba8;
    uint64_t bufferBytes = 0;
};
struct Plan
{
    uint64_t compileNs = 0, prepareNs = 0, recordNs = 0;
    uint32_t recordingThreads = 1, recordingChunks = 0;
    bool asyncCompute = false;
    std::vector<uint32_t> asyncResources;
    std::vector<Barrier> prologueBarriers;
    std::vector<PassInfo> passes;
    std::vector<ResourceInfo> resources;
    std::vector<Edge> edges;
    std::vector<ScheduledPass> schedule;
    std::vector<Barrier> finalBarriers;
    bool memoryAlias = false;
    std::vector<std::vector<uint32_t>> aliasGroups;
    uint64_t aliasBackingBytes = 0;
    std::string profilingReason;
    std::string ToJson() const;
};
struct ExportedResource
{
    uint32_t resource;
    std::shared_ptr<rhi::Texture> texture;
    std::shared_ptr<rhi::Buffer> buffer;
    rhi::State state;
};
struct Submission
{
    uint64_t completion = 0;
    std::vector<ExportedResource> exports;
    struct GpuProfile
    {
        struct Timing
        {
            std::string name;
            double nanoseconds;
            double startNanoseconds = 0, endNanoseconds = 0;
            rhi::QueueType queue = rhi::QueueType::Graphics;
        };
        std::shared_ptr<rhi::Buffer> ticks;
        std::vector<std::string> names;
        uint64_t completion = 0;
        double periodNs = 0;
        std::vector<rhi::QueueClock> clocks;
        /// Requires the same runtime and completed submission; never waits or maps early.
        std::vector<Timing> Read(const rhi::Runtime& runtime) const;
    } profile;
};
rhi::State StateFor(Use use);
bool IsReadOnly(Use use);
} // namespace renderlab::rendergraph
