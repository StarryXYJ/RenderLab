#pragma once

#include <array>
#include <cstdint>

namespace renderlab::renderer
{
inline constexpr uint32_t RayAtlasTileSize = 64, RayAtlasTilePitch = 66, RayAtlasColumns = 8,
                          RayAtlasSize = RayAtlasTilePitch * RayAtlasColumns;
struct RayMeshData
{
    uint32_t vertexOffset = 0, indexOffset = 0, reserved0 = 0, reserved1 = 0;
};
struct RayInstanceData
{
    uint32_t mesh = 0, material = 0, reserved0 = 0, reserved1 = 0;
};
struct RayMaterialData
{
    std::array<float, 4> baseMetallic{1, 1, 1, 0}, emissionRoughness{0, 0, 0, .5f};
    std::array<uint32_t, 4> maps{};
    std::array<uint32_t, 4> normalFlags{1, 0, 0, 0};
    std::array<float, 4> options{1, 1, 0, 0};
};
static_assert(sizeof(RayMeshData) == 16 && sizeof(RayInstanceData) == 16 &&
              sizeof(RayMaterialData) == 80);
} // namespace renderlab::renderer
