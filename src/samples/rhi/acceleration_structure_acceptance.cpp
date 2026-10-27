#include "samples/rhi/acceleration_structure_acceptance.hpp"
#include "samples/rhi/reflection_temporal_acceptance.hpp"

#include "assets/model.hpp"
#include "core/log.hpp"
#include "renderer/ray_surface.hpp"
#include "renderer/raytracing.hpp"
#include "rendergraph/graph.hpp"
#include "rendergraph/pool.hpp"
#include "rhi/commands.hpp"
#include "rhi/runtime.hpp"
#include "shader/dxc_compiler.hpp"

#include <array>
#include <cmath>
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

void VerifyTraceRays(Runtime& runtime, Pool& pool, shader::Target target,
                     const std::filesystem::path& source,
                     const std::shared_ptr<AccelerationStructure>& hit,
                     const std::shared_ptr<AccelerationStructure>& miss)
{
    if (!runtime.SupportsRayTracingPipeline())
        throw std::runtime_error("--as-test TraceRays requires rayTracingPipeline capability");
    using namespace renderer;
    const auto upload = [&](std::span<const std::byte> bytes, const std::string& name)
    {
        auto buffer = runtime.CreateBuffer({bytes.size(), Memory::Upload, name});
        std::memcpy(buffer->Map().data(), bytes.data(), bytes.size());
        return buffer;
    };
    const std::array<assets::Vertex, 3> vertices{{{{-1, -1, 0}, {0, 0, -1}, {0, 0}},
                                                  {{1, -1, 0}, {0, 0, -1}, {1, 0}},
                                                  {{0, 1, 0}, {0, 0, -1}, {.5f, 1}}}};
    const std::array<uint32_t, 3> indices{0, 1, 2};
    const std::array<RayMeshData, 1> meshes{};
    const std::array<RayInstanceData, 1> instances{};
    std::array<RayMaterialData, 1> materials{};
    materials[0].baseMetallic = {1, 0, 0, 0};
    materials[0].emissionRoughness = {1, 0, 0, .5f};
    const std::array buffers{upload(std::as_bytes(std::span{vertices}), "Trace.Vertices"),
                             upload(std::as_bytes(std::span{indices}), "Trace.Indices"),
                             upload(std::as_bytes(std::span{meshes}), "Trace.Meshes"),
                             upload(std::as_bytes(std::span{instances}), "Trace.Instances"),
                             upload(std::as_bytes(std::span{materials}), "Trace.Materials")};
    constexpr std::array<uint32_t, 5> strides{32, 4, 16, 16, 80};
    auto atlas = runtime.CreateTexture(
        {RayAtlasSize, RayAtlasSize, Format::Rgba8, false, false, "Trace.Atlas"});
    const auto pitch = (RayAtlasSize * 4 + 255) / 256 * 256;
    auto pixels =
        runtime.CreateBuffer({uint64_t(pitch) * RayAtlasSize, Memory::Upload, "Trace.AtlasUpload"});
    auto mapped = pixels->Map();
    std::memset(mapped.data(), 255, mapped.size());
    for (uint32_t y = 0; y < RayAtlasSize; ++y)
        for (uint32_t x = 66; x < 132; ++x)
        {
            auto* texel = mapped.data() + y * pitch + x * 4;
            texel[0] = texel[1] = std::byte{128};
        }
    auto copy = runtime.CreateCommands();
    copy->Barrier(pixels, State::HostWrite, State::CopySource);
    copy->Barrier(atlas, State::Undefined, State::CopyDest);
    copy->CopyTexture(pixels, atlas, pitch, true);
    copy->Barrier(atlas, State::CopyDest, State::Sampled);
    runtime.Wait(runtime.Submit(std::move(copy)));
    auto sampler = runtime.CreateSampler();
    std::vector<BindingDesc> entries{{0, BindingKind::AccelerationStructure},
                                     {1, BindingKind::StorageBuffer},
                                     {10, BindingKind::SampledTexture},
                                     {11, BindingKind::Sampler}};
    for (uint32_t i = 5; i <= 9; ++i)
        entries.push_back({i, BindingKind::ReadOnlyBuffer});
    auto layout = runtime.CreateLayout(entries);
    // Distinct shader objects force a new PSO/table generation. Old work retains the first pair.
    std::vector<std::pair<std::shared_ptr<ShaderTable>, uint64_t>> pending;
    for (uint32_t generation = 0; generation < 2; ++generation)
    {
        shader::DxcCompiler compiler;
        PipelineDesc desc;
        desc.layout = layout;
        const auto compile = [&](ShaderStage stage, const char* entry)
        {
            shader::ProgramRequest request;
            request.entry = {stage, source, entry};
            request.target = target;
            return runtime.CreateShader(stage, compiler.Compile(request), entry);
        };
        desc.rayGeneration = compile(ShaderStage::RayGeneration, "RayGeneration");
        desc.miss = compile(ShaderStage::Miss, "Miss");
        desc.closestHit = compile(ShaderStage::ClosestHit, "ClosestHit");
        auto pipeline = runtime.CreatePipeline(desc);
        auto table = runtime.CreateShaderTable(pipeline);
        for (uint32_t query = 0; query < 2; ++query)
        {
            auto scene = query == 0 ? hit : miss;
            auto result = runtime.CreateBuffer({4, Memory::Device, "Trace.Result", true});
            Graph graph;
            auto as = graph.ImportBuffer("Trace.AS", scene->storage,
                                         {State::AccelerationStructureRead, true});
            auto records = graph.ImportBuffer("Trace.SBT", table->buffer, {State::HostWrite, true});
            auto output = graph.ImportBuffer("Trace.Result", result, {State::Undefined, false});
            std::array<BufferHandle, 5> data;
            for (size_t i = 0; i < buffers.size(); ++i)
                data[i] = graph.ImportBuffer("Trace.Data" + std::to_string(i), buffers[i],
                                             {State::HostWrite, true});
            auto texture = graph.ImportTexture("Trace.Atlas", atlas, {State::Sampled, true});
            auto pass = graph.AddRayTracingPass("Trace.IndexedPBR");
            as = pass.ReadBuffer(as, Use::AccelerationStructureRead);
            records = pass.ReadBuffer(records, Use::ShaderTableRead);
            output = pass.WriteBuffer(output);
            texture = pass.ReadTexture(texture);
            for (auto& buffer : data)
                buffer = pass.ReadBuffer(buffer);
            pass.Execute(
                [=](RayTracingContext& context)
                {
                    context.Bind(pipeline);
                    context.BindAccelerationStructure(0, scene, as);
                    context.BindBuffer(1, output, 0, 4, 4);
                    for (uint32_t i = 0; i < data.size(); ++i)
                        context.BindBuffer(i + 5, data[i], 0, 0, strides[i]);
                    context.BindTexture(10, texture);
                    context.BindSampler(11, sampler);
                    context.TraceRays(table, records, 1, 1);
                });
            graph.Export(output, State::CopySource);
            graph.Compile();
            auto completion = graph.Execute(pool).completion;
            auto readback = runtime.CreateBuffer({4, Memory::Readback, "Trace.Readback"});
            auto read = runtime.CreateCommands();
            read->Copy(result, readback, 4);
            read->Barrier(readback, State::CopyDest, State::HostRead);
            completion = runtime.Submit(std::move(read));
            pending.emplace_back(table, completion);
            runtime.Wait(completion);
            uint32_t value = 0;
            std::memcpy(&value, readback->Map().data(), 4);
            if (value != (query == 0 ? 1u : 0u))
                throw std::runtime_error("TraceRays indexed PBR hit/miss mismatch: " +
                                         std::to_string(value));
        }
    }
}

