#pragma once

#include "scene/scene.hpp"

#include <array>
#include <span>
#include <stdexcept>

namespace renderlab::renderer
{
/// @brief Shared raster and path emitter layout: one header and three float4s per emitter.
/// Zero-initialized output preserves existing rendering for a scene without emitters.
inline constexpr size_t MaxRectLights = 2;
using RectLightConstants = std::array<float, 28>;
inline RectLightConstants PackRectLights(std::span<const scene::AreaLight> lights)
{
    if (lights.size() > MaxRectLights)
        throw std::invalid_argument("Rendering supports at most two rectangular area lights");
    RectLightConstants result{};
    result[0] = static_cast<float>(lights.size());
    for (size_t i = 0; i < lights.size(); ++i)
    {
        const auto& light = lights[i];
        const size_t offset = 4 + i * 12;
        result[offset] = light.center.x;
        result[offset + 1] = light.center.y;
        result[offset + 2] = light.center.z;
        result[offset + 3] = light.radiance.x;
        result[offset + 4] = light.edgeU.x;
        result[offset + 5] = light.edgeU.y;
        result[offset + 6] = light.edgeU.z;
        result[offset + 7] = light.radiance.y;
        result[offset + 8] = light.edgeV.x;
        result[offset + 9] = light.edgeV.y;
        result[offset + 10] = light.edgeV.z;
        result[offset + 11] = light.radiance.z;
    }
    return result;
}
// Path tracing reserves this layout inside its existing constants ABI.
inline constexpr size_t MaxPathRectLights = MaxRectLights;
using PathRectLightConstants = RectLightConstants;
inline PathRectLightConstants PackPathRectLights(std::span<const scene::AreaLight> lights)
{
    return PackRectLights(lights);
}
} // namespace renderlab::renderer
