#pragma once

#include "assets/model.hpp"
#include "math/math.hpp"

#include <cstddef>
#include <cstdint>
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
    bool operator==(const Transform&) const = default;
    math::Mat4 Matrix() const;
};
/// @brief Perspective camera looking along local -Z; vertical FOV is in degrees.
struct Camera
{
    float fov = 60, nearPlane = .1f, farPlane = 100;
    bool operator==(const Camera&) const = default;
};
/// @brief Model asset UUID, optional primitive UUID and explicit user material overrides.
struct MeshRenderer
{
    std::string model, mesh;
    /// Imported material subresource UUID -> user .material asset UUID.
    std::map<std::string, std::string> materials;
    bool operator==(const MeshRenderer&) const = default;
};
/// @brief Linear color/intensity; the owning transform emits light along local -Z.
struct DirectionalLight
{
    math::Vec3 color{1, 1, 1};
    float intensity = 1;
    bool enabled = true;
    bool shadows = false;
    uint32_t shadowResolution = 512;
    float shadowBias = .001f, shadowNormalBias = .01f;
    bool operator==(const DirectionalLight&) const = default;
};
/// @brief One-sided rectangular emitter in local XY, emitting along local -Z.
/// Radiance is linear RGB; width and height are local-space full extents.
struct RectAreaLight
{
    math::Vec3 radiance{1, 1, 1};
    float width = 1, height = 1;
    bool enabled = true;
    bool operator==(const RectAreaLight&) const = default;
};
/// @brief Isotropic point emitter; intensity is in candela and range is in world units.
struct PointLight
{
    math::Vec3 color{1, 1, 1};
    float intensity = 1, range = 10;
    bool enabled = true;
    bool operator==(const PointLight&) const = default;
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
    std::optional<RectAreaLight> areaLight;
    std::optional<PointLight> pointLight;
    bool operator==(const Object&) const = default;
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
    /// Lat-long sRGB image UUID; empty uses uniform linear ambient radiance.
    std::string environment;
    float environmentIntensity = 1, environmentRotation = 0;
    /// Exposure in stops, applied to linear HDR before the ACES fitted display transform.
    float exposure = 0;
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
    bool shadows = false;
    uint32_t shadowResolution = 512;
    float shadowBias = .001f, shadowNormalBias = .01f;
};
/// @brief World-space parallelogram emitter. Edges are full extents and preserve hierarchy shear.
/// The oriented front normal is -normalize(cross(edgeU, edgeV)); area is world-space.
struct AreaLight
{
    std::string object;
    math::Vec3 center, edgeU, edgeV, normal, radiance;
    float area = 0;
};
/// @brief Extracted isotropic emitter; scale does not change its range or intensity.
struct PointEmitter
{
    std::string object;
    math::Vec3 position, color;
    float intensity = 1, range = 10;
};
/// @brief Self-contained frame values; recording consumes these without reading the Scene document.
struct Snapshot
{
    math::Mat4 view, projection, viewProjection;
    math::Vec3 cameraPosition, ambient;
    std::string environment;
    float environmentIntensity = 1, environmentRotation = 0, exposure = 0;
    MainLight light;
    /// Enabled emitters in stable UUID order; renderer support is separate.
    std::vector<AreaLight> areaLights;
    std::vector<PointEmitter> pointLights;
    std::vector<Instance> instances;
    /// Stable enabled scene instances, independent of camera culling and light/shadow settings.
    std::vector<Instance> allInstances;
    /// Enabled caster candidates when raster shadows are requested. Material roles filter later.
    std::vector<Instance> shadowCasters;
    size_t totalInstances = 0, culledInstances = 0;
    /// When false, instances/culledInstances are unavailable; allInstances remains authoritative.
    bool cpuVisibilityKnown = true;
};
/** @brief Stable identity/history and CPU culling; GPU resources remain the renderer's projection.
 */
class Extractor
{
public:
    struct Statistics
    {
        bool hierarchyRebuilt = false;
        size_t geometryComputed = 0, geometryReused = 0;
    };
    /// @brief Selects the light by UUID, optionally culls bounds and advances extraction history.
    Snapshot Extract(const Scene&, float aspect, const std::map<std::string, assets::Model>& models,
                     bool cpuCulling = true);
    /// Last successful extraction; counters do not read clocks.
    Statistics LastStatistics() const
    {
        return statistics_;
    }

private:
    struct Geometry
    {
        math::Mat4 world, normal;
        math::Bounds localBounds, bounds;
    };
    bool initialized_ = false;
    std::vector<Object> objects_;
    std::map<std::string, WorldTransform> transforms_;
    std::map<std::string, size_t> objectIndices_;
    std::map<std::string, Geometry> previous_;
    Statistics statistics_;
};
} // namespace renderlab::scene
