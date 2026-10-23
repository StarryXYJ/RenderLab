#pragma once

#include "rhi/resources.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

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
    Copy
};
enum class Use
{
    Sampled,
    Uniform,
    ShaderRead,
    Storage,
    Color,
    Depth,
    CopySource,
    CopyDestination,
    Vertex,
    Index,
    Indirect
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
    Depth
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
};
struct AccessInfo
{
    uint32_t resource, inputVersion, outputVersion;
    Use use;
    bool read, write;
    BufferRange bufferRange;
};
struct PassInfo
{
    std::string name, source, reason;
    PassKind kind;
    bool live = false;
    std::vector<AccessInfo> accesses;
};
struct ResourceInfo
{
    std::string name;
    bool imported = false;
    uint32_t first = UINT32_MAX, last = 0;
    uint64_t estimatedBytes = 0;
    uint32_t versions = 0;
    rhi::State initial = rhi::State::Undefined, final = rhi::State::Undefined;
};
struct Plan
{
    uint64_t compileNs = 0, prepareNs = 0, recordNs = 0;
    std::vector<PassInfo> passes;
    std::vector<ResourceInfo> resources;
    std::vector<Edge> edges;
    std::vector<ScheduledPass> schedule;
    std::vector<Barrier> finalBarriers;
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
};
rhi::State StateFor(Use use);
bool IsReadOnly(Use use);
} // namespace renderlab::rendergraph
