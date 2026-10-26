#pragma once

#include "scene/scene.hpp"

namespace renderlab::renderer
{
/// @brief Fits a single orthographic 0..1 reversed-depth projection to all enabled bounds.
/// XY centers are snapped to texels; padding protects boundary receivers and degenerate bounds.
math::Mat4 FitDirectionalShadow(const scene::Snapshot& snapshot);
} // namespace renderlab::renderer
