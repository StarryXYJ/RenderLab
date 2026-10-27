#pragma once

#include "renderer/feature.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace renderlab::renderer
{
inline constexpr uint32_t MaxReflectionSamples = 1u << 24;
/// Raw engine-program ABI: row-major matrix followed by two float4 values in a 256-byte slice.
struct RayQueryParameters
{
    std::array<float, 16> inverseViewProjection{};
    std::array<float, 4> directionToLight{}, shadowOptions{};
    std::array<float, 40> reserved{};
};
static_assert(sizeof(RayQueryParameters) == 256 &&
              offsetof(RayQueryParameters, directionToLight) == 64 &&
              offsetof(RayQueryParameters, shadowOptions) == 80);
struct ReflectionParameters
{
    std::array<float, 16> inverseViewProjection{};
    std::array<float, 4> cameraPosition{}, directionToLight{}, lightColor{}, environment{};
    // Sample index, history sample count, directional shadow enabled, sample count limit.
    std::array<float, 4> options{};
    std::array<float, 28> reserved{};
};
static_assert(sizeof(ReflectionParameters) == 256 &&
              offsetof(ReflectionParameters, options) == 128);
/** @brief Opaque scene visibility toward the main directional light, separate from lighting. */
class RayQueryShadowFeature final : public RenderFeature
{
public:
    RayQueryShadowFeature() : RenderFeature("RayTracing.Shadows", {"Deferred.GBuffer"}) {}
    void Build(rendergraph::Graph&, const RenderFrame&) override;
};
/** @brief One specular bounce, stationary accumulation and explicit IBL replacement. */
class RayReflectionFeature final : public RenderFeature
{
public:
    RayReflectionFeature() : RenderFeature("RayTracing.Reflections", {"Deferred.Lighting"}) {}
    void Build(rendergraph::Graph&, const RenderFrame&) override;
};
} // namespace renderlab::renderer
