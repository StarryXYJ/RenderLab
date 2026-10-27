#include "editor/camera_input.hpp"
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
    try
    {
        LookContracts({});
        LookContracts({20, 40, 0});
        std::cout << "Editor camera look contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
