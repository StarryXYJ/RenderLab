#pragma once

#include "rendergraph/graph.hpp"
#include "rendergraph/pool.hpp"
#include "rhi/commands.hpp"

#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace renderlab::rendergraph
{
namespace detail
{
enum class Phase
{
    Building,
    Compiled,
    Preparing,
    Prepared,
    Recording,
    Recorded,
    Submitted,
    Failed
};
struct Version
{
    uint32_t writer = UINT32_MAX;
    bool preserve = false;
};
struct Resource
{
    bool isTexture = false, imported = false;
    rhi::TextureDesc textureDesc;
    rhi::BufferDesc bufferDesc;
    ImportInfo initial;
    std::vector<Version> versions{{}};
    std::optional<rhi::State> exported;
    std::shared_ptr<rhi::Texture> texture;
    std::shared_ptr<rhi::TextureView> view;
    std::shared_ptr<rhi::Buffer> buffer;
    std::shared_ptr<Allocation> lease;
    rhi::State start = rhi::State::Undefined, final = rhi::State::Undefined;
    const std::string& Name() const
    {
        return isTexture ? textureDesc.name : bufferDesc.name;
    }
};
struct Access
{
    uint32_t resource, input, output;
    Use use;
    bool read, write;
    BufferRange bufferRange;
};
struct Attachment
{
    uint32_t resource;
    rhi::LoadOp load;
    std::array<float, 4> clear;
};
struct Pass
{
    std::string name, source;
    PassKind kind;
    bool sideEffect = false;
    std::vector<Access> accesses;
    std::optional<Attachment> color, depth;
    Callback callback;
    bool depthReadOnly = false;
};
inline void Require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::invalid_argument("RenderGraph: " + message);
}
uint64_t EstimatedBytes(const Resource& resource);
} // namespace detail
struct Graph::Impl
{
    uint64_t id;
    detail::Phase phase = detail::Phase::Building;
    uint32_t active = UINT32_MAX;
    std::vector<detail::Resource> resources;
    std::vector<detail::Pass> passes;
    struct NamedResource
    {
        uint32_t resource;
        bool texture;
    };
    std::unordered_map<std::string, NamedResource> names;
    std::vector<Edge> order;
    Plan plan;
    Pool* pool = nullptr;
    std::unique_ptr<rhi::CommandList> commands;
    void Building() const;
    void Builder(uint32_t pass) const;
    detail::Resource& Validate(uint64_t graph, uint32_t resource, uint32_t version, bool texture);
    void ValidateUse(uint32_t pass, const detail::Resource& resource, Use use, bool write) const;
    void ValidateState(const detail::Resource& resource, rhi::State state) const;
    void AddAccess(uint32_t pass, detail::Access access);
    void Scope(uint32_t pass) const;
    detail::Resource& Resolve(uint32_t pass, uint64_t graph, uint32_t resource, uint32_t version,
                              bool texture, std::optional<Use> use = {});
    void Barriers();
    void ValidateBindings(uint32_t pass, const rhi::BindingGroup& group);
    void CheckBufferRange(uint32_t pass, BufferHandle handle, uint64_t offset, uint64_t size);
};
} // namespace renderlab::rendergraph
