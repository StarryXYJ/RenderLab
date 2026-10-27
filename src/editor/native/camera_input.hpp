#pragma once

#include "math/math.hpp"

namespace renderlab::editor
{
/// @brief Converts screen-space look deltas (right/down positive) to a local -Z camera.
inline math::Vec3 CameraLookDelta(float rightDegrees, float downDegrees)
{
    return {-downDegrees, -rightDegrees, 0};
}
} // namespace renderlab::editor
