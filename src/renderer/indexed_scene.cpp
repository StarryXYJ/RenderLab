#include "renderer/indexed_scene.hpp"

#include "rendergraph/graph.hpp"
#include "rhi/commands.hpp"
#include "rhi/upload.hpp"
#include "shader/dxc_compiler.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace renderlab::renderer
{
namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::invalid_argument(message);
}
struct MeshRange
{
    uint32_t vertex = 0, index = 0, count = 0;
};
struct Batch
{
    std::shared_ptr<rhi::Pipeline> pipeline;
    int32_t queue;
    uint32_t stencil;
    bool gbuffer;
    std::vector<uint32_t> instances;
    std::vector<uint32_t> candidates;
};
} // namespace
struct IndexedScene::Impl
{
    rhi::Runtime& runtime;
    rhi::UploadPool uploadPool;
    std::unique_ptr<rhi::UploadPool::Batch> uploads;
    std::shared_ptr<rhi::BindingLayout> layout;
    std::shared_ptr<rhi::ShaderModule> vertexShader, forwardShader, gbufferShader;
    std::shared_ptr<rhi::Pipeline> clearCounts, cull;
    std::shared_ptr<rhi::Sampler> sampler;
    std::vector<std::shared_ptr<rhi::Pipeline>> pipelines;
    std::shared_ptr<rhi::Buffer> vertices, indices;
    std::vector<std::string> geometryKeys;
    std::map<std::string, MeshRange> meshes;
    IndexedSceneStatistics stats;
    IndexedCullDiagnostics diagnostics;
    Impl(rhi::Runtime& r, shader::Target target, const std::filesystem::path& shaders,
         const shader::Interface& types)
        : runtime(r), uploadPool(r, 8 * 1024 * 1024, 8)
    {
        const auto support = runtime.GetIndirectSupport();
        Require(support.firstInstance && support.shaderInstanceIndex &&
                    runtime.SampledTextureArrayLimit() >= IndexedTextureCapacity,
                "Indexed scene requires nonzero firstInstance, shader instance indexing "
                "(DX12 SM 6.8) and 64 nonuniform sampled textures");
        layout =
            runtime.CreateLayout({{0, rhi::BindingKind::ConstantBuffer},
                                  {1, rhi::BindingKind::ConstantBuffer},
                                  {2, rhi::BindingKind::ConstantBuffer},
                                  {0, rhi::BindingKind::ReadOnlyBuffer, 2},
                                  {1, rhi::BindingKind::ReadOnlyBuffer, 2},
                                  {2, rhi::BindingKind::ReadOnlyBuffer, 2},
                                  {16, rhi::BindingKind::SampledTexture, 1, IndexedTextureCapacity},
                                  {0, rhi::BindingKind::Sampler, 1},
                                  {8, rhi::BindingKind::SampledTexture},
                                  {9, rhi::BindingKind::SampledTexture},
                                  {10, rhi::BindingKind::SampledTexture}});
        shader::DxcCompiler compiler;
        const auto module = [&](rhi::ShaderStage stage, const char* entry)
        {
            shader::ProgramRequest request;
            request.entry = {stage, shaders / "IndexedRaster.hlsl", entry};
            request.target = target;
            request.includeDirectories = {shaders};
            request.generatedTypes = shader::GenerateTypesHeader(types);
            if (stage == rhi::ShaderStage::Vertex && target == shader::Target::Dxil)
                request.targetProfile = "vs_6_8";
            return runtime.CreateShader(stage, compiler.Compile(request), entry);
        };
        vertexShader = module(rhi::ShaderStage::Vertex, "VSMain");
        forwardShader = module(rhi::ShaderStage::Pixel, "PSForward");
        gbufferShader = module(rhi::ShaderStage::Pixel, "PSGBuffer");
        sampler = runtime.CreateSampler();
        const auto compute = [&](const char* entry, std::vector<rhi::BindingDesc> entries)
        {
            shader::ProgramRequest request;
            request.entry = {rhi::ShaderStage::Compute, shaders / "IndexedCull.hlsl", entry};
            request.target = target;
            request.includeDirectories = {shaders};
            request.generatedTypes = shader::GenerateTypesHeader(types);
            rhi::PipelineDesc desc;
            desc.layout = runtime.CreateLayout(std::move(entries));
            desc.compute =
                runtime.CreateShader(rhi::ShaderStage::Compute, compiler.Compile(request), entry);
            return runtime.CreatePipeline(std::move(desc));
        };
        clearCounts = compute("Clear", {{5, rhi::BindingKind::StorageBuffer}});
        cull = compute("Cull", {{0, rhi::BindingKind::ReadOnlyBuffer},
                                {1, rhi::BindingKind::ReadOnlyBuffer},
                                {2, rhi::BindingKind::ReadOnlyBuffer},
                                {3, rhi::BindingKind::StorageBuffer},
                                {4, rhi::BindingKind::StorageBuffer},
                                {5, rhi::BindingKind::StorageBuffer},
                                {6, rhi::BindingKind::ConstantBuffer}});
    }
    std::shared_ptr<rhi::Pipeline> Pipeline(const IndexedDrawInput& input)
    {
        auto desc = input.reference->desc;
        desc.layout = layout;
        desc.vertex = vertexShader;
        desc.pixel = input.gbuffer ? gbufferShader : forwardShader;
        auto result = runtime.CreatePipeline(std::move(desc));
        if (std::ranges::find(pipelines, result) == pipelines.end())
        {
            if (pipelines.size() == 128)
                pipelines.erase(pipelines.begin());
            pipelines.push_back(result);
        }
        return result;
    }
    void EnsureGeometry(const std::vector<IndexedDrawInput>& inputs)
    {
        std::map<std::string, const assets::Mesh*> sources;
        for (const auto& input : inputs)
        {
            Require(input.mesh && !input.meshKey.empty(), "Indexed scene requires mesh identity");
            sources.try_emplace(input.meshKey, input.mesh);
        }
        std::vector<std::string> keys;
        for (const auto& [key, mesh] : sources)
        {
            (void)mesh;
            keys.push_back(key);
        }
        if (keys == geometryKeys)
            return;
        std::vector<assets::Vertex> vertexData;
        std::vector<uint32_t> indexData;
        std::map<std::string, MeshRange> ranges;
        for (const auto& [key, mesh] : sources)
        {
            Require(!mesh->vertices.empty() && !mesh->indices.empty() &&
                        mesh->vertices.size() <= uint64_t(INT32_MAX) - vertexData.size() &&
                        mesh->indices.size() <= uint64_t(UINT32_MAX) - indexData.size(),
                    "Indexed geometry exceeds the common indirect ABI");
            ranges[key] = {static_cast<uint32_t>(vertexData.size()),
                           static_cast<uint32_t>(indexData.size()),
                           static_cast<uint32_t>(mesh->indices.size())};
            vertexData.insert(vertexData.end(), mesh->vertices.begin(), mesh->vertices.end());
            indexData.insert(indexData.end(), mesh->indices.begin(), mesh->indices.end());
        }
        auto commands = runtime.CreateCommands();
        const auto upload =
            [&](std::span<const std::byte> bytes, const char* name, rhi::State final)
        {
            auto staging = runtime.CreateBuffer({bytes.size(), rhi::Memory::Upload, name});
            std::memcpy(staging->Map().data(), bytes.data(), bytes.size());
            auto destination = runtime.CreateBuffer({bytes.size(), rhi::Memory::Device, name});
            commands->Barrier(staging, rhi::State::HostWrite, rhi::State::CopySource);
            commands->Barrier(destination, rhi::State::Undefined, rhi::State::CopyDest);
            commands->Copy(staging, destination, bytes.size());
            commands->Barrier(destination, rhi::State::CopyDest, final);
            return destination;
        };
        auto newVertices =
            upload(std::as_bytes(std::span(vertexData)), "Indexed.Vertices", rhi::State::Vertex);
        auto newIndices =
            upload(std::as_bytes(std::span(indexData)), "Indexed.Indices", rhi::State::Index);
        runtime.Wait(runtime.Submit(std::move(commands)));
        vertices = std::move(newVertices);
        indices = std::move(newIndices);
        meshes = std::move(ranges);
        geometryKeys = std::move(keys);
        ++stats.geometryUploads;
    }
};
IndexedScene::IndexedScene(rhi::Runtime& runtime, shader::Target target,
                           const std::filesystem::path& shaders, const shader::Interface& types)
    : impl_(std::make_unique<Impl>(runtime, target, shaders, types))
{
}
IndexedScene::~IndexedScene() = default;
IndexedSceneStatistics IndexedScene::Statistics() const
{
    return impl_->stats;
}
IndexedCullDiagnostics IndexedScene::Diagnostics() const
{
    return impl_->diagnostics;
}
void IndexedScene::Commit(uint64_t completion)
{
    if (impl_->uploads)
    {
        impl_->uploads->Seal(completion);
        impl_->uploads.reset();
    }
}
void IndexedScene::Build(rendergraph::Graph& graph, const std::vector<IndexedDrawInput>& inputs,
                         const IndexedFrameBindings& frame, bool indirect,
                         std::vector<DrawPacket>& opaque, std::vector<DrawPacket>& gbuffer,
                         bool gpuCulling, bool diagnostics)
{
    using namespace rendergraph;
    auto& self = *impl_;
    self.uploads.reset();
    self.stats = {.geometryUploads = self.stats.geometryUploads};
    self.stats.gpuCulling = gpuCulling;
    self.diagnostics = {};
    Require(!gpuCulling || (indirect && self.runtime.GetIndirectSupport().countBuffer),
            "GPU culling requires enabled indirect count buffers");
    Require(inputs.size() <= IndexedInstanceCapacity, "Indexed scene exceeds 16384 instances");
    if (inputs.empty())
        return;
    self.EnsureGeometry(inputs);
    self.uploads = std::make_unique<rhi::UploadPool::Batch>(self.uploadPool);
    std::map<const rhi::Texture*, uint32_t> textureIndices;
    std::vector<std::string> textureNames;
    std::map<std::string, uint32_t> materialIndices;
    std::vector<IndexedMaterialData> materials;
    std::vector<IndexedInstanceData> objects;
    objects.reserve(inputs.size());
    std::map<const rhi::Pipeline*, std::shared_ptr<rhi::Pipeline>> framePipelines;
    std::vector<Batch> batches;
    std::map<std::tuple<int32_t, const rhi::Pipeline*, uint32_t, bool>, uint32_t> batchIndices;
    for (const auto& input : inputs)
    {
        auto material = input.material;
        std::array<uint32_t, 5> maps;
        for (uint32_t i = 0; i < maps.size(); ++i)
        {
            Require(input.textures[i] && !input.textureNames[i].empty(),
                    "Indexed material requires five resolved sampled textures");
            const auto* texture = input.textures[i]->texture.get();
            if (!textureIndices.contains(texture))
            {
                Require(textureNames.size() < IndexedTextureCapacity,
                        "Indexed scene exceeds 64 sampled image/color-space versions");
                textureIndices[texture] = static_cast<uint32_t>(textureNames.size());
                textureNames.push_back(input.textureNames[i]);
            }
            maps[i] = textureIndices.at(texture);
        }
        std::copy_n(maps.begin(), 4, material.maps.begin());
        material.flags[0] = maps[4];
        const std::string materialKey(reinterpret_cast<const char*>(&material), sizeof(material));
        if (!materialIndices.contains(materialKey))
        {
            materialIndices[materialKey] = static_cast<uint32_t>(materials.size());
            materials.push_back(material);
        }
        if (!framePipelines.contains(input.reference.get()))
            framePipelines.emplace(input.reference.get(), self.Pipeline(input));
        const auto& pipeline = framePipelines.at(input.reference.get());
        const auto key =
            std::tuple{input.queue, pipeline.get(), input.stencilReference, input.gbuffer};
        if (!batchIndices.contains(key))
        {
            batchIndices[key] = static_cast<uint32_t>(batches.size());
            batches.push_back({pipeline, input.queue, input.stencilReference, input.gbuffer, {}});
        }
        const auto batch = batchIndices.at(key);
        const auto& mesh = self.meshes.at(input.meshKey);
        IndexedInstanceData object;
        object.world = input.instance.world.values;
        object.normal = input.instance.normal.values;
        object.previousWorld = input.instance.previousWorld.values;
        object.minimum = {input.instance.bounds.minimum.x, input.instance.bounds.minimum.y,
                          input.instance.bounds.minimum.z, 0};
        object.maximum = {input.instance.bounds.maximum.x, input.instance.bounds.maximum.y,
                          input.instance.bounds.maximum.z, 0};
        object.indices = {mesh.index, materialIndices.at(materialKey), mesh.count, mesh.vertex};
        batches[batch].candidates.push_back(static_cast<uint32_t>(objects.size()));
        if (!gpuCulling && input.cpuVisible)
            batches[batch].instances.push_back(static_cast<uint32_t>(objects.size()));
        objects.push_back(object);
    }
    self.stats.instances = static_cast<uint32_t>(objects.size());
    self.stats.materials = static_cast<uint32_t>(materials.size());
    self.stats.textures = static_cast<uint32_t>(textureNames.size());
    self.stats.batches = static_cast<uint32_t>(batches.size());
    std::vector<uint32_t> visible;
    std::vector<rhi::DrawIndexedArguments> arguments;
    std::vector<uint32_t> offsets;
    std::vector<uint32_t> candidates;
    std::vector<std::array<uint32_t, 4>> batchRanges;
    for (const auto& batch : batches)
    {
        if (gpuCulling)
        {
            const auto offset = static_cast<uint32_t>(candidates.size());
            const auto count = static_cast<uint32_t>(batch.candidates.size());
            offsets.push_back(offset);
            batchRanges.push_back({offset, count, 0, 0});
            candidates.insert(candidates.end(), batch.candidates.begin(), batch.candidates.end());
            continue;
        }
        offsets.push_back(static_cast<uint32_t>(visible.size()));
        for (const auto instance : batch.instances)
        {
            const auto& object = objects[instance];
            arguments.push_back({object.indices[2], 1, object.indices[0],
                                 static_cast<int32_t>(object.indices[3]),
                                 static_cast<uint32_t>(visible.size())});
            visible.push_back(instance);
        }
    }
    self.stats.visible = static_cast<uint32_t>(visible.size());
    if (!gpuCulling && visible.empty())
        return;
    Require(!indirect ||
                std::ranges::all_of(batches,
                                    [&](const auto& batch)
                                    {
                                        return (gpuCulling ? batch.candidates.size()
                                                           : batch.instances.size()) <=
                                               self.runtime.GetIndirectSupport().maxDrawCount;
                                    }),
            "Indexed batch exceeds the enabled multi-draw limit");
    std::map<const rhi::Buffer*, std::string> imported;
    const auto upload = [&](auto& values, const std::string& name, uint32_t slot, uint32_t stride)
    {
        const auto slice = self.uploads->Write(std::as_bytes(std::span(values)));
        if (!imported.contains(slice.buffer.get()))
        {
            graph.ImportBuffer(name, slice.buffer, {rhi::State::HostWrite, true});
            imported[slice.buffer.get()] = name;
        }
        return BufferBinding{slot,
                             imported.at(slice.buffer.get()),
                             Use::ShaderRead,
                             {slice.offset, slice.size},
                             stride,
                             2};
    };
    const auto objectBinding = upload(objects, "Indexed.Objects", 0, sizeof(IndexedInstanceData));
    const auto materialBinding =
        upload(materials, "Indexed.Materials", 1, sizeof(IndexedMaterialData));
    BufferBinding visibleBinding;
    if (!gpuCulling)
        visibleBinding = upload(visible, "Indexed.Visible", 2, 4);
    graph.ImportBuffer("Indexed.Vertices", self.vertices, {rhi::State::Vertex, true});
    graph.ImportBuffer("Indexed.Indices", self.indices, {rhi::State::Index, true});
    if (gpuCulling)
    {
        const auto candidateBinding = upload(candidates, "Indexed.Candidates", 1, 4);
        const auto batchBinding = upload(batchRanges, "Indexed.Batches", 2, 16);
        const uint64_t visibleBytes = uint64_t(objects.size()) * 4;
        const uint64_t countBytes = uint64_t(batches.size()) * 4;
        const uint64_t argumentBytes = uint64_t(objects.size()) * sizeof(rhi::DrawIndexedArguments);
        auto gpuVisible =
            graph.CreateBuffer("Indexed.GPUVisible", {visibleBytes, rhi::Memory::Device, "", true});
        auto gpuArguments =
            graph.CreateBuffer("Indexed.Arguments", {argumentBytes, rhi::Memory::Device, "", true});
        auto counts =
            graph.CreateBuffer("Indexed.Count", {countBytes, rhi::Memory::Device, "", true});
        auto clear = graph.AddComputePass("Indexed.ClearCounts");
        counts = clear.WriteBuffer(counts);
        clear.Execute(
            [pipeline = self.clearCounts, counts, countBytes,
             groups = (static_cast<uint32_t>(batches.size()) + 63) / 64](ComputeContext& context)
            {
                context.Bind(pipeline);
                context.BindBuffer(5, counts, 0, countBytes, 4);
                context.Dispatch(groups, 1);
            });
        auto pass = graph.AddComputePass("Indexed.FrustumCulling");
        const auto objectsHandle =
            pass.ReadBuffer(objectBinding.buffer, Use::ShaderRead, objectBinding.range);
        const auto candidatesHandle =
            pass.ReadBuffer(candidateBinding.buffer, Use::ShaderRead, candidateBinding.range);
        const auto batchesHandle =
            pass.ReadBuffer(batchBinding.buffer, Use::ShaderRead, batchBinding.range);
        const auto view = pass.ReadBuffer(frame.view.buffer, Use::Uniform, frame.view.range);
        gpuVisible = pass.WriteBuffer(gpuVisible);
        gpuArguments = pass.WriteBuffer(gpuArguments);
        counts = pass.ReadWriteBuffer(counts);
        pass.Execute(
            [=, pipeline = self.cull,
             groups = static_cast<uint32_t>(batches.size())](ComputeContext& context)
            {
                context.Bind(pipeline);
                context.BindBuffer(0, objectsHandle, objectBinding.range.offset,
                                   objectBinding.range.size, objectBinding.stride);
                context.BindBuffer(1, candidatesHandle, candidateBinding.range.offset,
                                   candidateBinding.range.size, 4);
                context.BindBuffer(2, batchesHandle, batchBinding.range.offset,
                                   batchBinding.range.size, 16);
                context.BindBuffer(3, gpuVisible, 0, visibleBytes, 4);
                context.BindBuffer(4, gpuArguments, 0, argumentBytes,
                                   sizeof(rhi::DrawIndexedArguments));
                context.BindBuffer(5, counts, 0, countBytes, 4);
                context.BindBuffer(6, view, frame.view.range.offset, frame.view.range.size);
                context.Dispatch(groups, 1);
            });
        visibleBinding = {2, "Indexed.GPUVisible", Use::ShaderRead, {0, visibleBytes}, 4, 2};
        if (diagnostics)
        {
            self.diagnostics.readback = self.runtime.CreateBuffer(
                {countBytes + visibleBytes, rhi::Memory::Readback, "Indexed.Culling.Readback"});
            self.diagnostics.visibleOffset = static_cast<uint32_t>(countBytes);
            for (const auto& range : batchRanges)
            {
                self.diagnostics.batchOffsets.push_back(range[0]);
                self.diagnostics.batchCapacities.push_back(range[1]);
            }
            auto readback =
                graph.ImportBuffer("Indexed.Culling.Readback", self.diagnostics.readback,
                                   {rhi::State::CopyDest, false});
            auto copy = graph.AddCopyPass("Indexed.Culling.Readback");
            copy.ReadBuffer(counts);
            copy.ReadBuffer(gpuVisible);
            readback = copy.WriteBuffer(readback);
            copy.Execute(
                [=](CopyContext& context)
                {
                    context.Copy(counts, readback, countBytes);
                    context.Copy(gpuVisible, readback, visibleBytes, 0, countBytes);
                });
            graph.Export(readback, rhi::State::HostRead);
        }
    }
    else if (indirect)
    {
        const auto source = upload(arguments, "Indexed.CPUArgs", 0, 4);
        auto destination = graph.CreateBuffer(
            "Indexed.Arguments",
            {arguments.size() * sizeof(rhi::DrawIndexedArguments), rhi::Memory::Device, "", true});
        auto pass = graph.AddCopyPass("Indexed.UploadArguments");
        const auto staging = pass.ReadBuffer(source.buffer, Use::CopySource, source.range);
        destination = pass.WriteBuffer(destination);
        pass.Execute(
            [=](CopyContext& context)
            { context.Copy(staging, destination, source.range.size, source.range.offset); });
    }
    for (uint32_t b = 0; b < batches.size(); ++b)
    {
        const auto& batch = batches[b];
        if (!gpuCulling && batch.instances.empty())
            continue;
        DrawPacket draw;
        draw.pipeline = batch.pipeline;
        draw.vertices = "Indexed.Vertices";
        draw.indices = "Indexed.Indices";
        draw.stencilReference = batch.stencil;
        draw.queue = batch.queue;
        draw.buffers = {frame.view,    frame.lighting,  frame.shadow,
                        objectBinding, materialBinding, visibleBinding};
        for (uint32_t i = 0; i < IndexedTextureCapacity; ++i)
            draw.textures.push_back({16, textureNames[i < textureNames.size() ? i : 0], 1, i});
        draw.textures.push_back({8, frame.environment});
        draw.textures.push_back({9, frame.brdf});
        draw.textures.push_back({10, "MainLightShadow"});
        draw.samplers = {{0, self.sampler, 1}};
        auto& destination = batch.gbuffer ? gbuffer : opaque;
        if (indirect)
        {
            draw.kind = DrawKind::IndexedIndirect;
            draw.indirect = "Indexed.Arguments";
            draw.indirectOffset = uint64_t(offsets[b]) * sizeof(rhi::DrawIndexedArguments);
            draw.indirectMaxCount = static_cast<uint32_t>(gpuCulling ? batch.candidates.size()
                                                                     : batch.instances.size());
            if (gpuCulling)
            {
                draw.indirectCount = "Indexed.Count";
                draw.indirectCountOffset = uint64_t(b) * 4;
            }
            destination.push_back(std::move(draw));
        }
        else
        {
            draw.kind = DrawKind::IndexedSequence;
            draw.indexedDraws.assign(arguments.begin() + offsets[b],
                                     arguments.begin() + offsets[b] + batch.instances.size());
            destination.push_back(std::move(draw));
        }
    }
}
} // namespace renderlab::renderer
