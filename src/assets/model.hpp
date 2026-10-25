#pragma once

#include "assets/database.hpp"
#include "math/math.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace renderlab::assets
{
/// @brief Shared static-mesh vertex layout: object-space position/normal and UV0.
struct Vertex
{
    math::Vec3 position, normal;
    std::array<float, 2> uv{};
};
static_assert(sizeof(Vertex) == 32);
/// @brief Tightly packed RGBA8 pixels; color textures are uploaded with sRGB decoding.
struct Image
{
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;
};
/// @brief Imported Opaque base-color data; image indexes Model::images, or -1 means white.
struct Surface
{
    std::string id, name;
    std::array<float, 4> color{1, 1, 1, 1};
    int image = -1;
    bool doubleSided = false;
};
/// @brief One indexed triangle primitive; material indexes Model::materials.
struct Mesh
{
    std::string id, name;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    math::Bounds bounds;
    uint32_t material = 0;
};
/// @brief A selected glTF node primitive; transform is relative to the model root.
struct ModelInstance
{
    std::string id;
    uint32_t mesh = 0;
    math::Mat4 transform = math::Mat4::Identity();
};
/// @brief CPU import result; child IDs persist in metadata while key identifies cached content.
struct Model
{
    std::string id, key;
    std::vector<Mesh> meshes;
    std::vector<Surface> materials;
    std::vector<Image> images;
    std::vector<ModelInstance> instances;
    bool cacheHit = false;
};
/// @brief Decodes an encoded image through WIC; rejects invalid or oversized dimensions.
Image DecodeImage(const std::vector<std::byte>& bytes);
/** @brief Imports static triangles or restores a validated content cache.
 * Updates metadata for dependencies and child identities; unsupported glTF features fail
 * explicitly.
 */
Model LoadModel(Database& database, const std::string& id);
} // namespace renderlab::assets
