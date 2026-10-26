#pragma once

#include "renderer/feature.hpp"

#include <array>
#include <cstddef>

namespace renderlab::renderer
{
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
/** @brief Opaque scene visibility toward the main directional light, separate from lighting. */
class RayQueryShadowFeature final : public RenderFeature
{
public:
    RayQueryShadowFeature() : RenderFeature("RayTracing.Shadows", {"Deferred.GBuffer"}) {}
    void Build(rendergraph::Graph&, const RenderFrame&) override;
};
} // namespace renderlab::renderer
