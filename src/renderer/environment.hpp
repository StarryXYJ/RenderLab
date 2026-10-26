#pragma once

#include "assets/model.hpp"

#include <cstdint>
#include <vector>

namespace renderlab::renderer
{
/// @brief Linear RGBA32F data; atlas tiles are lat-long maps, top to bottom roughness 0..1
/// then cosine-convolved irradiance divided by pi. Single mip/layer, deterministic CPU bake.
struct EnvironmentImage
{
    uint32_t width = 0, height = 0;
    std::vector<float> pixels;
};
inline constexpr uint32_t EnvironmentWidth = 64, EnvironmentHeight = 32, EnvironmentLevels = 6;
/// @brief Null source bakes uniform fallback radiance. Image input is decoded from sRGB.
EnvironmentImage PrefilterEnvironment(const assets::Image* source, math::Vec3 fallback);
/// @brief Integrates the GGX split-sum scale/bias for NdotV (x) and perceptual roughness (y).
EnvironmentImage IntegrateEnvironmentBrdf();
} // namespace renderlab::renderer
