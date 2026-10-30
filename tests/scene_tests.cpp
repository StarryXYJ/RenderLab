#include "scene/scene.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace
{
using namespace renderlab;
constexpr auto CameraId = "00000000000000000000000000000001";
constexpr auto ParentId = "00000000000000000000000000000002";
constexpr auto ChildId = "00000000000000000000000000000003";
constexpr auto OtherId = "00000000000000000000000000000004";
constexpr auto ModelId = "00000000000000000000000000000100";
constexpr auto MeshId = "00000000000000000000000000000101";
constexpr auto SurfaceId = "00000000000000000000000000000102";
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void Fails(F&& work)
{
    bool rejected = false;
    try
    {
        work();
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    Require(rejected, "Invalid scene/model input accepted");
}
struct Fixture
{
    scene::Scene document;
    std::map<std::string, assets::Model> models;
    Fixture()
    {
        scene::Object camera;
        camera.id = CameraId;
        camera.camera = scene::Camera{};
        document.activeCamera = CameraId;
        document.objects.push_back(camera);
        scene::Object parent;
        parent.id = ParentId;
        parent.transform.position = {0, 0, -5};
        parent.transform.scale = {2, 1, .5f};
        document.objects.push_back(parent);
        scene::Object child;
        child.id = ChildId;
        child.parent = ParentId;
        child.transform.rotation = math::Quaternion::EulerDegrees({0, 37, 0});
        child.mesh = scene::MeshRenderer{ModelId};
        document.objects.push_back(child);
        scene::Object other;
        other.id = OtherId;
        other.transform.position = {1, 0, -5};
        other.mesh = scene::MeshRenderer{ModelId};
        document.objects.push_back(other);
        assets::Model model;
        model.id = ModelId;
        model.key = "fixture";
        assets::Mesh mesh;
        mesh.id = MeshId;
        mesh.bounds = {{-.5f, -.5f, -.5f}, {.5f, .5f, .5f}};
        model.meshes.push_back(mesh);
        model.materials.push_back({SurfaceId, "Test"});
        model.instances.push_back({"00000000000000000000000000000103", 0, math::Mat4::Identity()});
        models.emplace(ModelId, std::move(model));
    }
};
const scene::Instance& Child(const scene::Snapshot& snapshot)
{
    const auto it = std::ranges::find_if(snapshot.allInstances, [](const auto& value)
                                         { return value.id.starts_with(ChildId); });
    Require(it != snapshot.allInstances.end(), "Missing child instance");
    return *it;
}
scene::Snapshot Check(scene::Extractor& extractor, const Fixture& fixture, bool culling = true)
{
    auto actual = extractor.Extract(fixture.document, 1, fixture.models, culling);
    scene::Extractor cold;
    const auto expected = cold.Extract(fixture.document, 1, fixture.models, culling);
    Require(actual.viewProjection.values == expected.viewProjection.values &&
                actual.instances.size() == expected.instances.size() &&
                actual.culledInstances == expected.culledInstances &&
                actual.allInstances.size() == expected.allInstances.size(),
            "Cached camera/visibility differs from cold extraction");
    for (size_t i = 0; i < actual.allInstances.size(); ++i)
    {
        const auto& a = actual.allInstances[i];
        const auto& b = expected.allInstances[i];
        Require(a.id == b.id && a.material == b.material && a.world.values == b.world.values &&
                    a.normal.values == b.normal.values && a.bounds == b.bounds,
                "Cached geometry/material differs from cold extraction");
    }
    return actual;
}
void Contracts()
{
    Fixture fixture;
    scene::Extractor extractor;
    auto previous = Check(extractor, fixture);
    Require(extractor.LastStatistics().hierarchyRebuilt &&
                extractor.LastStatistics().geometryComputed == 2,
            "Cold extraction must build both instances");
    Check(extractor, fixture, false);
    Require(!extractor.LastStatistics().hierarchyRebuilt &&
                extractor.LastStatistics().geometryComputed == 0 &&
                extractor.LastStatistics().geometryReused == 2,
            "Static extraction repeated transform/normal/bounds work");
    fixture.document.MoveCamera({.1f, 0, 0}, {0, 2, 0});
    Check(extractor, fixture);
    Require(extractor.LastStatistics().geometryReused == 2,
            "Camera movement invalidated static instance geometry");
    fixture.document.Get(ParentId).transform.position.x = 1;
    auto current = Check(extractor, fixture);
    Require(extractor.LastStatistics().geometryComputed == 1 &&
                Child(current).previousWorld.values == Child(previous).world.values,
            "Parent change failed to update world or preserve previousWorld");
    previous = current;
    fixture.models.at(ModelId).instances[0].transform = math::Compose({0, 1, 0}, {}, {1, 1, 1});
    current = Check(extractor, fixture);
    Require(!extractor.LastStatistics().hierarchyRebuilt &&
                extractor.LastStatistics().geometryComputed == 2 &&
                Child(current).previousWorld.values == Child(previous).world.values,
            "Model-node edits failed to invalidate instance geometry");
    fixture.models.at(ModelId).meshes[0].bounds.maximum.x += 1;
    Check(extractor, fixture);
    Require(extractor.LastStatistics().geometryComputed == 2,
            "In-place mesh bounds edits bypassed cache invalidation");
    fixture.document.Get(ChildId).mesh->materials[SurfaceId] = "00000000000000000000000000000900";
    Check(extractor, fixture);
    Require(extractor.LastStatistics().geometryReused == 2,
            "Material-only edit repeated geometry work");
    fixture.document.Get(ChildId).enabled = false;
    Check(extractor, fixture);
    fixture.document.Get(ChildId).enabled = true;
    current = Check(extractor, fixture);
    Require(Child(current).previousWorld.values == Child(current).world.values,
            "Re-enabled instance retained removed extraction history");
    const auto beforeReorder = current;
    std::reverse(fixture.document.objects.begin(), fixture.document.objects.end());
    current = Check(extractor, fixture);
    Require(extractor.LastStatistics().geometryReused == 2 &&
                Child(current).world.values == Child(beforeReorder).world.values,
            "Document reorder changed instance identity/geometry");
    fixture.document.Get(ParentId).transform.position.x += 2;
    fixture.document.exposure = std::numeric_limits<float>::quiet_NaN();
    Fails([&] { extractor.Extract(fixture.document, 1, fixture.models); });
    fixture.document.exposure = 0;
    current = Check(extractor, fixture);
    Require(Child(current).previousWorld.values == Child(beforeReorder).world.values,
            "Rejected scene advanced previousWorld");
    const auto valid = fixture.document;
    fixture.document.Get(ChildId).parent = ChildId;
    Fails([&] { extractor.Extract(fixture.document, 1, fixture.models); });
    fixture.document = valid;
    fixture.document.Get(ParentId).transform.scale.x = 0;
    Fails([&] { extractor.Extract(fixture.document, 1, fixture.models); });
    fixture.document = valid;
    fixture.document.ambient.x = -1;
    Fails([&] { extractor.Extract(fixture.document, 1, fixture.models); });
    fixture.document = valid;
    fixture.models.at(ModelId).meshes[0].material = 100;
    Fails([&] { extractor.Extract(fixture.document, 1, fixture.models); });
    fixture.models.at(ModelId).meshes[0].material = 0;
    Check(extractor, fixture);
    Require(extractor.LastStatistics().geometryReused == 2,
            "Rejected extraction damaged the last successful geometry cache");
    auto saved = fixture.document.Get(ChildId);
    std::erase_if(fixture.document.objects,
                  [](const auto& object) { return object.id == ChildId; });
    Check(extractor, fixture);
    saved.transform.position.y += 3;
    fixture.document.objects.push_back(saved);
    current = Check(extractor, fixture);
    Require(Child(current).previousWorld.values == Child(current).world.values,
            "Deleted/reinserted instance retained stale history");
}
} // namespace
int main()
{
    try
    {
        Contracts();
        std::cout << "Scene cache contracts passed: edits/model/bounds/history/cancel/reorder\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
