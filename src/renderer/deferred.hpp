#pragma once

#include "renderer/feature.hpp"

namespace renderlab::renderer
{
/** @brief Three-target material GBuffer; shared CameraDepth remains the visibility attachment. */
class GBufferFeature final : public RenderFeature
{
public:
    GBufferFeature() : RenderFeature("Deferred.GBuffer") {}
    void Build(rendergraph::Graph&, const RenderFrame&) override;
};

/** @brief Fullscreen lighting over the declared GBuffer and scene lighting inputs. */
class DeferredLightingFeature final : public RenderFeature
{
public:
    DeferredLightingFeature() : RenderFeature("Deferred.Lighting", {"Deferred.GBuffer"}) {}
    void Build(rendergraph::Graph&, const RenderFrame&) override;
};
} // namespace renderlab::renderer
