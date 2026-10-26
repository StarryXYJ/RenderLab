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
