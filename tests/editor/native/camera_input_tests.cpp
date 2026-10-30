#include "editor/native/camera_input.hpp"
#include "editor/native/snapshot_store.hpp"
#include "editor/native/viewport_config.hpp"
#include "scene/scene.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace renderlab;
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
void LookContracts(math::Vec3 initialAngles)
{
    scene::Scene document;
    scene::Object camera;
    camera.id = "camera";
    camera.camera.emplace();
    camera.transform.rotation = math::Quaternion::EulerDegrees(initialAngles);
    document.activeCamera = camera.id;
    document.objects.push_back(camera);
    const auto original = camera.transform;
    const auto pose = original.Matrix();
    const auto forward = math::Direction(pose, {0, 0, -1});
    const auto right = math::Direction(pose, {1, 0, 0});
    const auto up = math::Direction(pose, {0, 1, 0});
    for (const float sign : {-1.f, 1.f})
    {
        document.Get(camera.id).transform = original;
        document.MoveCamera({}, editor::CameraLookDelta(sign * 5, 0));
        auto actual = math::Direction(document.Get(camera.id).transform.Matrix(), {0, 0, -1});
        Require(sign * math::Dot(actual - forward, right) > 0,
                "Right/left input must turn the camera toward that side");
        document.Get(camera.id).transform = original;
        document.MoveCamera({}, editor::CameraLookDelta(0, sign * 5));
        actual = math::Direction(document.Get(camera.id).transform.Matrix(), {0, 0, -1});
        Require(sign * math::Dot(actual - forward, up) < 0,
                "Screen down/up input must look down/up");
        document.MoveCamera({0, 0, -1}, {});
        const auto movement = document.Get(camera.id).transform.position - original.position;
        Require(math::Dot(movement, actual) > .99f,
                "Forward movement must follow the resulting view direction");
    }
}
} // namespace

int main()
{
    editor::SnapshotStore snapshots;
    Require(snapshots.Acquire().first == 0, "Empty snapshot must remain unavailable");
    Require(snapshots.Publish("first"), "Small snapshot rejected");
    const auto pin = snapshots.Acquire();
    snapshots.Publish("second");
    Require(snapshots.Acquire() == pin, "Repeated acquire invalidated the outstanding pin");
    char value[16]{};
    Require(!snapshots.Copy(pin.first, value, 1), "Short copy must fail without consuming pin");
    Require(snapshots.Copy(pin.first, value, sizeof(value)) && std::string(value) == "first",
            "Live publication changed a pinned snapshot");
    Require(!snapshots.Publish(std::string(editor::SnapshotStore::Budget, 'X')),
            "Oversized snapshot exceeded the budget");
    Require(snapshots.Release(pin.first) && !snapshots.Copy(pin.first, value, sizeof(value)),
            "Released snapshot ID remained usable");
    try
    {
        LookContracts({});
        LookContracts({20, 40, 0});
        scene::Snapshot source;
        source.light.direction = {0, 1, 0};
        source.light.color = {1, .5f, .25f};
        source.light.intensity = 2;
        const auto defaults = editor::DefaultConfig(source, 60, false, true);
        editor::ValidateConfig(defaults);
        auto candidate = defaults;
        candidate.light_intensity = 3;
        candidate.flags |= 16;
        const auto options = editor::RenderOptions(candidate, source.light);
        Require(source.light.intensity == 2 && options.lightOverride->intensity == 3 &&
                    options.lightOverride->shadows,
                "Runtime overrides modified source lighting");
        for (uint32_t variant = 0; variant < 6; ++variant)
        {
            candidate = defaults;
            if (variant == 0)
                candidate.size = 1;
            if (variant == 1)
                candidate.recording_threads = 17;
            if (variant == 2)
                candidate.shadow_resolution = 192;
            if (variant == 3)
                candidate.flags = 32;
            if (variant == 4)
                candidate.light_x = candidate.light_y = candidate.light_z = 0;
            if (variant == 5)
            {
                candidate.pipeline = 1;
                candidate.view = 7;
            }
            bool rejected = false;
            try
            {
                editor::ValidateConfig(candidate);
            }
            catch (const std::invalid_argument&)
            {
                rejected = true;
            }
            Require(rejected, "Invalid runtime configuration was accepted");
        }
        std::cout << "Editor camera look contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
