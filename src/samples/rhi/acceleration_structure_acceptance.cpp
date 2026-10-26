#include "samples/rhi/acceleration_structure_acceptance.hpp"

#include "core/log.hpp"
#include "rendergraph/graph.hpp"
#include "rendergraph/pool.hpp"
#include "rhi/commands.hpp"
#include "rhi/runtime.hpp"
#include "shader/dxc_compiler.hpp"

#include <array>
#include <cstring>
#include <format>
#include <map>
#include <stdexcept>
#include <vector>

namespace renderlab::samples
{
namespace
{
using namespace rhi;
using namespace rendergraph;

std::shared_ptr<Buffer> UploadAccelerationInput(Runtime& runtime, const std::string& name,
                                                std::span<const std::byte> bytes)
{
    auto upload = runtime.CreateBuffer({bytes.size(), Memory::Upload, name + ".Upload"});
    std::memcpy(upload->Map().data(), bytes.data(), bytes.size());
    auto destination =
        runtime.CreateBuffer({bytes.size(), Memory::Device, name, false, true, true});
    auto commands = runtime.CreateCommands();
    commands->Barrier(upload, State::HostWrite, State::CopySource);
    commands->Barrier(destination, State::Undefined, State::CopyDest);
    commands->Copy(upload, destination, bytes.size());
    commands->Barrier(destination, State::CopyDest, State::AccelerationStructureBuildInput);
    runtime.Wait(runtime.Submit(std::move(commands)));
    return destination;
}

uint32_t BuildAndQuery(Runtime& runtime, Pool& pool, std::shared_ptr<Pipeline> queryPipeline,
                       const std::shared_ptr<AccelerationStructure>& accelerationStructure,
                       const std::string& name)
{
    std::shared_ptr<Buffer> resultBuffer;
    uint64_t completion = 0;
    {
        Graph graph;
        std::map<const Buffer*, BufferHandle> imported;
        uint32_t importIndex = 0;
        const auto import =
            [&](const std::shared_ptr<Buffer>& buffer, State state, bool initialized)
        {
            if (const auto found = imported.find(buffer.get()); found != imported.end())
                return found->second;
            const auto resourceName = "AS.Input." + std::to_string(importIndex++);
            auto handle = graph.ImportBuffer(resourceName, buffer, {state, initialized});
            imported.emplace(buffer.get(), handle);
            return handle;
        };
        for (const auto& geometry : accelerationStructure->desc.geometries)
        {
            import(geometry.vertices, State::AccelerationStructureBuildInput, true);
            if (geometry.indices)
                import(geometry.indices, State::AccelerationStructureBuildInput, true);
        }
        if (accelerationStructure->instanceData)
            import(accelerationStructure->instanceData, State::HostWrite, true);
        for (const auto& instance : accelerationStructure->desc.instances)
            import(instance.bottomLevel->storage, State::AccelerationStructureRead, true);
        const auto outputStorage = import(accelerationStructure->storage, State::Undefined, false);
        const auto scratch = import(accelerationStructure->scratch, State::Undefined, false);
        BufferHandle result;
        if (accelerationStructure->desc.topLevel)
        {
            resultBuffer = runtime.CreateBuffer(
                {sizeof(uint32_t), Memory::Device, name + ".RayQueryResult", true});
            result =
                graph.ImportBuffer("AS.RayQueryResult", resultBuffer, {State::Undefined, false});
        }
        auto build = graph.AddComputePass(name + ".Build");
        for (const auto& geometry : accelerationStructure->desc.geometries)
        {
            build.ReadBuffer(
                import(geometry.vertices, State::AccelerationStructureBuildInput, true),
                Use::AccelerationStructureBuildInput);
            if (geometry.indices)
                build.ReadBuffer(
                    import(geometry.indices, State::AccelerationStructureBuildInput, true),
                    Use::AccelerationStructureBuildInput);
        }
        if (accelerationStructure->instanceData)
            build.ReadBuffer(import(accelerationStructure->instanceData, State::HostWrite, true),
                             Use::AccelerationStructureBuildInput);
        for (const auto& instance : accelerationStructure->desc.instances)
            build.ReadBuffer(
                import(instance.bottomLevel->storage, State::AccelerationStructureRead, true),
                Use::AccelerationStructureRead);
        auto built = build.WriteBuffer(outputStorage, Use::AccelerationStructureBuildWrite);
        const auto builtScratch = build.WriteBuffer(scratch, Use::AccelerationStructureBuildWrite);
        build.Execute(
            [accelerationStructure, built, builtScratch](ComputeContext& context)
            { context.BuildAccelerationStructure(accelerationStructure, built, builtScratch); });

        if (accelerationStructure->desc.allowUpdate)
        {
            auto update = graph.AddComputePass(name + ".Update");
            for (const auto& geometry : accelerationStructure->desc.geometries)
            {
                update.ReadBuffer(imported.at(geometry.vertices.get()),
                                  Use::AccelerationStructureBuildInput);
                if (geometry.indices)
                    update.ReadBuffer(imported.at(geometry.indices.get()),
                                      Use::AccelerationStructureBuildInput);
            }
            if (accelerationStructure->instanceData)
                update.ReadBuffer(imported.at(accelerationStructure->instanceData.get()),
                                  Use::AccelerationStructureBuildInput);
            for (const auto& instance : accelerationStructure->desc.instances)
                update.ReadBuffer(imported.at(instance.bottomLevel->storage.get()),
                                  Use::AccelerationStructureRead);
            built = update.ReadWriteBuffer(built, Use::AccelerationStructureBuildWrite);
            const auto temporary =
                update.WriteBuffer(builtScratch, Use::AccelerationStructureBuildWrite);
            update.Execute(
                [accelerationStructure, built, temporary](ComputeContext& context)
                {
                    context.BuildAccelerationStructure(accelerationStructure, built, temporary,
                                                       true);
                });
        }

        BufferHandle writtenResult;
        if (accelerationStructure->desc.topLevel)
        {
            auto query = graph.AddComputePass(name + ".RayQuery");
            const auto queryStorage = query.ReadBuffer(built, Use::AccelerationStructureRead);
            writtenResult = query.WriteBuffer(result, Use::Storage);
            query.Execute(
                [=](ComputeContext& context)
                {
                    context.Bind(queryPipeline);
                    context.BindAccelerationStructure(0, accelerationStructure, queryStorage);
                    context.BindBuffer(1, writtenResult, 0, sizeof(uint32_t), sizeof(uint32_t));
                    context.Dispatch(1, 1);
                });
        }
        graph.Export(built, State::AccelerationStructureRead);
        if (accelerationStructure->desc.topLevel)
            graph.Export(writtenResult, State::CopySource);
        graph.Compile();
        completion = graph.Execute(pool).completion;
        accelerationStructure->scratch.reset();
    }
    runtime.Wait(completion);

    if (!accelerationStructure->desc.topLevel)
        return UINT32_MAX;

    auto readback = runtime.CreateBuffer({sizeof(uint32_t), Memory::Readback, name + ".Readback"});
    auto copy = runtime.CreateCommands();
    copy->Copy(resultBuffer, readback, sizeof(uint32_t));
    copy->Barrier(readback, State::CopyDest, State::HostRead);
    runtime.Wait(runtime.Submit(std::move(copy)));
    uint32_t value = 0;
    std::memcpy(&value, readback->Map().data(), sizeof(value));
    return value;
}

std::array<float, 12> Translation(float x)
{
    return {1, 0, 0, x, 0, 1, 0, 0, 0, 0, 1, 0};
}
} // namespace

void VerifyAccelerationStructures(Runtime& runtime, shader::Target target,
                                  const std::filesystem::path& shader,
                                  const std::filesystem::path& output, Logger& logger)
{
    if (!runtime.SupportsAccelerationStructures() || !runtime.SupportsRayQuery())
        throw std::runtime_error(
            "--as-test requires accelerationStructure and rayQuery device capabilities");
    std::filesystem::create_directories(output);
    constexpr std::array<float, 9> vertices{-1, -1, 0, 1, -1, 0, 0, 1, 0};
    constexpr std::array<uint32_t, 3> indices{0, 1, 2};
    auto vertexBuffer = UploadAccelerationInput(runtime, "AS.Triangle.Vertices",
                                                std::as_bytes(std::span{vertices}));
    auto indexBuffer =
        UploadAccelerationInput(runtime, "AS.Triangle.Indices", std::as_bytes(std::span{indices}));
    AccelerationStructureDesc bottomDesc;
    bottomDesc.name = "AS.Triangle.BLAS";
    bottomDesc.allowUpdate = true;
    bottomDesc.geometries.push_back({vertexBuffer, indexBuffer, 0, 0, 3, 3, sizeof(float) * 3});
    const auto bottomSizes = runtime.QueryAccelerationStructureSizes(bottomDesc);
    auto bottom = runtime.CreateAccelerationStructure(bottomDesc);

    shader::DxcCompiler compiler;
    shader::ProgramRequest request;
    request.entry = {ShaderStage::Compute, shader, "CSMain"};
    request.target = target;
    request.targetProfile = "cs_6_5";
    auto module = runtime.CreateShader(ShaderStage::Compute, compiler.Compile(request), "CSMain");
    auto layout = runtime.CreateLayout(
        {{0, BindingKind::AccelerationStructure}, {1, BindingKind::StorageBuffer}});
    PipelineDesc description;
    description.layout = layout;
    description.compute = module;
    auto queryPipeline = runtime.CreatePipeline(std::move(description));
    Pool pool(runtime, 16, 16ull * 1024 * 1024);
    if (BuildAndQuery(runtime, pool, queryPipeline, bottom, "AS.BLAS") != UINT32_MAX)
        throw std::runtime_error("BLAS graph build did not complete");

    const auto topLevel =
        [&](std::vector<AccelerationStructureInstance> instances, const std::string& name)
    {
        AccelerationStructureDesc description;
        description.name = name;
        description.topLevel = true;
        description.allowUpdate = true;
        description.instances = std::move(instances);
        return runtime.CreateAccelerationStructure(description);
    };
    const auto instance = [&](uint32_t id, float x)
    { return AccelerationStructureInstance{bottom, Translation(x), id, 0xff}; };
    auto centered = topLevel({instance(0, 0)}, "AS.TLAS.Centered");
    if (BuildAndQuery(runtime, pool, queryPipeline, centered, "AS.TLAS.Centered") != 1)
        throw std::runtime_error("TLAS Ray Query did not hit the centered triangle");
    auto added = topLevel({instance(0, 0), instance(1, 4)}, "AS.TLAS.AddedInstance");
    if (BuildAndQuery(runtime, pool, queryPipeline, added, "AS.TLAS.AddedInstance") != 1)
        throw std::runtime_error("Adding a TLAS instance changed the centered ray result");
    auto moved = topLevel({instance(0, 4)}, "AS.TLAS.MovedInstance");
    if (BuildAndQuery(runtime, pool, queryPipeline, moved, "AS.TLAS.MovedInstance") != 0)
        throw std::runtime_error("Moving a TLAS instance did not remove the ray hit");
    auto empty = topLevel({}, "AS.TLAS.Empty");
    if (BuildAndQuery(runtime, pool, queryPipeline, empty, "AS.TLAS.Empty") != 0)
        throw std::runtime_error("Empty TLAS returned a ray hit");
    logger.Write(
        LogLevel::Info,
        std::format("M5.2 AS/Ray Query passed on {}: BLAS storage={} B, TLAS storage={} B, "
                    "instance add/move/remove and graph build/read declarations validated",
                    target == shader::Target::Dxil ? "DX12" : "Vulkan", bottomSizes.storage,
                    centered->sizes.storage));
}
} // namespace renderlab::samples