void VerifyReflectionDenoise(Runtime& runtime, shader::Target target,
                             const std::filesystem::path& source, Logger& logger)
{
    constexpr uint32_t width = 9, height = 7, pitch = 256;
    using Pixel = std::array<float, 4>;
    using Image = std::array<Pixel, width * height>;
    shader::ProgramRequest request;
    request.entry = {ShaderStage::Compute, source, "Denoise"};
    request.target = target;
    if (target == shader::Target::Spirv)
        request.defines["SPIRV"] = "1";
    shader::DxcCompiler compiler;
    PipelineDesc desc;
    desc.layout = runtime.CreateLayout({{0, BindingKind::SampledTexture},
                                        {1, BindingKind::SampledTexture},
                                        {2, BindingKind::StorageTexture},
                                        {3, BindingKind::ConstantBuffer},
                                        {5, BindingKind::SampledTexture},
                                        {6, BindingKind::SampledTexture},
                                        {7, BindingKind::SampledTexture}});
    desc.compute = runtime.CreateShader(ShaderStage::Compute, compiler.Compile(request), "Denoise");
    auto pipeline = runtime.CreatePipeline(desc);
    const auto run = [&](Image radiance, Image normals, Image materials, Image depth, Image guide,
                         uint32_t priorSamples = 0)
    {
        auto commands = runtime.CreateCommands();
        const auto upload = [&](const Image& values, Format format = Format::Rgba32Float)
        {
            auto image =
                runtime.CreateTexture({width, height, format, false, false, "Denoise.Input"});
            auto buffer = runtime.CreateBuffer({pitch * height, Memory::Upload, "Denoise.Upload"});
            for (uint32_t y = 0; y < height; ++y)
                for (uint32_t x = 0; x < width; ++x)
                    std::memcpy(buffer->Map().data() + y * pitch + x * FormatBytes(format),
                                values[y * width + x].data(), FormatBytes(format));
            commands->Barrier(buffer, State::HostWrite, State::CopySource);
            commands->Barrier(image, State::Undefined, State::CopyDest);
            commands->CopyTexture(buffer, image, pitch, true);
            commands->Barrier(image, State::CopyDest, State::Sampled);
            return runtime.CreateView(image);
        };
        auto color = upload(radiance), nr = upload(normals), am = upload(materials),
             depths = upload(depth), hits = upload(guide, Format::R32Float);
        renderer::ReflectionParameters parameters;
        parameters.inverseViewProjection = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        parameters.options[1] = static_cast<float>(priorSamples);
        parameters.options[3] = static_cast<float>(renderer::MaxReflectionSamples);
        auto constants =
            runtime.CreateBuffer({sizeof(parameters), Memory::Upload, "Denoise.Constants"});
        std::memcpy(constants->Map().data(), &parameters, sizeof(parameters));
        Binding uniform{3};
        uniform.buffer = constants;
        uniform.size = sizeof(parameters);
        auto output = runtime.CreateTexture(
            {width, height, Format::Rgba32Float, true, false, "Denoise.Output"});
        auto result = runtime.CreateBuffer({pitch * height, Memory::Readback, "Denoise.Readback"});
        commands->Barrier(constants, State::HostWrite, State::UniformRead);
        commands->Barrier(output, State::Undefined, State::Storage);
        commands->Bind(pipeline);
        commands->Bind(runtime.CreateGroup(desc.layout, {{0, color},
                                                         {1, hits},
                                                         {2, runtime.CreateView(output)},
                                                         uniform,
                                                         {5, nr},
                                                         {6, am},
                                                         {7, depths}}));
        commands->Compute((width + 7) / 8, (height + 7) / 8);
        commands->Barrier(output, State::Storage, State::CopySource);
        commands->CopyTexture(result, output, pitch, false);
        commands->Barrier(result, State::CopyDest, State::HostRead);
        runtime.Wait(runtime.Submit(std::move(commands)));
        Image actual;
        for (uint32_t y = 0; y < height; ++y)
            std::memcpy(actual.data() + y * width, result->Map().data() + y * pitch,
                        width * sizeof(Pixel));
        for (const auto& pixel : actual)
            for (auto channel : pixel)
                if (!std::isfinite(channel))
                    throw std::runtime_error(
                        "Spatial reflection denoiser produced non-finite output");
        return actual;
    };
    Image radiance, normals, materials, depth, guide;
    radiance.fill({2, 3, 4, .5f});
    normals.fill({0, 0, 1, .5f});
    materials.fill({.5f, .5f, .5f, 0});
    depth.fill({.25f, 0, 0, 0});
    guide.fill({2, 0, 0, 0});
    const auto same = [](const Image& a, const Image& b)
    {
        for (size_t i = 0; i < a.size(); ++i)
            for (uint32_t c = 0; c < 4; ++c)
                if (std::abs(a[i][c] - b[i][c]) > 1e-5f)
                    throw std::runtime_error(
                        "Spatial reflection denoiser violated preservation contract");
    };
    same(run(radiance, normals, materials, depth, guide), radiance);
    constexpr uint32_t center = 3 * width + 4;
    radiance[center][0] = 32;
    auto reduced = run(radiance, normals, materials, depth, guide);
    if (reduced[center][0] >= 16 || reduced[center - 1][0] <= 2 || reduced[center][3] != .5f)
        throw std::runtime_error("Spatial reflection denoiser did not reduce an isolated sample");
    same(run(radiance, normals, materials, depth, guide, 2047), radiance);
    auto sharp = normals;
    for (auto& pixel : sharp)
        pixel[3] = .045f;
    same(run(radiance, sharp, materials, depth, guide), radiance);
    auto background = depth;
    background.fill({0, 0, 0, 0});
    same(run(radiance, normals, materials, background, guide), radiance);
    auto unlit = normals;
    for (auto& pixel : unlit)
        pixel[3] = -1;
    same(run(radiance, unlit, materials, depth, guide), radiance);
    // Adjacent constant regions must not bleed across primary geometry discontinuities.
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
            radiance[y * width + x] = x < 4 ? Pixel{1, 1, 1, .5f} : Pixel{8, 8, 8, .5f};
    auto edges = normals;
    auto depthEdges = depth;
    auto hitEdges = guide;
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 4; x < width; ++x)
        {
            edges[y * width + x] = {0, 1, 0, .5f};
            depthEdges[y * width + x][0] = .9f;
            hitEdges[y * width + x][0] = 100;
        }
    same(run(radiance, edges, materials, depth, guide), radiance);
    same(run(radiance, normals, materials, depthEdges, guide), radiance);
    auto flat = run(radiance, normals, materials, depth, guide);
    auto hitBoundary = run(radiance, normals, materials, depth, hitEdges);
    if (hitBoundary[center - 1][0] >= flat[center - 1][0])
        throw std::runtime_error("Reflection hit-distance guide did not protect its boundary");
    logger.Write(LogLevel::Info,
                 "Spatial reflection denoise passed: odd 9x7 extent, constants, "
                 "isolated noise, alpha, sharp/high-spp bypass, background/unlit, normal/depth "
                 "edges and hit-distance guide");
}

