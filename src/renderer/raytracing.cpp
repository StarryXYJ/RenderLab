#include "renderer/raytracing.hpp"

#include "renderer/frame.hpp"
#include "rendergraph/graph.hpp"

#include <stdexcept>

namespace renderlab::renderer
{
void PathTracingFeature::Build(rendergraph::Graph& graph, const RenderFrame& frame)
{
    using namespace rendergraph;
    const auto& input = frame.rayTracing;
    if (!input.scene || !input.reflectionTable || !input.historyPipeline ||
        !input.pathStatisticsPipeline || input.historyRadiance.empty() ||
        input.historyDirectRadiance.empty() || input.historyDiffuseRadiance.empty())
        throw std::invalid_argument("Path tracing requires a scene, shader table and history");
    const auto camera = graph.GetTextureDesc(graph.GetTexture("CameraColor"));
    graph.CreateTexture("PathTracing.RawRadiance",
                        {camera.width, camera.height, rhi::Format::Rgba32Float, true, false});
    graph.CreateTexture("PathTracing.RawDirectRadiance",
                        {camera.width, camera.height, rhi::Format::Rgba32Float, true, false});
    graph.CreateTexture("PathTracing.RawDiffuseRadiance",
                        {camera.width, camera.height, rhi::Format::Rgba32Float, true, false});
    graph.CreateTexture("PathTracing.SurfaceGuide",
                        {camera.width, camera.height, rhi::Format::Rgba32Float, true, false});
    graph.CreateTexture("PathTracing.MaterialGuide",
                        {camera.width, camera.height, rhi::Format::Rgba32Float, true, false});
    const uint64_t momentBytes = uint64_t(camera.width) * camera.height * 8;
    graph.CreateBuffer("PathTracing.BatchMoments", {momentBytes, rhi::Memory::Device, "", true});
    const auto records = graph.ImportBuffer(
        "PathTracing.ShaderTable", input.reflectionTable->buffer, {rhi::State::HostWrite, true});
    auto trace = graph.AddRayTracingPass("PathTracing.Trace");
    const auto scene = trace.ReadBuffer(input.storage, Use::AccelerationStructureRead);
    const auto table = trace.ReadBuffer(records, Use::ShaderTableRead);
    const auto parameters = input.reflectionParameters;
    const auto constants = trace.ReadBuffer(parameters.buffer, Use::Uniform, parameters.range);
    std::vector<BufferHandle> surfaces;
    for (const auto& binding : input.surfaces)
        surfaces.push_back(trace.ReadBuffer(binding.buffer, binding.use, binding.range));
    std::vector<TextureHandle> textures;
    for (const auto& binding : input.reflectionTextures)
        textures.push_back(trace.ReadTexture(binding.texture));
    const auto raw = trace.WriteTexture("PathTracing.RawRadiance");
    const auto rawDirect = trace.WriteTexture("PathTracing.RawDirectRadiance");
    const auto rawDiffuse = trace.WriteTexture("PathTracing.RawDiffuseRadiance");
    const auto surfaceGuide = trace.WriteTexture("PathTracing.SurfaceGuide");
    const auto materialGuide = trace.WriteTexture("PathTracing.MaterialGuide");
    const auto batchMoments = trace.WriteBuffer("PathTracing.BatchMoments");
    trace.Execute(
        [=](RayTracingContext& context)
        {
            context.Bind(input.reflectionTable->pipeline);
            context.BindAccelerationStructure(0, input.scene, scene);
            context.BindTexture(1, raw);
            context.BindTexture(2, surfaceGuide);
            context.BindTexture(3, materialGuide);
            context.BindBuffer(17, batchMoments, 0, momentBytes, 8);
            context.BindTexture(18, rawDirect);
            context.BindTexture(19, rawDiffuse);
            context.BindBuffer(4, constants, parameters.range.offset, parameters.range.size);
            for (size_t i = 0; i < surfaces.size(); ++i)
            {
                const auto& binding = input.surfaces[i];
                context.BindBuffer(binding.slot, surfaces[i], binding.range.offset,
                                   binding.range.size, binding.stride);
            }
            for (size_t i = 0; i < textures.size(); ++i)
                context.BindTexture(input.reflectionTextures[i].slot, textures[i]);
            context.BindSampler(11, input.sampler);
            context.TraceRays(input.reflectionTable, table, camera.width, camera.height);
        });
    auto history = graph.AddComputePass("PathTracing.Accumulate");
    const auto current = history.ReadTexture(raw);
    const auto previous =
        input.previousRadiance.empty() ? current : history.ReadTexture(input.previousRadiance);
    const auto historyConstants =
        history.ReadBuffer(parameters.buffer, Use::Uniform, parameters.range);
    const auto accumulated = history.WriteTexture(input.historyRadiance);
    history.Execute(
        [=](ComputeContext& context)
        {
            context.Bind(input.historyPipeline);
            context.BindTexture(0, current);
            context.BindTexture(1, previous);
            context.BindTexture(2, accumulated);
            context.BindBuffer(3, historyConstants, parameters.range.offset, parameters.range.size);
            context.Dispatch((camera.width + 7) / 8, (camera.height + 7) / 8);
        });
    auto directHistory = graph.AddComputePass("PathTracing.AccumulateDirect");
    const auto directCurrent = directHistory.ReadTexture(rawDirect);
    const auto directPrevious = input.previousDirectRadiance.empty()
                                    ? directCurrent
                                    : directHistory.ReadTexture(input.previousDirectRadiance);
    const auto directConstants =
        directHistory.ReadBuffer(parameters.buffer, Use::Uniform, parameters.range);
    const auto directAccumulated = directHistory.WriteTexture(input.historyDirectRadiance);
    directHistory.Execute(
        [=](ComputeContext& context)
        {
            context.Bind(input.historyPipeline);
            context.BindTexture(0, directCurrent);
            context.BindTexture(1, directPrevious);
            context.BindTexture(2, directAccumulated);
            context.BindBuffer(3, directConstants, parameters.range.offset, parameters.range.size);
            context.Dispatch((camera.width + 7) / 8, (camera.height + 7) / 8);
        });
    auto diffuseHistory = graph.AddComputePass("PathTracing.AccumulateDiffuse");
    const auto diffuseCurrent = diffuseHistory.ReadTexture(rawDiffuse);
    const auto diffusePrevious = input.previousDiffuseRadiance.empty()
                                     ? diffuseCurrent
                                     : diffuseHistory.ReadTexture(input.previousDiffuseRadiance);
    const auto diffuseConstants =
        diffuseHistory.ReadBuffer(parameters.buffer, Use::Uniform, parameters.range);
    const auto diffuseAccumulated = diffuseHistory.WriteTexture(input.historyDiffuseRadiance);
    diffuseHistory.Execute(
        [=](ComputeContext& context)
        {
            context.Bind(input.historyPipeline);
            context.BindTexture(0, diffuseCurrent);
            context.BindTexture(1, diffusePrevious);
            context.BindTexture(2, diffuseAccumulated);
            context.BindBuffer(3, diffuseConstants, parameters.range.offset, parameters.range.size);
            context.Dispatch((camera.width + 7) / 8, (camera.height + 7) / 8);
        });
    auto statistics = graph.AddComputePass("PathTracing.Statistics");
    const auto statisticsCurrent = statistics.ReadTexture(raw);
    const auto batch = statistics.ReadBuffer(batchMoments);
    const auto previousMoments = statistics.ReadBuffer(input.previousStatistics);
    const auto outputMoments = statistics.WriteBuffer(input.historyStatistics);
    const auto statisticsConstants =
        statistics.ReadBuffer(parameters.buffer, Use::Uniform, parameters.range);
    statistics.Execute(
        [=](ComputeContext& context)
        {
            context.Bind(input.pathStatisticsPipeline);
            context.BindTexture(0, statisticsCurrent);
            context.BindBuffer(3, statisticsConstants, parameters.range.offset,
                               parameters.range.size);
            context.BindBuffer(4, batch, 0, momentBytes, 8);
            context.BindBuffer(5, previousMoments, 0,
                               input.previousRadiance.empty() ? 8 : momentBytes, 8);
            context.BindBuffer(6, outputMoments, 0, momentBytes, 8);
            context.Dispatch((camera.width + 7) / 8, (camera.height + 7) / 8);
        });
    if (!input.pathDenoisePipeline)
    {
        graph.SetTexture("PathTracing.DisplayRadiance", accumulated);
        return;
    }
    graph.CreateTexture("PathTracing.FilteredRadiance",
                        {camera.width, camera.height, rhi::Format::Rgba32Float, true, false});
    auto denoise = graph.AddComputePass("PathTracing.Denoise");
    const auto source = denoise.ReadTexture(accumulated);
    const auto direct = denoise.ReadTexture(directAccumulated);
    const auto diffuse = denoise.ReadTexture(diffuseAccumulated);
    const auto surface = denoise.ReadTexture(surfaceGuide);
    const auto material = denoise.ReadTexture(materialGuide);
    const auto moments = denoise.ReadBuffer(outputMoments);
    const auto filterConstants =
        denoise.ReadBuffer(parameters.buffer, Use::Uniform, parameters.range);
    const auto filtered = denoise.WriteTexture("PathTracing.FilteredRadiance");
    denoise.Execute(
        [=](ComputeContext& context)
        {
            context.Bind(input.pathDenoisePipeline);
            context.BindTexture(0, source);
            context.BindTexture(1, surface);
            context.BindTexture(2, filtered);
            context.BindBuffer(3, filterConstants, parameters.range.offset, parameters.range.size);
            context.BindTexture(4, material);
            context.BindBuffer(5, moments, 0, momentBytes, 8);
            context.BindTexture(6, direct);
            context.BindTexture(7, diffuse);
            context.Dispatch((camera.width + 7) / 8, (camera.height + 7) / 8);
        });
    if (!input.temporalPipeline)
    {
        graph.SetTexture("PathTracing.DisplayRadiance", filtered);
        return;
    }
    graph.CreateTexture("PathTracing.TemporalDisplay",
                        {camera.width, camera.height, rhi::Format::Rgba32Float, true, false});
    auto temporal = graph.AddComputePass("PathTracing.Temporal");
    const auto temporalCurrent = temporal.ReadTexture(filtered);
    const auto temporalSurface = temporal.ReadTexture(surfaceGuide);
    const auto temporalMaterial = temporal.ReadTexture(materialGuide);
    const auto temporalFiltered = temporal.ReadTexture(filtered);
    const auto temporalConstants = temporal.ReadBuffer(
        input.temporalParameters.buffer, Use::Uniform, input.temporalParameters.range);
    std::array<TextureHandle, 4> old, output;
    for (size_t i = 0; i < old.size(); ++i)
    {
        old[i] = input.previousTemporal[i].empty()
                     ? temporalCurrent
                     : temporal.ReadTexture(input.previousTemporal[i]);
        output[i] = temporal.WriteTexture(input.temporalOutputs[i]);
    }
    const auto display = temporal.WriteTexture("PathTracing.TemporalDisplay");
    temporal.Execute(
        [=](ComputeContext& context)
        {
            context.Bind(input.temporalPipeline);
            context.BindTexture(0, temporalCurrent);
            context.BindTexture(1, temporalSurface);
            context.BindTexture(2, temporalMaterial);
            context.BindBuffer(3, temporalConstants, input.temporalParameters.range.offset,
                               input.temporalParameters.range.size);
            for (uint32_t i = 0; i < old.size(); ++i)
            {
                context.BindTexture(4 + i, old[i]);
                context.BindTexture(8 + i, output[i]);
            }
            context.BindTexture(12, temporalFiltered);
            context.BindTexture(13, display);
            context.Dispatch((camera.width + 7) / 8, (camera.height + 7) / 8);
        });
    graph.SetTexture("PathTracing.DisplayRadiance", display);
}
void RayQueryShadowFeature::Build(rendergraph::Graph& graph, const RenderFrame& frame)
{
    using namespace rendergraph;
    const auto& input = frame.rayTracing;
    if (!input.scene || !input.shadowPipeline)
        throw std::invalid_argument(
            "Ray Tracing requires a prepared scene AS and Ray Query program");
    const auto camera = graph.GetTextureDesc(graph.GetTexture("CameraColor"));
    graph.CreateTexture("RayTracing.ShadowVisibility",
                        {camera.width, camera.height, rhi::Format::Rgba8, true, false});
    auto pass = graph.AddComputePass("RayTracing.Shadows");
    const auto scene = pass.ReadBuffer(input.storage, Use::AccelerationStructureRead);
    const auto depth = pass.ReadTexture("CameraDepth");
    const auto normal = pass.ReadTexture("GBuffer.NormalRoughness");
    const auto parameters =
        pass.ReadBuffer(input.parameters.buffer, Use::Uniform, input.parameters.range);
    const auto output = pass.WriteTexture("RayTracing.ShadowVisibility");
    pass.Execute(
        [=](ComputeContext& context)
        {
            context.Bind(input.shadowPipeline);
            context.BindAccelerationStructure(0, input.scene, scene);
            context.BindTexture(1, depth);
            context.BindTexture(2, normal);
            context.BindBuffer(3, parameters, input.parameters.range.offset,
                               input.parameters.range.size);
            context.BindTexture(4, output);
            context.Dispatch((camera.width + 7) / 8, (camera.height + 7) / 8);
        });
}
void RayReflectionFeature::Build(rendergraph::Graph& graph, const RenderFrame& frame)
{
    using namespace rendergraph;
    const auto& input = frame.rayTracing;
    if (!input.reflectionTable)
        return;
    const auto camera = graph.GetTextureDesc(graph.GetTexture("CameraColor"));
    graph.CreateTexture("RayTracing.RawRadiance",
                        {camera.width, camera.height, rhi::Format::Rgba32Float, true, false});
    if (input.denoisePipeline)
        graph.CreateTexture("RayTracing.HitGuide",
                            {camera.width, camera.height, rhi::Format::R32Float, true, false});
    const auto records = graph.ImportBuffer("RayTracing.ShaderTable", input.reflectionTable->buffer,
                                            {rhi::State::HostWrite, true});
    auto trace = graph.AddRayTracingPass("RayTracing.Reflection.Trace");
    const auto scene = trace.ReadBuffer(input.storage, Use::AccelerationStructureRead);
    const auto table = trace.ReadBuffer(records, Use::ShaderTableRead);
    const auto depth = trace.ReadTexture("CameraDepth");
    const auto normal = trace.ReadTexture("GBuffer.NormalRoughness");
    const auto albedo = trace.ReadTexture("GBuffer.AlbedoMetallic");
    const auto& parameters = input.reflectionParameters;
    const auto constants = trace.ReadBuffer(parameters.buffer, Use::Uniform, parameters.range);
    std::vector<BufferHandle> surfaces;
    for (const auto& binding : input.surfaces)
        surfaces.push_back(trace.ReadBuffer(binding.buffer, binding.use, binding.range));
    std::vector<TextureHandle> textures;
    for (const auto& binding : input.reflectionTextures)
        textures.push_back(trace.ReadTexture(binding.texture));
    const auto raw = trace.WriteTexture("RayTracing.RawRadiance");
    const auto guide =
        input.denoisePipeline ? trace.WriteTexture("RayTracing.HitGuide") : TextureHandle{};
    trace.Execute(
        [=](RayTracingContext& context)
        {
            context.Bind(input.reflectionTable->pipeline);
            context.BindAccelerationStructure(0, input.scene, scene);
            context.BindTexture(1, raw);
            context.BindTexture(2, depth);
            context.BindTexture(3, normal);
            context.BindBuffer(4, constants, parameters.range.offset, parameters.range.size);
            context.BindTexture(12, albedo);
            if (input.denoisePipeline)
                context.BindTexture(14, guide);
            for (size_t i = 0; i < surfaces.size(); ++i)
            {
                const auto& b = input.surfaces[i];
                context.BindBuffer(b.slot, surfaces[i], b.range.offset, b.range.size, b.stride);
            }
            for (size_t i = 0; i < textures.size(); ++i)
                context.BindTexture(input.reflectionTextures[i].slot, textures[i]);
            context.BindSampler(11, input.sampler);
            context.TraceRays(input.reflectionTable, table, camera.width, camera.height);
        });
    auto history = graph.AddComputePass("RayTracing.Reflection.Accumulate");
    const auto current = history.ReadTexture(raw);
    const auto previous =
        input.previousRadiance.empty() ? current : history.ReadTexture(input.previousRadiance);
    const auto historyConstants =
        history.ReadBuffer(parameters.buffer, Use::Uniform, parameters.range);
    const auto accumulated = history.WriteTexture(input.historyRadiance);
    if (!input.denoisePipeline)
        graph.SetTexture("RayTracing.DenoisedRadiance", accumulated);
    history.Execute(
        [=](ComputeContext& context)
        {
            context.Bind(input.historyPipeline);
            context.BindTexture(0, current);
            context.BindTexture(1, previous);
            context.BindTexture(2, accumulated);
            context.BindBuffer(3, historyConstants, parameters.range.offset, parameters.range.size);
            context.Dispatch((camera.width + 7) / 8, (camera.height + 7) / 8);
        });
    auto statistics = graph.AddComputePass("RayTracing.Reflection.Statistics");
    const auto statisticsRaw = statistics.ReadTexture(raw);
    const auto statisticsPrevious = input.previousRadiance.empty()
                                        ? statisticsRaw
                                        : statistics.ReadTexture(input.previousRadiance);
    const auto oldStatistics = statistics.ReadBuffer(input.previousStatistics);
    const auto newStatistics = statistics.WriteBuffer(input.historyStatistics);
    const auto statisticsNormal = statistics.ReadTexture("GBuffer.NormalRoughness");
    const auto statisticsAlbedo = statistics.ReadTexture("GBuffer.AlbedoMetallic");
    const auto statisticsDepth = statistics.ReadTexture("CameraDepth");
    const auto statisticsBrdf = statistics.ReadTexture(input.reflectionTextures.at(2).texture);
    const auto statisticsUniform =
        statistics.ReadBuffer(parameters.buffer, Use::Uniform, parameters.range);
    statistics.Execute(
        [=](ComputeContext& context)
        {
            context.Bind(input.statisticsPipeline);
            context.BindTexture(0, statisticsRaw);
            context.BindTexture(1, statisticsPrevious);
            context.BindBuffer(2, newStatistics, 0, 0, 8);
            context.BindBuffer(3, statisticsUniform, parameters.range.offset,
                               parameters.range.size);
            context.BindBuffer(4, oldStatistics, 0, 0, 8);
            context.BindTexture(5, statisticsNormal);
            context.BindTexture(6, statisticsAlbedo);
            context.BindTexture(7, statisticsDepth);
            context.BindTexture(8, statisticsBrdf);
            context.Dispatch((camera.width + 7) / 8, (camera.height + 7) / 8);
        });
    auto filtered = accumulated;
    if (input.denoisePipeline && !input.temporalPipeline)
    {
        graph.CreateTexture("RayTracing.DenoisedRadiance",
                            {camera.width, camera.height, rhi::Format::Rgba32Float, true, false});
        auto denoise = graph.AddComputePass("RayTracing.Reflection.Denoise");
        const auto source = denoise.ReadTexture(accumulated);
        const auto hit = denoise.ReadTexture(guide);
        const auto normals = denoise.ReadTexture("GBuffer.NormalRoughness");
        const auto materials = denoise.ReadTexture("GBuffer.AlbedoMetallic");
        const auto depths = denoise.ReadTexture("CameraDepth");
        const auto uniform = denoise.ReadBuffer(parameters.buffer, Use::Uniform, parameters.range);
        filtered = denoise.WriteTexture("RayTracing.DenoisedRadiance");
        const auto destination = filtered;
        denoise.Execute(
            [=](ComputeContext& context)
            {
                context.Bind(input.denoisePipeline);
                context.BindTexture(0, source);
                context.BindTexture(1, hit);
                context.BindTexture(2, destination);
                context.BindBuffer(3, uniform, parameters.range.offset, parameters.range.size);
                context.BindTexture(5, normals);
                context.BindTexture(6, materials);
                context.BindTexture(7, depths);
                context.Dispatch((camera.width + 7) / 8, (camera.height + 7) / 8);
            });
    }
    if (input.temporalPipeline)
    {
        const auto reconstruct = [&](size_t index, const std::string& name, TextureHandle source,
                                     TextureHandle auxiliary)
        {
            // Two versioned scratch images suffice; old versions are dead before reuse.
            const auto outputName = index == 1 || index == 3 ? "RayTracing.Reconstruction.B"
                                                             : "RayTracing.Reconstruction.A";
            if (index < 2)
                graph.CreateTexture(outputName, {camera.width, camera.height,
                                                 rhi::Format::Rgba32Float, true, false});
            auto pass = graph.AddComputePass(name);
            const auto signal = pass.ReadTexture(source);
            const auto extra = pass.ReadTexture(auxiliary);
            const auto normals = pass.ReadTexture("GBuffer.NormalRoughness");
            const auto depths = pass.ReadTexture("CameraDepth");
            const auto materials = pass.ReadTexture("GBuffer.AlbedoMetallic");
            const auto brdf = pass.ReadTexture(input.reflectionTextures.at(2).texture);
            const auto progressive = pass.ReadTexture(accumulated);
            const auto uncertainty = pass.ReadBuffer(newStatistics);
            const auto& uniform = input.temporalParameters;
            const auto constants = pass.ReadBuffer(uniform.buffer, Use::Uniform, uniform.range);
            const auto destination = pass.WriteTexture(outputName);
            pass.Execute(
                [=](ComputeContext& context)
                {
                    context.Bind(input.reconstructionPipelines[index]);
                    context.BindTexture(0, signal);
                    context.BindTexture(1, extra);
                    context.BindTexture(2, destination);
                    context.BindBuffer(3, constants, uniform.range.offset, uniform.range.size);
                    context.BindTexture(4, normals);
                    context.BindTexture(5, depths);
                    context.BindTexture(6, materials);
                    context.BindTexture(7, brdf);
                    context.BindTexture(8, progressive);
                    context.BindBuffer(9, uncertainty, 0, 0, 8);
                    context.Dispatch((camera.width + 7) / 8, (camera.height + 7) / 8);
                });
            if (index == 4)
                graph.SetTexture("RayTracing.DenoisedRadiance", destination);
            return destination;
        };
        const auto prepared = reconstruct(0, "RayTracing.Reflection.Prepare", raw, guide);
        graph.CreateTexture("RayTracing.Temporal.Diagnostics",
                            {input.reflectionDiagnostics ? camera.width : 1,
                             input.reflectionDiagnostics ? camera.height : 1,
                             rhi::Format::Rgba32Float, true, false});
        auto temporal = graph.AddComputePass("RayTracing.Reflection.Temporal");
        const auto diagnostic = temporal.WriteTexture("RayTracing.Temporal.Diagnostics");
        const auto source = temporal.ReadTexture(prepared);
        const auto normals = temporal.ReadTexture("GBuffer.NormalRoughness");
        const auto depths = temporal.ReadTexture("CameraDepth");
        const auto hit = temporal.ReadTexture(prepared);
        std::array<TextureHandle, 4> previousTemporal{source, hit, normals, source};
        for (size_t i = 0; i < previousTemporal.size(); ++i)
            if (!input.previousTemporal[i].empty())
                previousTemporal[i] = temporal.ReadTexture(input.previousTemporal[i]);
        const auto& uniform = input.temporalParameters;
        const auto temporalConstants =
            temporal.ReadBuffer(uniform.buffer, Use::Uniform, uniform.range);
        std::array<TextureHandle, 4> outputs;
        for (size_t i = 0; i < outputs.size(); ++i)
            outputs[i] = temporal.WriteTexture(input.temporalOutputs[i]);
        temporal.Execute(
            [=](ComputeContext& context)
            {
                context.Bind(input.temporalPipeline);
                context.BindTexture(0, source);
                context.BindTexture(1, previousTemporal[0]);
                context.BindTexture(2, outputs[0]);
                context.BindBuffer(3, temporalConstants, uniform.range.offset, uniform.range.size);
                context.BindTexture(4, previousTemporal[1]);
                context.BindTexture(5, previousTemporal[2]);
                context.BindTexture(6, hit);
                context.BindTexture(7, depths);
                context.BindTexture(8, normals);
                context.BindTexture(9, outputs[1]);
                context.BindTexture(10, outputs[2]);
                context.BindTexture(11, previousTemporal[3]);
                context.BindTexture(12, outputs[3]);
                context.BindTexture(13, diagnostic);
                context.Dispatch((camera.width + 7) / 8, (camera.height + 7) / 8);
            });
        filtered = outputs[0];
        for (size_t scale = 1; scale <= 3; ++scale)
            filtered = reconstruct(scale, "RayTracing.Reflection.Filter" + std::to_string(scale),
                                   filtered, outputs[3]);
        filtered = reconstruct(4, "RayTracing.Reflection.Resolve", filtered, accumulated);
    }
    graph.CreateTexture("RayTracing.Composite",
                        {camera.width, camera.height, rhi::Format::Rgba32Float, true, true});
    auto combine = graph.AddComputePass("RayTracing.Reflection.Combine");
    const auto radiance = combine.ReadTexture(filtered);
    const auto color = combine.ReadTexture("CameraColor");
    const auto combineNormal = combine.ReadTexture("GBuffer.NormalRoughness");
    const auto combineAlbedo = combine.ReadTexture("GBuffer.AlbedoMetallic");
    const auto combineDepth = combine.ReadTexture("CameraDepth");
    const auto occlusion = combine.ReadTexture("GBuffer.EmissionOcclusion");
    const auto environment = combine.ReadTexture(input.reflectionTextures.at(1).texture);
    const auto brdf = combine.ReadTexture(input.reflectionTextures.at(2).texture);
    const auto combineConstants =
        combine.ReadBuffer(parameters.buffer, Use::Uniform, parameters.range);
    const auto output = combine.WriteTexture("RayTracing.Composite");
    combine.Execute(
        [=](ComputeContext& context)
        {
            context.Bind(input.combinePipeline);
            context.BindTexture(0, radiance);
            context.BindTexture(2, output);
            context.BindBuffer(3, combineConstants, parameters.range.offset, parameters.range.size);
            context.BindTexture(4, color);
            context.BindTexture(5, combineNormal);
            context.BindTexture(6, combineAlbedo);
            context.BindTexture(7, combineDepth);
            context.BindTexture(8, environment);
            context.BindTexture(9, brdf);
            context.BindTexture(10, occlusion);
            context.Dispatch((camera.width + 7) / 8, (camera.height + 7) / 8);
        });
    graph.SetTexture("CameraColor", output);
}
} // namespace renderlab::renderer
