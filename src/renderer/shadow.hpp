#pragma once

#include "scene/scene.hpp"

namespace renderlab::renderer
{
/// @brief Fits a single orthographic 0..1 reversed-depth projection to all enabled bounds.
/// XY centers are snapped to texels; padding protects boundary receivers and degenerate bounds.
math::Mat4 FitDirectionalShadow(const scene::Snapshot& snapshot);
/// @brief Perspective shadow projection from an emitter center along its front normal.
/// Uses a 170-degree field and bounds-derived far plane; receivers outside the frustum are
/// unshadowed.
inline constexpr float AreaShadowNear = .01f, AreaShadowFov = 170.f;
math::Mat4 FitAreaShadow(const scene::AreaLight& light, const scene::Snapshot& snapshot,
                         float* farPlane = nullptr);
} // namespace renderlab::renderer
