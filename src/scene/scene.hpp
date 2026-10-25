#pragma once

#include "assets/model.hpp"
#include "math/math.hpp"

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace renderlab::scene
{
/// @brief Parent-relative TRS; rotation is XYZW and zero scale is rejected.
struct Transform
{
    math::Vec3 position{}, scale{1, 1, 1};
    math::Quaternion rotation{};
    math::Mat4 Matrix() const;
};
/// @brief Perspective camera looking along local -Z; vertical FOV is in degrees.
struct Camera
{
    float fov = 60, nearPlane = .1f, farPlane = 100;
};
/// @brief Model asset UUID, optional primitive UUID and explicit user material overrides.
struct MeshRenderer
{
    std::string model, mesh;
    /// Imported material subresource UUID -> user .material asset UUID.
    std::map<std::string, std::string> materials;
};
/// @brief Linear color/intensity; the owning transform emits light along local -Z.
struct DirectionalLight
{
    math::Vec3 color{1, 1, 1};
    float intensity = 1;
    bool enabled = true;
};
/// @brief Stable scene identity with optional components and inherited parent enable state.
struct Object
{
    std::string id, name, parent;
    bool enabled = true;
    Transform transform;
    std::optional<Camera> camera;
    std::optional<MeshRenderer> mesh;
    std::optional<DirectionalLight> light;
};
/// @brief Resolved hierarchy pose; rotation excludes scale for camera and light orientation.
struct WorldTransform
{
    math::Mat4 matrix;
    math::Quaternion rotation;
    bool enabled;
};
/** @brief Editable CPU document. Rendering consumes an extracted value snapshot. */
struct Scene
{
    std::string activeCamera;
    math::Vec3 ambient{.035f, .035f, .035f};
    std::vector<Object> objects;
    Object& Get(const std::string& id);
    const Object& Get(const std::string& id) const;
    /// @brief Validates components/hierarchy and resolves poses; cycles and missing parents throw.
    std::map<std::string, WorldTransform> WorldTransforms() const;
    static Scene Load(const std::filesystem::path& path);
    /// @brief Validates and atomically writes the editable document, excluding render history.
    void Save(const std::filesystem::path& path) const;
    /// Local movement; rotation is camera-local pitch/roll and parent-space yaw, in degrees.
    void MoveCamera(math::Vec3 motion, math::Vec3 rotationDegrees);
};
/// @brief Visible instance value data; previousWorld is from the preceding extraction.
struct Instance
{
    std::string id, model, mesh, material;
    math::Mat4 world, previousWorld, normal;
    math::Bounds bounds;
};
/// @brief Selected light; direction points toward the light, and zero intensity means no direct
/// light.
struct MainLight
{
    std::string object;
    math::Vec3 direction{0, 0, 1}, color{};
    float intensity = 0;
};
/// @brief Self-contained frame values; recording consumes these without reading the Scene document.
struct Snapshot
{
    math::Mat4 view, projection, viewProjection;
    math::Vec3 cameraPosition, ambient;
    MainLight light;
    std::vector<Instance> instances;
    size_t totalInstances = 0, culledInstances = 0;
};
/** @brief Stable identity/history and CPU culling; GPU resources remain the renderer's projection.
 */
class Extractor
{
public:
    /// @brief Selects the first enabled light by UUID, culls bounds and advances instance history.
    Snapshot Extract(const Scene&, float aspect,
                     const std::map<std::string, assets::Model>& models);

private:
    std::map<std::string, math::Mat4> previous_;
};
} // namespace renderlab::scene
