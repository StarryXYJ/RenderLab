#include "scene/scene.hpp"
#include "assets/json_io.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <stdexcept>

namespace renderlab::scene
{
namespace
{
using Json = nlohmann::json;
Json Array(math::Vec3 v)
{
    return Json::array({v.x, v.y, v.z});
}
math::Vec3 Vector(const Json& j)
{
    const auto v = j.get<std::array<float, 3>>();
    for (float x : v)
        if (!std::isfinite(x))
            throw std::invalid_argument("Nonfinite scene vector");
    return {v[0], v[1], v[2]};
}
void Require(bool value, const char* message)
{
    if (!value)
        throw std::invalid_argument(message);
}
} // namespace
math::Mat4 Transform::Matrix() const
{
    return math::Compose(position, rotation, scale);
}
Object& Scene::Get(const std::string& id)
{
    const auto found = std::ranges::find(objects, id, &Object::id);
    if (found == objects.end())
        throw std::invalid_argument("Missing scene object: " + id);
    return *found;
}
const Object& Scene::Get(const std::string& id) const
{
    return const_cast<Scene*>(this)->Get(id);
}
std::map<std::string, WorldTransform> Scene::WorldTransforms() const
{
    Require(objects.size() <= 10000, "Scene exceeds object limit");
    const auto colorValid = [](math::Vec3 c)
    {
        return std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z) && c.x >= 0 &&
               c.y >= 0 && c.z >= 0;
    };
    Require(colorValid(ambient), "Invalid ambient color");
    Require((environment.empty() || project::ValidId(environment)) &&
                std::isfinite(environmentIntensity) && environmentIntensity >= 0 &&
                environmentIntensity <= 10000 && std::isfinite(environmentRotation),
            "Invalid environment settings");
    Require(std::isfinite(exposure) && exposure >= -16 && exposure <= 16,
            "Exposure must be in [-16,16] stops");
    std::map<std::string, const Object*> ids;
    for (const auto& object : objects)
    {
        Require(project::ValidId(object.id), "Invalid scene object UUID");
        Require(ids.emplace(object.id, &object).second, "Duplicate scene object UUID");
        if (object.light)
        {
            Require(colorValid(object.light->color) && std::isfinite(object.light->intensity) &&
                        object.light->intensity >= 0,
                    "Invalid directional light");
            const auto& light = *object.light;
            Require(light.shadowResolution >= 128 && light.shadowResolution <= 2048 &&
                        (light.shadowResolution & (light.shadowResolution - 1)) == 0 &&
                        std::isfinite(light.shadowBias) && light.shadowBias >= 0 &&
                        light.shadowBias <= .1f && std::isfinite(light.shadowNormalBias) &&
                        light.shadowNormalBias >= 0 && light.shadowNormalBias <= 1,
                    "Invalid directional shadow settings");
        }
        if (object.camera)
            math::Perspective(object.camera->fov, 1, object.camera->nearPlane,
                              object.camera->farPlane);
        if (object.mesh)
        {
            Require(project::ValidId(object.mesh->model) &&
                        (object.mesh->mesh.empty() || project::ValidId(object.mesh->mesh)),
                    "Invalid model/mesh reference");
            for (const auto& [child, material] : object.mesh->materials)
                Require(project::ValidId(child) && project::ValidId(material),
                        "Invalid material override");
        }
    }
    std::map<std::string, WorldTransform> result;
    std::set<std::string> visiting;
    std::function<void(const std::string&, size_t)> visit = [&](const std::string& id, size_t depth)
    {
        if (result.contains(id))
            return;
        Require(depth < 512 && visiting.insert(id).second, "Scene hierarchy cycle/depth limit");
        Require(ids.contains(id), "Missing scene parent");
        const auto& object = *ids.at(id);
        WorldTransform world{object.transform.Matrix(), object.transform.rotation, object.enabled};
        if (!object.parent.empty())
        {
            visit(object.parent, depth + 1);
            const auto& parent = result.at(object.parent);
            world.matrix = parent.matrix * world.matrix;
            world.rotation = parent.rotation * world.rotation;
            world.enabled = world.enabled && parent.enabled;
        }
        result.emplace(id, world);
        visiting.erase(id);
    };
    for (const auto& [id, object] : ids)
    {
        (void)object;
        visit(id, 0);
    }
    return result;
}
Scene Scene::Load(const std::filesystem::path& path)
{
    const auto j = assets::detail::LoadJson(path);
    Require(j.at("version") == 1, "Unsupported scene version");
    Scene scene;
    scene.activeCamera = j.at("activeCamera");
    scene.ambient = Vector(j.value("ambient", Json::array({.035, .035, .035})));
    const auto environment = j.value("environment", Json::object());
    scene.environment = environment.value("image", "");
    scene.environmentIntensity = environment.value("intensity", 1.f);
    scene.environmentRotation = environment.value("rotation", 0.f);
    scene.exposure = j.value("exposure", 0.f);
    for (const auto& input : j.at("objects"))
    {
        Object o;
        o.id = input.at("id");
        o.name = input.value("name", "Object");
        o.parent = input.value("parent", "");
        o.enabled = input.value("enabled", true);
        const auto t = input.value("transform", Json::object());
        o.transform.position = Vector(t.value("position", Json::array({0, 0, 0})));
        o.transform.scale = Vector(t.value("scale", Json::array({1, 1, 1})));
        const auto q = t.value("rotation", Json::array({0, 0, 0, 1})).get<std::array<float, 4>>();
        o.transform.rotation = {q[0], q[1], q[2], q[3]};
        if (input.contains("camera"))
        {
            const auto& c = input.at("camera");
            o.camera = Camera{c.value("fov", 60.f), c.value("near", .1f), c.value("far", 100.f)};
            math::Perspective(o.camera->fov, 1, o.camera->nearPlane, o.camera->farPlane);
        }
        if (input.contains("meshRenderer"))
        {
            const auto& m = input.at("meshRenderer");
            MeshRenderer mesh;
            mesh.model = m.at("model");
            mesh.mesh = m.value("mesh", "");
            mesh.materials = m.value("materials", std::map<std::string, std::string>{});
            Require(project::ValidId(mesh.model), "Invalid model asset UUID");
            for (const auto& [subresource, material] : mesh.materials)
                Require(project::ValidId(subresource) && project::ValidId(material),
                        "Invalid material override UUID");
            o.mesh = std::move(mesh);
        }
        if (input.contains("directionalLight"))
        {
            const auto& light = input.at("directionalLight");
            o.light = DirectionalLight{Vector(light.value("color", Json::array({1, 1, 1}))),
                                       light.value("intensity", 1.f), light.value("enabled", true)};
            o.light->shadows = light.value("shadows", false);
            o.light->shadowResolution = light.value("shadowResolution", 512u);
            o.light->shadowBias = light.value("shadowBias", .001f);
            o.light->shadowNormalBias = light.value("shadowNormalBias", .01f);
            Require(std::isfinite(o.light->intensity) && o.light->intensity >= 0 &&
                        o.light->color.x >= 0 && o.light->color.y >= 0 && o.light->color.z >= 0,
                    "Invalid directional light");
        }
        scene.objects.push_back(std::move(o));
    }
    scene.WorldTransforms();
    Require(scene.Get(scene.activeCamera).camera.has_value(), "Active object must be a camera");
    return scene;
}
void Scene::Save(const std::filesystem::path& path) const
{
    WorldTransforms();
    Json j{{"version", 1},
           {"activeCamera", activeCamera},
           {"ambient", Array(ambient)},
           {"environment",
            {{"image", environment},
             {"intensity", environmentIntensity},
             {"rotation", environmentRotation}}},
           {"exposure", exposure},
           {"objects", Json::array()}};
    for (const auto& o : objects)
    {
        const auto q = o.transform.rotation;
        Json data{{"id", o.id},
                  {"name", o.name},
                  {"parent", o.parent},
                  {"enabled", o.enabled},
                  {"transform",
                   {{"position", Array(o.transform.position)},
                    {"rotation", Json::array({q.x, q.y, q.z, q.w})},
                    {"scale", Array(o.transform.scale)}}}};
        if (o.camera)
            data["camera"] = {
                {"fov", o.camera->fov}, {"near", o.camera->nearPlane}, {"far", o.camera->farPlane}};
        if (o.mesh)
            data["meshRenderer"] = {
                {"model", o.mesh->model}, {"mesh", o.mesh->mesh}, {"materials", o.mesh->materials}};
        if (o.light)
            data["directionalLight"] = {{"color", Array(o.light->color)},
                                        {"intensity", o.light->intensity},
                                        {"enabled", o.light->enabled},
                                        {"shadows", o.light->shadows},
                                        {"shadowResolution", o.light->shadowResolution},
                                        {"shadowBias", o.light->shadowBias},
                                        {"shadowNormalBias", o.light->shadowNormalBias}};
        j["objects"].push_back(std::move(data));
    }
    assets::detail::SaveJson(path, j);
}
void Scene::MoveCamera(math::Vec3 motion, math::Vec3 rotation)
{
    auto& object = Get(activeCamera);
    Require(object.camera.has_value(), "Missing active camera");
    // Yaw uses the parent up axis; pitch/roll use the camera's own axes.
    object.transform.rotation = math::Quaternion::EulerDegrees({0, rotation.y, 0}) *
                                object.transform.rotation *
                                math::Quaternion::EulerDegrees({rotation.x, 0, rotation.z});
    object.transform.position =
        object.transform.position +
        math::Direction(math::Compose({}, object.transform.rotation, {1, 1, 1}), motion);
}
Snapshot Extractor::Extract(const Scene& scene, float aspect,
                            const std::map<std::string, assets::Model>& models)
{
    const auto transforms = scene.WorldTransforms();
    const auto& camera = scene.Get(scene.activeCamera);
    Require(camera.camera.has_value() && transforms.at(camera.id).enabled,
            "Active camera is disabled/missing");
    const auto& pose = transforms.at(camera.id);
    Snapshot snapshot;
    snapshot.cameraPosition = math::Point(pose.matrix, {});
    snapshot.ambient = scene.ambient;
    snapshot.environment = scene.environment;
    snapshot.environmentIntensity = scene.environmentIntensity;
    snapshot.environmentRotation = scene.environmentRotation;
    snapshot.exposure = scene.exposure;
    snapshot.view = math::Inverse(math::Compose(snapshot.cameraPosition, pose.rotation, {1, 1, 1}));
    snapshot.projection = math::Perspective(camera.camera->fov, aspect, camera.camera->nearPlane,
                                            camera.camera->farPlane);
    snapshot.viewProjection = snapshot.projection * snapshot.view;
    // UUID order, independent of document storage order, chooses one active directional light.
    for (const auto& [id, world] : transforms)
    {
        const auto& object = scene.Get(id);
        if (world.enabled && object.light && object.light->enabled)
        {
            snapshot.light = {id,
                              math::Normalize(math::Direction(
                                  math::Compose({}, world.rotation, {1, 1, 1}), {0, 0, 1})),
                              object.light->color,
                              object.light->intensity,
                              object.light->shadows,
                              object.light->shadowResolution,
                              object.light->shadowBias,
                              object.light->shadowNormalBias};
            break;
        }
    }
    std::map<std::string, math::Mat4> next;
    for (const auto& [id, world] : transforms)
    {
        const auto& object = scene.Get(id);
        if (!world.enabled || !object.mesh)
            continue;
        const auto& renderer = *object.mesh;
        const auto& model = models.at(renderer.model);
        for (const auto& [materialId, overrideId] : renderer.materials)
        {
            (void)overrideId;
            Require(std::ranges::any_of(model.materials, [&](const auto& material)
                                        { return material.id == materialId; }),
                    "Unknown imported material subresource");
        }
        bool matched = renderer.mesh.empty();
        const auto append =
            [&](const assets::Mesh& mesh, const math::Mat4& transform, const std::string& node)
        {
            Instance instance;
            instance.id = id + "/" + node + "/" + mesh.id;
            instance.model = model.id;
            instance.mesh = mesh.id;
            instance.world = world.matrix * transform;
            instance.normal = math::Transpose(math::Inverse(instance.world));
            instance.previousWorld =
                previous_.contains(instance.id) ? previous_.at(instance.id) : instance.world;
            next.emplace(instance.id, instance.world);
            instance.bounds = math::TransformBounds(instance.world, mesh.bounds);
            const auto& surface = model.materials.at(mesh.material);
            if (auto found = renderer.materials.find(surface.id); found != renderer.materials.end())
                instance.material = found->second;
            ++snapshot.totalInstances;
            snapshot.allInstances.push_back(instance);
            if (snapshot.light.shadows && snapshot.light.intensity > 0)
                snapshot.shadowCasters.push_back(instance);
            if (!math::Visible(snapshot.viewProjection, instance.bounds))
            {
                ++snapshot.culledInstances;
                return;
            }
            snapshot.instances.push_back(std::move(instance));
        };
        if (!renderer.mesh.empty())
        {
            for (const auto& mesh : model.meshes)
                if (mesh.id == renderer.mesh)
                {
                    matched = true;
                    append(mesh, math::Mat4::Identity(), "");
                }
        }
        else
            for (const auto& instance : model.instances)
                append(model.meshes.at(instance.mesh), instance.transform, instance.id);
        Require(matched, "Unknown model mesh subresource");
    }
    previous_ = std::move(next);
    return snapshot;
}
} // namespace renderlab::scene
