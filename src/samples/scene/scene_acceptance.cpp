#include "samples/scene/scene_acceptance.hpp"
#include "samples/scene/reflection_temporal_motion.hpp"

#include "assets/database.hpp"
#include "assets/io.hpp"
#include "core/log.hpp"
#include "material/asset.hpp"
#include "renderer/raytracing.hpp"
#include "renderer/scene_renderer.hpp"
#include "rhi/runtime.hpp"
#include "rhi/upload.hpp"
#include "samples/common/check.hpp"
#include "samples/common/image_io.hpp"
#include "shader/compiler.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <fstream>
#include <set>
#include <stdexcept>
#include <vector>

namespace renderlab::samples
{
namespace
{
void MeasurePathMotionSequences(rhi::Runtime& runtime, shader::Target target,
                                const std::filesystem::path& project,
                                const std::filesystem::path& engine,
                                const std::filesystem::path& output, Logger& logger)
{
    constexpr uint32_t width = 128, height = 72, motionFrames = 8, sequenceFrames = 12;
#ifdef NDEBUG
    constexpr uint32_t referenceFrames = 512;
#else
    constexpr uint32_t referenceFrames = 32;
#endif
    const auto pitch = rhi::TextureRowPitch(width);
    const auto image = runtime.CreateTexture(
        {width, height, rhi::Format::Rgba8Srgb, false, true, "PathTracing.Sequence"});
    renderer::SceneRenderer scene(runtime, target, project, engine);
    const auto initialDocument = scene.Document();
    rhi::State state = rhi::State::Undefined;
    renderer::SceneRenderOptions options;
    options.pipeline = renderer::PipelineType::PathTracing;
    const auto render = [&](bool readback)
    {
        auto result = scene.Render(image, state, nullptr, readback, {}, options);
        state = rhi::State::CopySource;
        runtime.Wait(result.completion);
        return result;
    };
    const auto capture =
        [&](const renderer::SceneReadback& frame, const std::filesystem::path& path)
    {
        const auto bytes = frame.pixels->Map();
        std::filesystem::create_directories(path.parent_path());
        SaveImage(path, bytes, width, height, pitch);
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    };
    const std::array<const char*, 3> names{"gentle", "fast", "reveal"};
    for (size_t trajectory = 0; trajectory < names.size(); ++trajectory)
    {
        std::array<std::vector<std::vector<std::byte>>, 2> sequences;
        std::vector<scene::Scene> poses;
        const auto base = output / "path-sequence" / names[trajectory];
        for (size_t mode = 0; mode < sequences.size(); ++mode)
        {
            // Change illumination so the sample sequence and temporal history both restart.
            scene.Document() = initialDocument;
            options.reflectionDenoise = false;
            options.reflectionTemporal = false;
            auto light = std::ranges::find_if(scene.Document().objects, [](const auto& object)
                                              { return object.light.has_value(); });
            detail::Require(light != scene.Document().objects.end(),
                            "Sequence fixture needs a light");
            light->light->intensity *= .5f;
            render(false);
            scene.Document() = initialDocument;
            options.reflectionDenoise = true;
            options.reflectionTemporal = mode == 1;
            for (uint32_t warmup = 0; warmup < 8; ++warmup)
                render(false);
            for (uint32_t frame = 0; frame < sequenceFrames; ++frame)
            {
                if (frame < motionFrames)
                    scene.Document().MoveCamera({trajectory == 0   ? .02f
                                                 : trajectory == 1 ? .08f
                                                                   : .12f,
                                                 0, 0},
                                                {0,
                                                 trajectory == 0   ? .15f
                                                 : trajectory == 1 ? .8f
                                                                   : -.6f,
                                                 0});
                if (mode == 0)
                    poses.push_back(scene.Document());
                const auto result = render(true);
                sequences[mode].push_back(
                    capture(result, base / (mode == 0 ? "spatial" : "temporal") /
                                        std::format("{:02}.bmp", frame)));
            }
        }
        options.reflectionDenoise = false;
        options.reflectionTemporal = false;
        std::vector<std::vector<std::byte>> references;
        for (uint32_t frame = 0; frame < motionFrames; ++frame)
        {
            // Reset even if a repeated pose happens to match the last reference frame.
            scene.Document() = poses[frame];
            auto light = std::ranges::find_if(scene.Document().objects, [](const auto& object)
                                              { return object.light.has_value(); });
            detail::Require(light != scene.Document().objects.end(),
                            "Sequence fixture needs a light");
            light->light->intensity *= .5f;
            render(false);
            scene.Document() = poses[frame];
            renderer::SceneReadback reference;
            for (uint32_t sample = 0; sample < referenceFrames; ++sample)
            {
                reference = render(sample + 1 == referenceFrames);
                detail::Require(reference.reflectionSamples ==
                                    (sample + 1) * renderer::PathSamplesPerFrame,
                                "Path sequence reference failed to accumulate");
            }
            references.push_back(
                capture(reference, base / "reference" / std::format("{:02}.bmp", frame)));
        }
        while (references.size() < sequenceFrames)
            references.push_back(references.back());
        std::array<double, 2> mse{}, residualDelta{}, signedError{}, largeError{}, sphereRoiMse{},
            stoppedMse{};
        constexpr double components = double(width) * height * 3;
        for (uint32_t frame = 0; frame < sequenceFrames; ++frame)
            for (uint32_t y = 0; y < height; ++y)
                for (uint32_t x = 0; x < width; ++x)
                    for (uint32_t channel = 0; channel < 3; ++channel)
                    {
                        const auto pixel = y * pitch + x * 4 + channel;
                        for (size_t mode = 0; mode < sequences.size(); ++mode)
                        {
                            const double error =
                                int(std::to_integer<uint8_t>(sequences[mode][frame][pixel])) -
                                int(std::to_integer<uint8_t>(references[frame][pixel]));
                            mse[mode] += error * error / components / sequenceFrames;
                            if (frame >= motionFrames)
                                stoppedMse[mode] +=
                                    error * error / components / (sequenceFrames - motionFrames);
                            // Fixed image crop covers the sphere cluster, plus adjacent floor.
                            if (x >= 30 && x < 100 && y >= 25 && y < 60)
                                sphereRoiMse[mode] +=
                                    error * error / (70. * 35 * 3) / sequenceFrames;
                            signedError[mode] += error / components / sequenceFrames;
                            largeError[mode] +=
                                (std::abs(error) > 16 ? 1. : 0.) / components / sequenceFrames;
                            if (frame == 0)
                                continue;
                            const double previous =
                                int(std::to_integer<uint8_t>(sequences[mode][frame - 1][pixel])) -
                                int(std::to_integer<uint8_t>(references[frame - 1][pixel]));
                            residualDelta[mode] += (error - previous) * (error - previous) /
                                                   components / (sequenceFrames - 1);
                        }
                    }
        const auto report = std::format(
            "{{\n  \"trajectory\": \"{}\", \"referenceSpp\": {},\n"
            "  \"mse\": [{:.6f}, {:.6f}], \"residualDeltaMse\": [{:.6f}, {:.6f}],\n"
            "  \"signedError\": [{:.6f}, {:.6f}], \"absoluteErrorOver16Fraction\": "
            "[{:.6f}, {:.6f}],\n  \"sphereCropMse\": [{:.6f}, {:.6f}],\n"
            "  \"stoppedMse\": [{:.6f}, {:.6f}]\n}}\n",
            names[trajectory], referenceFrames * renderer::PathSamplesPerFrame, mse[0], mse[1],
            residualDelta[0], residualDelta[1], signedError[0], signedError[1], largeError[0],
            largeError[1], sphereRoiMse[0], sphereRoiMse[1], stoppedMse[0], stoppedMse[1]);
#ifdef NDEBUG
        detail::Require(mse[1] <= mse[0] * 1.1 && residualDelta[1] <= residualDelta[0] * 1.1 &&
                            sphereRoiMse[1] <= sphereRoiMse[0] * 1.1 &&
                            stoppedMse[1] <= stoppedMse[0] * 1.05,
                        "Path temporal sequence significantly worsened against spatial baseline");
#endif
        std::ofstream reportFile(base / "metrics.json", std::ios::binary);
        reportFile.exceptions(std::ios::failbit | std::ios::badbit);
        reportFile << report;
        reportFile.close();
        logger.Write(LogLevel::Info, "Path motion sequence " + report);
    }
}
} // namespace

void VerifyScene(rhi::Runtime& runtime, shader::Target target, const std::filesystem::path& project,
                 const std::filesystem::path& engine, const std::filesystem::path& output,
                 Logger& logger, renderer::PipelineType pipeline, bool validate)
{
    std::filesystem::create_directories(output);
    auto fixture = project;
    if (validate && pipeline == renderer::PipelineType::RayTracing)
    {
        const auto root = output / project::NewId();
        std::filesystem::create_directories(root);
        std::filesystem::copy(project.parent_path() / "Assets", root / "Assets",
                              std::filesystem::copy_options::recursive);
        fixture = root / project.filename();
        std::filesystem::copy_file(project, fixture);
    }
    renderer::SceneRenderer renderer(runtime, target, fixture, engine);
    if (validate && pipeline == renderer::PipelineType::RayTracing &&
        project.stem() == "RayTracing")
    {
        renderer::SceneRenderer pathRenderer(runtime, target, fixture, engine);
        const auto original = pathRenderer.Document();
        auto pathTarget = runtime.CreateTexture(
            {64, 48, rhi::Format::Rgba8Srgb, false, true, "PathTracing.Acceptance"});
        renderer::SceneRenderOptions pathOptions;
        pathOptions.pipeline = renderer::PipelineType::PathTracing;
        pathOptions.captureGraph = true;
        auto state = rhi::State::Undefined;
        const auto pathFrame = [&]()
        {
            auto frame = pathRenderer.Render(pathTarget, state, nullptr, true, {}, pathOptions);
            state = rhi::State::CopySource;
            runtime.Wait(frame.completion);
            return frame;
        };
        const auto first = pathFrame();
        const auto second = pathFrame();
        detail::Require(first.reflectionSamples == renderer::PathSamplesPerFrame &&
                            first.reflectionHistoryReset &&
                            second.reflectionSamples == 2 * renderer::PathSamplesPerFrame &&
                            !second.reflectionHistoryReset,
                        "Path tracing did not accumulate successive samples");
        bool trace = false, accumulate = false, filter = false, temporal = false, raster = false;
        for (const auto& pass : first.graphSnapshot->passes)
        {
            trace |= pass.name == "PathTracing.Trace";
            accumulate |= pass.name == "PathTracing.Accumulate";
            filter |= pass.name == "PathTracing.Denoise";
            temporal |= pass.name == "PathTracing.Temporal";
            raster |= pass.name == "Deferred.GBuffer" || pass.name == "Forward.Opaque";
        }
        detail::Require(trace && accumulate && filter && temporal && !raster,
                        "Path tracing graph composition mismatch");
        pathOptions.reflectionDenoise = false;
        const auto switchedOff = pathFrame();
        detail::Require(switchedOff.reflectionSamples == 3 * renderer::PathSamplesPerFrame &&
                            !switchedOff.reflectionHistoryReset,
                        "Path filter toggle reset the raw accumulation");
        for (const auto& pass : switchedOff.graphSnapshot->passes)
            detail::Require(pass.name != "PathTracing.Denoise" &&
                                pass.name != "PathTracing.Temporal",
                            "Disabled path filter still entered the graph");
        renderer::SceneRenderer rawRenderer(runtime, target, fixture, engine);
        auto rawTarget = runtime.CreateTexture(
            {64, 48, rhi::Format::Rgba8Srgb, false, true, "PathTracing.RawReference"});
        auto rawState = rhi::State::Undefined;
        renderer::SceneReadback raw;
        for (int i = 0; i < 3; ++i)
        {
            raw = rawRenderer.Render(rawTarget, rawState, nullptr, true, {}, pathOptions);
            rawState = rhi::State::CopySource;
            runtime.Wait(raw.completion);
        }
        SaveImage(output / "path-filter-toggle.bmp", switchedOff.pixels->Map(), 64, 48,
                  rhi::TextureRowPitch(64));
        SaveImage(output / "path-raw-reference.bmp", raw.pixels->Map(), 64, 48,
                  rhi::TextureRowPitch(64));
        const auto switchedPixels = switchedOff.pixels->Map();
        const auto rawPixels = raw.pixels->Map();
        detail::Require(switchedPixels.size() == rawPixels.size(), "Path readback size mismatch");
        uint32_t differing = 0;
        for (size_t i = 0; i < rawPixels.size(); ++i)
        {
            const auto difference = std::abs(int(std::to_integer<uint8_t>(switchedPixels[i])) -
                                             int(std::to_integer<uint8_t>(rawPixels[i])));
            detail::Require(difference <= 1, "Path filter toggle changed raw history");
            differing += difference != 0;
        }
        detail::Require(differing <= 4, "Path filter toggle changed raw history broadly");
        // Compare the same moving-camera sequence against a stationary endpoint reference.
        std::array<std::vector<std::byte>, 3> motionImages;
        for (uint32_t mode = 0; mode < motionImages.size(); ++mode)
        {
            renderer::SceneRenderer motionRenderer(
                runtime, target, project.parent_path() / "PathTracingGallery.rlproject", engine);
            auto motionTarget = runtime.CreateTexture(
                {128, 72, rhi::Format::Rgba8Srgb, false, true, "PathTracing.Motion"});
            renderer::SceneRenderOptions motionOptions;
            motionOptions.pipeline = renderer::PipelineType::PathTracing;
            motionOptions.reflectionDenoise = mode != 2;
            motionOptions.reflectionTemporal = mode == 1;
            auto motionState = rhi::State::Undefined;
            renderer::SceneReadback motion;
            auto step = [&]()
            {
                motion = motionRenderer.Render(motionTarget, motionState, nullptr, true, {},
                                               motionOptions);
                motionState = rhi::State::CopySource;
                runtime.Wait(motion.completion);
            };
            for (int frame = 0; frame < 8; ++frame)
                step();
            for (int frame = 0; frame < 16; ++frame)
            {
                motionRenderer.Document().MoveCamera({.02f, 0, 0}, {0, .15f, 0});
                step();
            }
            if (mode == 2)
            {
#ifdef NDEBUG
                constexpr int referenceFrames = 511;
#else
                constexpr int referenceFrames = 63;
#endif
                for (int frame = 0; frame < referenceFrames; ++frame)
                    step();
            }
            const auto pixels = motion.pixels->Map();
            motionImages[mode].assign(pixels.begin(), pixels.end());
            SaveImage(output / (mode == 0   ? "path-motion-spatial.bmp"
                                : mode == 1 ? "path-motion-temporal.bmp"
                                            : "path-motion-reference.bmp"),
                      pixels, 128, 72, rhi::TextureRowPitch(128));
        }
        std::array<double, 2> error{};
        for (size_t mode = 0; mode < error.size(); ++mode)
        {
            for (size_t i = 0; i < motionImages[mode].size(); ++i)
            {
                if (i % 4 == 3)
                    continue;
                const auto delta = int(std::to_integer<uint8_t>(motionImages[mode][i])) -
                                   int(std::to_integer<uint8_t>(motionImages[2][i]));
                error[mode] += delta * delta;
            }
            error[mode] /= 128 * 72 * 3;
        }
        logger.Write(LogLevel::Info, std::format("Path motion MSE: spatial {:.3f}, temporal {:.3f}",
                                                 error[0], error[1]));
#ifdef NDEBUG
        detail::Require(error[1] < error[0], "Path temporal motion increased endpoint MSE");
#endif
        MeasurePathMotionSequences(runtime, target,
                                   project.parent_path() / "PathTracingGallery.rlproject", engine,
                                   output, logger);
        pathOptions.reflectionDenoise = true;
        const auto temporalCold = pathFrame();
        detail::Require(temporalCold.reflectionTemporalFrames == 1,
                        "Path temporal history did not restart after filter toggle");
        pathOptions.reflectionTemporal = false;
        const auto temporalOff = pathFrame();
        detail::Require(!temporalOff.reflectionHistoryReset &&
                            temporalOff.reflectionTemporalFrames == 0,
                        "Path temporal toggle discarded raw history or retained temporal history");
        pathOptions.reflectionTemporal = true;
        const auto temporalRestart = pathFrame();
        detail::Require(!temporalRestart.reflectionHistoryReset &&
                            temporalRestart.reflectionTemporalFrames == 1,
                        "Path temporal re-enable did not restart display history");
        pathRenderer.Document().MoveCamera({.5f, 0, 0}, {0, 5, 0});
        const auto moved = pathFrame();
        detail::Require(
            moved.reflectionSamples == renderer::PathSamplesPerFrame &&
                moved.reflectionHistoryReset && moved.reflectionTemporalFrames == 2,
            "Path tracing camera change did not reset raw history and retain temporal history");
        pathRenderer.Document() = original;
        const auto restored = pathFrame();
        detail::Require(restored.reflectionSamples == renderer::PathSamplesPerFrame &&
                            restored.reflectionHistoryReset,
                        "Path tracing camera restoration did not reset history");
        const auto light = pathRenderer.LastSnapshot().light.object;
        detail::Require(!light.empty(), "Path tracing fixture needs a directional light");
        pathRenderer.Document().Get(light).light->intensity *= .5f;
        const auto changedLight = pathFrame();
        detail::Require(changedLight.reflectionSamples == renderer::PathSamplesPerFrame &&
                            changedLight.reflectionHistoryReset &&
                            changedLight.reflectionTemporalFrames == 1,
                        "Path tracing light change did not reset history");
        pathTarget = runtime.CreateTexture(
            {65, 48, rhi::Format::Rgba8Srgb, false, true, "PathTracing.Resized"});
        state = rhi::State::Undefined;
        const auto resized = pathFrame();
        detail::Require(resized.reflectionSamples == renderer::PathSamplesPerFrame &&
                            resized.reflectionHistoryReset && resized.reflectionTemporalFrames == 1,
                        "Path tracing resize did not reset history");
        pathOptions.exposure = 1.5f;
        const auto exposure = pathFrame();
        detail::Require(exposure.reflectionSamples == 2 * renderer::PathSamplesPerFrame &&
                            !exposure.reflectionHistoryReset,
                        "Path tracing display exposure discarded history");
        pathOptions.reflectionDiagnostics = true;
        bool rejectedDiagnostics = false;
        try
        {
            pathFrame();
        }
        catch (const std::invalid_argument&)
        {
            rejectedDiagnostics = true;
        }
        detail::Require(rejectedDiagnostics,
                        "Path tracing accepted unsupported reflection diagnostics");
        pathOptions.reflectionDiagnostics = false;
        auto& pathDocument = pathRenderer.Document();
        auto changedObject = std::ranges::find_if(pathDocument.objects, [](const auto& object)
                                                  { return object.mesh.has_value(); });
        detail::Require(changedObject != pathDocument.objects.end(),
                        "Path tracing fixture needs a mesh");
        changedObject->transform.position.x += .3f;
        const auto changedScene = pathFrame();
        detail::Require(changedScene.reflectionSamples == renderer::PathSamplesPerFrame &&
                            changedScene.reflectionHistoryReset,
                        "Path tracing scene change did not reset history");
        pathDocument = original;
        pathFrame();
        const auto materialSource =
            std::ranges::find_if(pathDocument.objects, [](const auto& value)
                                 { return value.mesh && !value.mesh->materials.empty(); });
        detail::Require(materialSource != pathDocument.objects.end(),
                        "Path tracing fixture needs a material override");
        const auto materialId = materialSource->mesh->materials.begin()->second;
        bool replaced = false;
        for (auto& entity : pathDocument.objects)
            if (entity.mesh && entity.id != materialSource->id)
            {
                for (const auto& surface : pathRenderer.Models().at(entity.mesh->model).materials)
                    entity.mesh->materials[surface.id] = materialId;
                replaced = true;
                break;
            }
        detail::Require(replaced, "Path tracing fixture needs multiple mesh objects");
        const auto changedMaterial = pathFrame();
        detail::Require(changedMaterial.reflectionSamples == renderer::PathSamplesPerFrame &&
                            changedMaterial.reflectionHistoryReset,
                        "Path tracing material change did not reset history");
    }
    if (pipeline == renderer::PipelineType::RayTracing)
        for (auto& object : renderer.Document().objects)
            if (object.light)
                object.light->shadows = true;
    auto color =
        runtime.CreateTexture({640, 360, rhi::Format::Rgba8Srgb, false, true, "Scene.Acceptance"});
    auto initial = rhi::State::Undefined;
    const auto render = [&](const std::string& name)
    {
        ::renderlab::renderer::SceneRenderOptions options;
        options.pipeline = pipeline;
        options.reflections = false;
        auto result =
            renderer.Render(color, initial, nullptr, true, output / (name + "-plan.json"), options);
        runtime.Wait(result.completion);
        initial = rhi::State::CopySource;
        const auto bytes = result.pixels->Map();
        SaveImage(output / (name + ".bmp"), bytes, 640, 360, rhi::TextureRowPitch(640));
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    };
    const auto baseline = render("scene");
    if (!validate)
    {
        logger.Write(LogLevel::Info, "Scene rendered to " + assets::detail::Text(output));
        return;
    }
    detail::Require(renderer.LastSnapshot().totalInstances > 0 &&
                        !renderer.LastSnapshot().instances.empty(),
                    "Scene renders no visible objects");
    std::set<uint32_t> colors;
    for (size_t i = 0; i + 4 <= baseline.size(); i += 4)
    {
        uint32_t pixel;
        std::memcpy(&pixel, baseline.data() + i, 4);
        colors.insert(pixel);
    }
    detail::Require(colors.size() > 16, "Scene readback lacks shaded geometry");
    auto& document = renderer.Document();
    const auto original = document;
    const auto selected = renderer.LastSnapshot().light.object;
    detail::Require(!selected.empty(), "Fixture requires a directional light");
    if (pipeline == renderer::PipelineType::RayTracing)
    {
        document.Get(selected).light->shadows = false;
        detail::Require(render("ray-shadow-disabled") != baseline,
                        "Ray Query shadows did not change direct-light visibility");
        document = original;
    }
    for (auto& object : document.objects)
        if (object.light && object.id != selected)
        {
            object.light->color = {0, 1, 0};
            object.light->intensity = 30;
        }
    detail::Require(render("secondary-light") == baseline,
                    "Secondary directional light changed output");
    document.Get(selected).light->intensity = 0;
    detail::Require(render("ambient") != baseline, "Main light intensity did not change output");
    document = original;
    document.Get(selected).transform.rotation = math::Quaternion::EulerDegrees({15, 70, 0});
    detail::Require(render("rotated-light") != baseline,
                    "Directional transform did not change lighting");
    document = original;
    document.MoveCamera({.5f, 0, 0}, {0, 5, 0});
    detail::Require(render("moved-camera") != baseline, "Camera transform did not change output");
    document = original;
    document.Save(output / "roundtrip.scene");
    document = scene::Scene::Load(output / "roundtrip.scene");
    detail::Require(render("roundtrip") == baseline, "Scene serialization changed the frame");
    auto object = std::ranges::find_if(document.objects,
                                       [](const auto& value) { return value.mesh.has_value(); });
    object->enabled = false;
    detail::Require(render("disabled-object") != baseline, "Object disable did not change output");
    document = original;
    for (auto& entity : document.objects)
        if (entity.light)
            entity.light->enabled = false;
    detail::Require(render("no-light") != baseline,
                    "Missing lights should disable direct illumination");
    document = original;
    auto mirrored = std::ranges::find_if(document.objects,
                                         [](const auto& value) { return value.mesh.has_value(); });
    mirrored->transform.scale.x = -mirrored->transform.scale.x;
    detail::Require(render("mirrored-object") != baseline,
                    "Mirrored transform did not change output");
    document = original;
    auto sourceObject = *std::ranges::find_if(document.objects, [](const auto& value)
                                              { return value.mesh.has_value(); });
    sourceObject.id = project::NewId();
    sourceObject.transform.position = {0, 1.8f, 0};
    document.objects.push_back(sourceObject);
    detail::Require(render("added-object") != baseline, "New instance did not change output");
    document = original;
    if (pipeline == renderer::PipelineType::RayTracing)
    {
        detail::Require(render("removed-object") == baseline,
                        "Removing a ray-traced instance did not restore the reference");
        const auto containsBuild = [&](const std::string& name)
        {
            return shader::ReadText(output / (name + "-plan.json")).find("RayTracing.BuildTLAS") !=
                   std::string::npos;
        };
        detail::Require(containsBuild("scene") && !containsBuild("secondary-light") &&
                            !containsBuild("moved-camera") && containsBuild("disabled-object") &&
                            containsBuild("added-object"),
                        "Scene TLAS cache did not follow instance and transform changes");
        renderer::SceneRenderOptions options;
        options.pipeline = pipeline;
        options.reflections = false;
        auto first = renderer.Render(color, initial, nullptr, true, {}, options);
        auto changed = std::ranges::find_if(document.objects, [](const auto& value)
                                            { return value.mesh.has_value(); });
        changed->transform.position.x += .75f;
        auto second = renderer.Render(color, rhi::State::CopySource, nullptr, true, {}, options);
        runtime.Wait(second.completion);
        const auto firstPixels = first.pixels->Map();
        const auto secondPixels = second.pixels->Map();
        detail::Require(std::equal(firstPixels.begin(), firstPixels.end(), baseline.begin()) &&
                            !std::equal(secondPixels.begin(), secondPixels.end(), baseline.begin()),
                        "In-flight TLAS versions did not preserve their submitted scene values");
        document = original;
        for (auto& entity : document.objects)
            if (entity.mesh)
                entity.enabled = false;
        detail::Require(render("empty-scene") != baseline, "Empty ray-traced scene did not render");
        document = original;
        renderer::SceneRenderOptions reflectionOptions;
        reflectionOptions.pipeline = pipeline;
        reflectionOptions.profile = true;
        reflectionOptions.shadowResolution = 128;
        const auto reflectionRender =
            [&](const std::string& name, renderer::SceneRenderOptions options)
        {
            auto result = renderer.Render(color, initial, nullptr, true,
                                          output / (name + "-plan.json"), options);
            initial = rhi::State::CopySource;
            return result;
        };
        auto firstReflection = reflectionRender("reflection-first", reflectionOptions);
        const auto firstPreparation = renderer.RayStatistics();
        auto accumulated = reflectionRender("reflection-accumulated", reflectionOptions);
        detail::Require(
            firstPreparation.geometryPacks == 1 && firstPreparation.uploadBatches == 1 &&
                renderer.RayStatistics().geometryPacks == firstPreparation.geometryPacks &&
                renderer.RayStatistics().uploadBatches == firstPreparation.uploadBatches,
            "Static ray surfaces were repacked or reuploaded after their initial batch");
        runtime.Wait(accumulated.completion);
        detail::Require(
            firstReflection.reflectionSamples == 1 && firstReflection.reflectionHistoryReset &&
                accumulated.reflectionSamples == 2 && !accumulated.reflectionHistoryReset,
            "Stationary reflection history did not accumulate across in-flight frames");
        detail::Require(firstReflection.reflectionTemporalFrames == 1 &&
                            accumulated.reflectionTemporalFrames == 2,
                        "Independent temporal history was not published after submission");
        SaveImage(output / "reflection-first.bmp", firstReflection.pixels->Map(), 640, 360,
                  rhi::TextureRowPitch(640));
        SaveImage(output / "reflection-accumulated.bmp", accumulated.pixels->Map(), 640, 360,
                  rhi::TextureRowPitch(640));
        for (uint32_t sample = 3; sample <= 130; ++sample)
        {
            // Exercise the same zero-input camera path as the interactive window after motion.
            document.MoveCamera({}, {});
            auto stationary =
                renderer.Render(color, initial, nullptr, false, {}, reflectionOptions);
            initial = rhi::State::CopySource;
            detail::Require(!stationary.reflectionHistoryReset &&
                                stationary.reflectionSamples == sample,
                            "Static reflection history reset or stopped growing beyond 64 samples");
            if (sample % 2 == 0)
                runtime.Wait(stationary.completion);
        }
        detail::Require(renderer.RayStatistics().geometryPacks == firstPreparation.geometryPacks &&
                            renderer.RayStatistics().uploadBatches ==
                                firstPreparation.uploadBatches,
                        "Stationary ray-surface cache performed repeated work");
        std::string costs;
        uint32_t reflectionPasses = 0;
        for (const auto& timing : firstReflection.profile.Read(runtime))
            if (timing.name.starts_with("RayTracing.Reflection."))
            {
                costs += std::format("{}: {:.6f} ms\n", timing.name, timing.nanoseconds / 1e6);
                ++reflectionPasses;
            }
        detail::Require(
            reflectionPasses == 10,
            "Trace/accumulation/statistics/prepare/temporal/three filters/resolve/combine did not "
            "execute");
        WriteFileAtomically(output / "reflection-gpu-cost.txt",
                            std::as_bytes(std::span{costs.data(), costs.size()}));
        auto rawOptions = reflectionOptions;
        rawOptions.view = renderer::SceneView::ReflectionRaw;
        auto rawFirst = reflectionRender("reflection-raw", rawOptions);
        auto rawSecond = reflectionRender("reflection-raw-repeat", rawOptions);
        runtime.Wait(rawSecond.completion);
        const auto rawPixels = rawFirst.pixels->Map(), repeatedRawPixels = rawSecond.pixels->Map();
        detail::Require(std::ranges::equal(rawPixels, repeatedRawPixels) &&
                            rawSecond.reflectionHistoryReset,
                        "Raw single-sample reflection reference was not deterministic");
        SaveImage(output / "reflection-raw.bmp", rawPixels, 640, 360, rhi::TextureRowPitch(640));
        auto filteredOptions = reflectionOptions;
        filteredOptions.reflectionHistory = false;
        auto filtered = reflectionRender("reflection-denoised-final", filteredOptions);
        filteredOptions.reflectionDenoise = false;
        auto unfiltered = reflectionRender("reflection-unfiltered-final", filteredOptions);
        runtime.Wait(unfiltered.completion);
        detail::Require(!std::ranges::equal(filtered.pixels->Map(), unfiltered.pixels->Map()),
                        "Spatial denoise toggle did not change the low-spp final image");
        filteredOptions.view = renderer::SceneView::ReflectionHistory;
        auto historyOff = reflectionRender("reflection-history-denoise-off", filteredOptions);
        filteredOptions.reflectionDenoise = true;
        auto historyOn = reflectionRender("reflection-history-denoise-on", filteredOptions);
        runtime.Wait(historyOn.completion);
        detail::Require(std::ranges::equal(historyOff.pixels->Map(), historyOn.pixels->Map()),
                        "Spatial denoise contaminated unfiltered single-sample history");
        filteredOptions.reflectionHistory = true;
        filteredOptions.view = renderer::SceneView::ReflectionDenoised;
        auto denoisedView = reflectionRender("reflection-denoised-view", filteredOptions);
        detail::Require(!denoisedView.reflectionHistoryReset && denoisedView.reflectionSamples == 2,
                        "Denoised view switch reset stationary history");
        filteredOptions.reflectionDenoise = false;
        filteredOptions.view = renderer::SceneView::Final;
        auto toggled = reflectionRender("reflection-toggle-preserves-history", filteredOptions);
        detail::Require(!toggled.reflectionHistoryReset && toggled.reflectionSamples == 3,
                        "Spatial denoise toggle reset progressive accumulation");
        runtime.Wait(toggled.completion);
        auto rasterOptions = reflectionOptions;
        rasterOptions.reflections = false;
        auto raster = reflectionRender("reflection-disabled", rasterOptions);
        auto enabled = reflectionRender("reflection-enabled", reflectionOptions);
        runtime.Wait(enabled.completion);
        detail::Require(!std::ranges::equal(raster.pixels->Map(), enabled.pixels->Map()),
                        "Reflection composition did not change the raster/shadow reference");
        SaveImage(output / "reflection-disabled.bmp", raster.pixels->Map(), 640, 360,
                  rhi::TextureRowPitch(640));
        document.MoveCamera({.2f, 0, 0}, {0, 2, 0});
        auto cameraChanged = reflectionRender("reflection-camera-change", reflectionOptions);
        detail::Require(renderer.RayStatistics().geometryPacks == firstPreparation.geometryPacks &&
                            renderer.RayStatistics().uploadBatches ==
                                firstPreparation.uploadBatches,
                        "Camera movement repacked or uploaded static ray surfaces");
        detail::Require(cameraChanged.reflectionHistoryReset,
                        "Camera motion did not invalidate reflection history");
        detail::Require(cameraChanged.reflectionTemporalFrames == 2,
                        "Small camera motion did not preserve a compatible reprojection source");
        document.MoveCamera({}, {});
        auto stoppedCamera = reflectionRender("reflection-camera-stopped", reflectionOptions);
        detail::Require(!stoppedCamera.reflectionHistoryReset &&
                            stoppedCamera.reflectionSamples == 2,
                        "Zero camera input after motion invalidated static reflection history");
        detail::Require(stoppedCamera.reflectionTemporalFrames == 3,
                        "Stopped camera did not preserve temporal source history");
        document.MoveCamera({2, 0, 0}, {});
        auto cut = reflectionRender("reflection-camera-cut", reflectionOptions);
        detail::Require(cut.reflectionTemporalFrames == 1,
                        "Camera cut did not reset the temporal source");
        document = original;
        auto& changedObject = *std::ranges::find_if(document.objects, [](const auto& value)
                                                    { return value.mesh.has_value(); });
        changedObject.transform.position.x += .3f;
        auto sceneChanged = reflectionRender("reflection-scene-change", reflectionOptions);
        detail::Require(sceneChanged.reflectionHistoryReset,
                        "Scene transform did not invalidate reflection history");
        detail::Require(sceneChanged.reflectionTemporalFrames == 1,
                        "Scene transform did not reset temporal history");
        document = original;
        auto beforeMaterial = reflectionRender("reflection-before-material", reflectionOptions);
        const auto beforeMaterialPreparation = renderer.RayStatistics();
        const auto materialSource =
            std::ranges::find_if(document.objects, [](const auto& value)
                                 { return value.mesh && !value.mesh->materials.empty(); });
        detail::Require(materialSource != document.objects.end(),
                        "Reflection fixture needs a material override");
        const auto replacement = materialSource->mesh->materials.begin()->second;
        bool replaced = false;
        for (auto& entity : document.objects)
            if (entity.mesh && entity.id != materialSource->id)
            {
                for (const auto& surface : renderer.Models().at(entity.mesh->model).materials)
                    entity.mesh->materials[surface.id] = replacement;
                replaced = true;
                break;
            }
        detail::Require(replaced, "Reflection fixture needs multiple mesh objects");
        auto materialChanged = reflectionRender("reflection-material-change", reflectionOptions);
        detail::Require(renderer.RayStatistics().geometryPacks ==
                                beforeMaterialPreparation.geometryPacks &&
                            renderer.RayStatistics().uploadBatches ==
                                beforeMaterialPreparation.uploadBatches + 1,
                        "Material-only edit repacked geometry or split its surface upload batch");
        detail::Require(materialChanged.reflectionHistoryReset,
                        "Material change did not invalidate reflection history");
        detail::Require(materialChanged.reflectionTemporalFrames == 1,
                        "Material change did not reset temporal history");
        document = original;
        const auto fixtureProject = project::Project::Open(fixture, engine);
        assets::Database fixtureAssets(fixtureProject);
        const auto materialPath = fixtureAssets.Get(replacement).path;
        shader::CompileOptions shaderOptions;
        shaderOptions.includeDirectories = {engine};
        shaderOptions.cacheDirectory = fixtureProject.library / "CasterTest";
        const auto lit = shader::CompileShader(engine / "RenderLab/Lit.shader", shaderOptions);
        detail::Require(bool(lit), lit.diagnostics);
        const shader::MaterialTemplate litTemplate(lit.package);
        const auto originalMaterial = material::Load(materialPath, litTemplate);
        auto shadowMaterial = originalMaterial;
        material::Set(shadowMaterial, litTemplate, "cast_shadows", std::array<double, 1>{0});
        auto beforeShadowPolicy = reflectionRender("reflection-before-caster", reflectionOptions);
        const auto beforeShadowPreparation = renderer.RayStatistics();
        runtime.Wait(beforeShadowPolicy.completion);
        material::Save(shadowMaterial, materialPath);
        std::filesystem::last_write_time(
            materialPath, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(1));
        auto shadowPolicy = reflectionRender("reflection-caster-disabled", reflectionOptions);
        detail::Require(
            renderer.RayStatistics().geometryPacks == beforeShadowPreparation.geometryPacks &&
                renderer.RayStatistics().uploadBatches == beforeShadowPreparation.uploadBatches,
            "ShadowCaster-only edit unnecessarily repacked or uploaded ray surfaces");
        runtime.Wait(shadowPolicy.completion);
        detail::Require(shadowPolicy.reflectionHistoryReset &&
                            shadowPolicy.reflectionSamples == 1 &&
                            shadowPolicy.reflectionTemporalFrames == 1,
                        "ShadowCaster-only edit did not reset both reflection histories");
        {
            renderer::SceneRenderer fresh(runtime, target, fixture, engine);
            fresh.Document() = document;
            auto reference = fresh.Render(color, initial, nullptr, true, {}, reflectionOptions);
            runtime.Wait(reference.completion);
            detail::Require(std::ranges::equal(shadowPolicy.pixels->Map(), reference.pixels->Map()),
                            "ShadowCaster-only edit differs from a cold renderer reference");
        }
        material::Save(originalMaterial, materialPath);
        std::filesystem::last_write_time(
            materialPath, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(2));
        auto shadowRestored = reflectionRender("reflection-caster-restored", reflectionOptions);
        detail::Require(shadowRestored.reflectionHistoryReset &&
                            shadowRestored.reflectionTemporalFrames == 1,
                        "Restoring ShadowCaster policy did not reset reflection histories");
        auto restored = reflectionRender("reflection-restored", reflectionOptions);
        auto alternateCamera = document.Get(document.activeCamera);
        alternateCamera.id = project::NewId();
        document.objects.push_back(alternateCamera);
        document.activeCamera = alternateCamera.id;
        auto alternate = reflectionRender("reflection-other-view", reflectionOptions);
        detail::Require(alternate.reflectionHistoryReset,
                        "A new ViewID reused another view's history");
        detail::Require(alternate.reflectionTemporalFrames == 1,
                        "A new ViewID reused another view's temporal history");
        document.activeCamera = original.activeCamera;
        auto returned = reflectionRender("reflection-return-view", reflectionOptions);
        detail::Require(!returned.reflectionHistoryReset,
                        "ViewID history was not retained independently");
        const auto addView = [&]
        {
            auto camera = document.Get(original.activeCamera);
            camera.id = project::NewId();
            document.activeCamera = camera.id;
            document.objects.push_back(std::move(camera));
            auto result = reflectionRender("reflection-cache-fill", reflectionOptions);
            runtime.Wait(result.completion);
        };
        addView();
        addView();
        document.activeCamera = original.activeCamera;
        auto touched = reflectionRender("reflection-cache-touch", reflectionOptions);
        runtime.Wait(touched.completion);
        const auto evictions = renderer.RayStatistics().historyEvictions;
        addView();
        detail::Require(renderer.RayStatistics().cachedViews == 4 &&
                            renderer.RayStatistics().historyEvictions == evictions + 1,
                        "Reflection View cache did not enforce its bounded capacity");
        document.activeCamera = original.activeCamera;
        auto recent = reflectionRender("reflection-cache-recent", reflectionOptions);
        detail::Require(!recent.reflectionHistoryReset,
                        "LRU cache evicted the recently touched reflection View");
        document.activeCamera = alternateCamera.id;
        auto oldest = reflectionRender("reflection-cache-oldest", reflectionOptions);
        detail::Require(oldest.reflectionHistoryReset,
                        "LRU cache retained the oldest reflection View after overflow");
        runtime.Wait(oldest.completion);
        document = original;
        auto small = runtime.CreateTexture(
            {320, 180, rhi::Format::Rgba8Srgb, false, true, "Reflection.Resize"});
        auto resized =
            renderer.Render(small, rhi::State::Undefined, nullptr, true, {}, reflectionOptions);
        detail::Require(resized.reflectionHistoryReset,
                        "Resize did not invalidate reflection history");
        detail::Require(resized.reflectionTemporalFrames == 1,
                        "Resize did not reset temporal history");
        document.environmentIntensity += .2f;
        auto environmentChanged =
            reflectionRender("reflection-environment-change", reflectionOptions);
        detail::Require(environmentChanged.reflectionHistoryReset,
                        "Lighting change did not invalidate reflection history");
        detail::Require(environmentChanged.reflectionTemporalFrames == 1,
                        "Lighting change did not reset temporal history");
        for (auto& entity : document.objects)
            if (entity.mesh)
                entity.enabled = false;
        auto emptyReflection = reflectionRender("reflection-empty", reflectionOptions);
        runtime.Wait(emptyReflection.completion);
        document = original;
        VerifyReflectionTemporalMotion(runtime, renderer, color, initial, output, logger);
        logger.Write(LogLevel::Info,
                     "M5.4 reflection acceptance passed: native TraceRays, raw reference, "
                     "stationary accumulation, "
                     "camera/scene/material/size/light invalidation, ViewID isolation, empty scene "
                     "and in-flight history. GPU diagnostic costs:\n" +
                         costs);
        logger.Write(LogLevel::Info,
                     "M5.2 scene acceptance passed: static BLAS reuse, TLAS membership/transform "
                     "updates, camera reuse, empty scene and two in-flight scene versions");
        return;
    }
    auto materialSource =
        std::ranges::find_if(document.objects, [](const auto& value)
                             { return value.mesh && !value.mesh->materials.empty(); });
    const auto overrideMaterials = materialSource->mesh->materials;
    auto destination =
        std::ranges::find_if(document.objects, [](const auto& value)
                             { return value.mesh && value.mesh->materials.empty(); });
    const auto& destinationModel = renderer.Models().at(destination->mesh->model);
    destination->mesh->materials = {
        {destinationModel.materials.front().id, overrideMaterials.begin()->second}};
    detail::Require(render("replaced-material") != baseline,
                    "Material replacement did not change output");
    logger.Write(LogLevel::Info,
                 "M4 scene acceptance passed: model/material/texture, transforms, main-light "
                 "selection/intensity/rotation, camera, object state and serialization");
}
} // namespace renderlab::samples
