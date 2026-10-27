#include "samples/scene/scene_acceptance.hpp"

#include "assets/io.hpp"
#include "core/log.hpp"
#include "renderer/scene_renderer.hpp"
#include "rhi/runtime.hpp"
#include "rhi/upload.hpp"
#include "samples/common/check.hpp"
#include "samples/common/image_io.hpp"

#include <algorithm>
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
    renderer::SceneRenderer renderer(runtime, target, project, engine);
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
        auto accumulated = reflectionRender("reflection-accumulated", reflectionOptions);
        runtime.Wait(accumulated.completion);
        detail::Require(
            firstReflection.reflectionSamples == 1 && firstReflection.reflectionHistoryReset &&
                accumulated.reflectionSamples == 2 && !accumulated.reflectionHistoryReset,
            "Stationary reflection history did not accumulate across in-flight frames");
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
        std::string costs;
        uint32_t reflectionPasses = 0;
        for (const auto& timing : firstReflection.profile.Read(runtime))
            if (timing.name.starts_with("RayTracing.Reflection."))
            {
                costs += std::format("{}: {:.6f} ms\n", timing.name, timing.nanoseconds / 1e6);
                ++reflectionPasses;
            }
        detail::Require(reflectionPasses == 4,
                        "Trace/accumulation/denoise/combine did not execute independently");
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
        detail::Require(cameraChanged.reflectionHistoryReset,
                        "Camera motion did not invalidate reflection history");
        document.MoveCamera({}, {});
        auto stoppedCamera = reflectionRender("reflection-camera-stopped", reflectionOptions);
        detail::Require(!stoppedCamera.reflectionHistoryReset &&
                            stoppedCamera.reflectionSamples == 2,
                        "Zero camera input after motion invalidated static reflection history");
        document = original;
        auto& changedObject = *std::ranges::find_if(document.objects, [](const auto& value)
                                                    { return value.mesh.has_value(); });
        changedObject.transform.position.x += .3f;
        auto sceneChanged = reflectionRender("reflection-scene-change", reflectionOptions);
        detail::Require(sceneChanged.reflectionHistoryReset,
                        "Scene transform did not invalidate reflection history");
        document = original;
        auto beforeMaterial = reflectionRender("reflection-before-material", reflectionOptions);
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
        detail::Require(materialChanged.reflectionHistoryReset,
                        "Material change did not invalidate reflection history");
        document = original;
        auto restored = reflectionRender("reflection-restored", reflectionOptions);
        auto alternateCamera = document.Get(document.activeCamera);
        alternateCamera.id = project::NewId();
        document.objects.push_back(alternateCamera);
        document.activeCamera = alternateCamera.id;
        auto alternate = reflectionRender("reflection-other-view", reflectionOptions);
        detail::Require(alternate.reflectionHistoryReset,
                        "A new ViewID reused another view's history");
        document.activeCamera = original.activeCamera;
        auto returned = reflectionRender("reflection-return-view", reflectionOptions);
        detail::Require(!returned.reflectionHistoryReset,
                        "ViewID history was not retained independently");
        auto small = runtime.CreateTexture(
            {320, 180, rhi::Format::Rgba8Srgb, false, true, "Reflection.Resize"});
        auto resized =
            renderer.Render(small, rhi::State::Undefined, nullptr, true, {}, reflectionOptions);
        detail::Require(resized.reflectionHistoryReset,
                        "Resize did not invalidate reflection history");
        document.environmentIntensity += .2f;
        auto environmentChanged =
            reflectionRender("reflection-environment-change", reflectionOptions);
        detail::Require(environmentChanged.reflectionHistoryReset,
                        "Lighting change did not invalidate reflection history");
        for (auto& entity : document.objects)
            if (entity.mesh)
                entity.enabled = false;
        auto emptyReflection = reflectionRender("reflection-empty", reflectionOptions);
        runtime.Wait(emptyReflection.completion);
        document = original;
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
