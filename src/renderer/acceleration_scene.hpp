#pragma once

#include "rendergraph/types.hpp"

#include <array>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace renderlab::rendergraph
{
class Graph;
class Pool;
} // namespace renderlab::rendergraph
namespace renderlab::renderer
{
/** @brief Static mesh BLAS cache and immutable, completion-protected TLAS versions. */
class AccelerationScene final
{
public:
    struct Mesh
    {
        std::string key;
        std::shared_ptr<rhi::Buffer> vertices, indices;
        uint32_t vertexCount, indexCount, vertexStride;
    };
    struct Instance
    {
        std::string mesh;
        std::array<float, 12> transform;
        uint32_t index;
        uint32_t mask = 0xff;
        bool operator==(const Instance&) const = default;
    };
    struct Prepared
    {
        std::shared_ptr<rhi::AccelerationStructure> topLevel;
        rendergraph::BufferHandle storage;
        std::vector<Instance> instances;
    };
    AccelerationScene(rhi::Runtime& runtime, rendergraph::Pool& pool)
        : runtime_(runtime), pool_(pool)
    {
    }
    /// First use builds static BLAS synchronously; geometry returns to Vertex/Index states.
    void EnsureMeshes(std::span<const Mesh> meshes);
    /// Declares a TLAS rebuild only when instance membership, indices or transforms change.
    Prepared Build(rendergraph::Graph& graph, std::vector<Instance> instances);
    /// Publish only after a successful graph submission; recorded commands own build scratch.
    void Commit(Prepared prepared, uint64_t completion);

private:
    rhi::Runtime& runtime_;
    rendergraph::Pool& pool_;
    std::map<std::string, std::shared_ptr<rhi::AccelerationStructure>> bottomLevels_;
    std::shared_ptr<rhi::AccelerationStructure> current_;
    std::vector<Instance> instances_;
    uint64_t completion_ = 0;
};
} // namespace renderlab::renderer
