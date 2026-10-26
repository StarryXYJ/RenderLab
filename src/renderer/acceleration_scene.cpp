#include "renderer/acceleration_scene.hpp"

#include "rendergraph/graph.hpp"
#include "rendergraph/pool.hpp"
#include "rhi/runtime.hpp"

#include <stdexcept>

namespace renderlab::renderer
{
using namespace rendergraph;
void AccelerationScene::EnsureMeshes(std::span<const Mesh> meshes)
{
    for (const auto& mesh : meshes)
    {
        if (const auto found = bottomLevels_.find(mesh.key); found != bottomLevels_.end())
        {
            const auto& geometry = found->second->desc.geometries.front();
            if (geometry.vertices == mesh.vertices && geometry.indices == mesh.indices)
                continue;
        }
        rhi::AccelerationStructureDesc description;
        description.name = mesh.key + ".BLAS";
        description.geometries.push_back({mesh.vertices, mesh.indices, 0, 0, mesh.vertexCount,
                                          mesh.indexCount, mesh.vertexStride});
        auto bottom = runtime_.CreateAccelerationStructure(description);
        Graph graph;
        const auto vertices =
            graph.ImportBuffer("Vertices", mesh.vertices, {rhi::State::Vertex, true});
        const auto indices = graph.ImportBuffer("Indices", mesh.indices, {rhi::State::Index, true});
        const auto storage = graph.ImportBuffer("BLAS.Storage", bottom->storage, {});
        const auto scratch = graph.ImportBuffer("BLAS.Scratch", bottom->scratch, {});
        auto pass = graph.AddComputePass("RayTracing.BuildBLAS");
        pass.ReadBuffer(vertices, Use::AccelerationStructureBuildInput);
        pass.ReadBuffer(indices, Use::AccelerationStructureBuildInput);
        const auto built = pass.WriteBuffer(storage, Use::AccelerationStructureBuildWrite);
        const auto temporary = pass.WriteBuffer(scratch, Use::AccelerationStructureBuildWrite);
        pass.Execute([=](ComputeContext& context)
                     { context.BuildAccelerationStructure(bottom, built, temporary); });
        graph.Export(built, rhi::State::AccelerationStructureRead);
        graph.Export(vertices, rhi::State::Vertex);
        graph.Export(indices, rhi::State::Index);
        graph.Compile();
        runtime_.Wait(graph.Execute(pool_).completion);
        bottom->scratch.reset();
        bottomLevels_.insert_or_assign(mesh.key, std::move(bottom));
        // A replacement BLAS must invalidate a TLAS even when the instance values are unchanged.
        current_.reset();
    }
}

AccelerationScene::Prepared AccelerationScene::Build(Graph& graph, std::vector<Instance> instances)
{
    if (current_ && instances == instances_)
    {
        const auto storage =
            graph.ImportBuffer("RayTracing.SceneAS", current_->storage,
                               {rhi::State::AccelerationStructureRead, true, completion_});
        graph.Export(storage, rhi::State::AccelerationStructureRead);
        return {current_, storage, std::move(instances)};
    }
    rhi::AccelerationStructureDesc description;
    description.name = "RayTracing.SceneAS";
    description.topLevel = true;
    for (const auto& instance : instances)
        description.instances.push_back(
            {bottomLevels_.at(instance.mesh), instance.transform, instance.index, instance.mask});
    auto top = runtime_.CreateAccelerationStructure(description);
    const auto storage = graph.ImportBuffer("RayTracing.SceneAS", top->storage, {});
    const auto scratch = graph.ImportBuffer("RayTracing.SceneScratch", top->scratch, {});
    const auto input = graph.ImportBuffer("RayTracing.SceneInstances", top->instanceData,
                                          {rhi::State::HostWrite, true});
    std::map<const rhi::AccelerationStructure*, BufferHandle> bottomHandles;
    for (const auto& instance : top->desc.instances)
        if (!bottomHandles.contains(instance.bottomLevel.get()))
            bottomHandles.emplace(
                instance.bottomLevel.get(),
                graph.ImportBuffer("RayTracing.BLAS." + std::to_string(bottomHandles.size()),
                                   instance.bottomLevel->storage,
                                   {rhi::State::AccelerationStructureRead, true}));
    auto pass = graph.AddComputePass("RayTracing.BuildTLAS");
    pass.ReadBuffer(input, Use::AccelerationStructureBuildInput);
    for (const auto& [object, handle] : bottomHandles)
        pass.ReadBuffer(handle, Use::AccelerationStructureRead);
    const auto built = pass.WriteBuffer(storage, Use::AccelerationStructureBuildWrite);
    const auto temporary = pass.WriteBuffer(scratch, Use::AccelerationStructureBuildWrite);
    pass.Execute([=](ComputeContext& context)
                 { context.BuildAccelerationStructure(top, built, temporary); });
    graph.Export(built, rhi::State::AccelerationStructureRead);
    return {top, built, std::move(instances)};
}

void AccelerationScene::Commit(Prepared prepared, uint64_t completion)
{
    prepared.topLevel->scratch.reset();
    current_ = std::move(prepared.topLevel);
    instances_ = std::move(prepared.instances);
    completion_ = completion;
}
} // namespace renderlab::renderer
