#include "samples/scene/scene_acceptance.hpp"
#include "samples/scene/reflection_temporal_motion.hpp"

#include "assets/database.hpp"
#include "assets/io.hpp"
#include "core/log.hpp"
#include "material/asset.hpp"
#include "renderer/scene_renderer.hpp"
#include "rhi/runtime.hpp"
#include "rhi/upload.hpp"
#include "samples/common/check.hpp"
#include "samples/common/image_io.hpp"
#include "shader/compiler.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <set>

namespace renderlab::samples
{
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