void VerifyRaySampling(Runtime& runtime, shader::Target target, const std::filesystem::path& source,
                       Logger& logger)
{
    shader::DxcCompiler compiler;
    shader::ProgramRequest request;
    request.entry = {ShaderStage::Compute, source, "CSMain"};
    request.target = target;
    PipelineDesc desc;
    desc.layout = runtime.CreateLayout({{0, BindingKind::StorageBuffer}});
    desc.compute = runtime.CreateShader(ShaderStage::Compute, compiler.Compile(request), "CSMain");
    auto pipeline = runtime.CreatePipeline(desc);
    constexpr uint32_t bytes = 6 * 4 * sizeof(float);
    auto result = runtime.CreateBuffer({bytes, Memory::Device, "Sampling.Result", true});
    auto readback = runtime.CreateBuffer({bytes, Memory::Readback, "Sampling.Readback"});
    Binding binding{0};
    binding.buffer = result;
    binding.size = bytes;
    binding.stride = 16;
    auto commands = runtime.CreateCommands();
    commands->Barrier(result, State::Undefined, State::Storage);
    commands->Bind(pipeline);
    commands->Bind(runtime.CreateGroup(desc.layout, {binding}));
    commands->Compute(6, 1);
    commands->Barrier(result, State::Storage, State::CopySource);
    commands->Copy(result, readback, bytes);
    commands->Barrier(readback, State::CopyDest, State::HostRead);
    runtime.Wait(runtime.Submit(std::move(commands)));
    std::array<float, 24> values{};
    std::memcpy(values.data(), readback->Map().data(), bytes);
    for (uint32_t i = 0; i < 6; ++i)
    {
        const auto* value = values.data() + i * 4;
        if (!std::isfinite(value[0]) || value[0] <= 0 || value[0] > 1.0001f || value[1] > 1.0001f ||
            value[2] != 0 || value[3] != 0)
            throw std::runtime_error(
                "Visible GGX/Sobol sampling failed its bounded-weight/stratification contract");
    }
    request.entry = {ShaderStage::Compute,
                     source.parent_path().parent_path() / "RenderLab" / "ReflectionPost.hlsl",
                     "Accumulate"};
    if (target == shader::Target::Spirv)
        request.defines["SPIRV"] = "1";
    desc = {};
    desc.layout = runtime.CreateLayout({{0, BindingKind::SampledTexture},
                                        {1, BindingKind::SampledTexture},
                                        {2, BindingKind::StorageTexture},
                                        {3, BindingKind::ConstantBuffer}});
    desc.compute =
        runtime.CreateShader(ShaderStage::Compute, compiler.Compile(request), "Accumulate");
    pipeline = runtime.CreatePipeline(desc);
    constexpr std::array<float, 4> previousPixel{1, 2, 3, .25f}, currentPixel{128, 64, 32, 1};
    const auto texture = [&](const std::array<float, 4>& pixel, const char* name)
    {
        auto image = runtime.CreateTexture({1, 1, Format::Rgba32Float, false, false, name});
        auto upload = runtime.CreateBuffer({256, Memory::Upload, std::string(name) + ".Upload"});
        std::memcpy(upload->Map().data(), pixel.data(), sizeof(pixel));
        auto copy = runtime.CreateCommands();
        copy->Barrier(upload, State::HostWrite, State::CopySource);
        copy->Barrier(image, State::Undefined, State::CopyDest);
        copy->CopyTexture(upload, image, 256, true);
        copy->Barrier(image, State::CopyDest, State::Sampled);
        runtime.Wait(runtime.Submit(std::move(copy)));
        return runtime.CreateView(image);
    };
    auto previous = texture(previousPixel, "Accumulation.Previous");
    auto current = texture(currentPixel, "Accumulation.Current");
    auto image =
        runtime.CreateTexture({1, 1, Format::Rgba32Float, true, false, "Accumulation.Output"});
    auto imageView = runtime.CreateView(image);
    auto pixels = runtime.CreateBuffer({256, Memory::Readback, "Accumulation.Readback"});
    bool initialized = false;
    for (uint32_t count : {0u, 127u, 2047u, renderer::MaxReflectionSamples})
    {
        renderer::ReflectionParameters parameters;
        parameters.options[1] = static_cast<float>(count);
        parameters.options[3] = static_cast<float>(renderer::MaxReflectionSamples);
        auto constants =
            runtime.CreateBuffer({sizeof(parameters), Memory::Upload, "Accumulation.Parameters"});
        std::memcpy(constants->Map().data(), &parameters, sizeof(parameters));
        Binding uniform{3};
        uniform.buffer = constants;
        uniform.size = sizeof(parameters);
        auto dispatch = runtime.CreateCommands();
        dispatch->Barrier(image, initialized ? State::CopySource : State::Undefined,
                          State::Storage);
        dispatch->Barrier(constants, State::HostWrite, State::UniformRead);
        dispatch->Bind(pipeline);
        dispatch->Bind(runtime.CreateGroup(desc.layout,
                                           {{0, current}, {1, previous}, {2, imageView}, uniform}));
        dispatch->Compute(1, 1);
        dispatch->Barrier(image, State::Storage, State::CopySource);
        if (initialized)
            dispatch->Barrier(pixels, State::HostRead, State::CopyDest);
        dispatch->CopyTexture(pixels, image, 256, false);
        dispatch->Barrier(pixels, State::CopyDest, State::HostRead);
        runtime.Wait(runtime.Submit(std::move(dispatch)));
        initialized = true;
        std::array<float, 4> actual{};
        std::memcpy(actual.data(), pixels->Map().data(), sizeof(actual));
        const float blend = count >= renderer::MaxReflectionSamples ? 0.f : 1.f / (count + 1);
        for (uint32_t channel = 0; channel < 4; ++channel)
        {
            const float expected =
                previousPixel[channel] + (currentPixel[channel] - previousPixel[channel]) * blend;
            if (!std::isfinite(actual[channel]) ||
                std::abs(actual[channel] - expected) > 1e-5f * std::max(1.f, std::abs(expected)))
                throw std::runtime_error(
                    "Progressive reflection mean/limit failed beyond 64 samples");
        }
    }
    logger.Write(LogLevel::Info, "Progressive reflection mean passed: reset, 1/128 and 1/2048 "
                                 "weights, and sample count limit");
    logger.Write(LogLevel::Info, "Visible GGX sampling passed: 6 roughness/view cases, 4096 "
                                 "samples each; finite weights <= 1, stratified quadrants");
    VerifyReflectionDenoise(
        runtime, target,
        source.parent_path().parent_path() / "RenderLab" / "ReflectionDenoise.hlsl", logger);
    VerifyReflectionTemporal(
        runtime, target,
        source.parent_path().parent_path() / "RenderLab" / "ReflectionTemporal.hlsl", logger);
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
    VerifyTraceRays(runtime, pool, target, shader.parent_path() / "trace_rays_acceptance.hlsl",
                    centered, moved);
    VerifyRaySampling(runtime, target, shader.parent_path() / "ray_sampling_acceptance.hlsl",
                      logger);
    logger.Write(LogLevel::Info,
                 "M5.3 TraceRays indexed PBR hit/miss and ShaderTable generation rebuild passed");
    logger.Write(
        LogLevel::Info,
        std::format("M5.2 AS/Ray Query passed on {}: BLAS storage={} B, TLAS storage={} B, "
                    "instance add/move/remove and graph build/read declarations validated",
                    target == shader::Target::Dxil ? "DX12" : "Vulkan", bottomSizes.storage,
                    centered->sizes.storage));
}
} // namespace renderlab::samples
