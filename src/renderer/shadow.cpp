#include "renderer/shadow.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace renderlab::renderer
{
math::Mat4 FitDirectionalShadow(const scene::Snapshot& snapshot)
{
    if (snapshot.shadowCasters.empty())
        return math::Mat4::Identity();
    const auto z = math::Normalize(snapshot.light.direction);
    const auto x = math::Normalize(
        math::Cross(std::abs(z.y) > .95f ? math::Vec3{1, 0, 0} : math::Vec3{0, 1, 0}, z));
    const auto y = math::Cross(z, x);
    auto basis = math::Mat4::Identity();
    for (size_t column = 0; column < 3; ++column)
    {
        basis.At(0, column) = std::array{x.x, x.y, x.z}[column];
        basis.At(1, column) = std::array{y.x, y.y, y.z}[column];
        basis.At(2, column) = std::array{z.x, z.y, z.z}[column];
    }
    std::array<float, 3> low, high;
    low.fill(std::numeric_limits<float>::infinity());
    high.fill(-std::numeric_limits<float>::infinity());
    for (const auto& instance : snapshot.shadowCasters)
    {
        const auto bounds = math::TransformBounds(basis, instance.bounds);
        const std::array a{bounds.minimum.x, bounds.minimum.y, bounds.minimum.z};
        const std::array b{bounds.maximum.x, bounds.maximum.y, bounds.maximum.z};
        for (size_t axis = 0; axis < 3; ++axis)
        {
            low[axis] = std::min(low[axis], a[axis]);
            high[axis] = std::max(high[axis], b[axis]);
        }
    }
    auto projection = math::Mat4::Identity();
    for (size_t axis = 0; axis < 3; ++axis)
    {
        const auto margin = std::max(.1f, (high[axis] - low[axis]) * .05f);
        low[axis] -= margin;
        high[axis] += margin;
        const auto extent = high[axis] - low[axis];
        auto center = (low[axis] + high[axis]) * .5f;
        if (axis < 2)
        {
            const auto texel = extent / float(snapshot.light.shadowResolution);
            center = std::round(center / texel) * texel;
        }
        projection.At(axis, axis) = (axis == 2 ? 1.f : 2.f) / extent;
        projection.At(axis, 3) = -center * projection.At(axis, axis) + (axis == 2 ? .5f : 0.f);
    }
    return projection * basis;
}
math::Mat4 FitAreaShadow(const scene::AreaLight& light, const scene::Snapshot& snapshot)
{
    const auto x = math::Normalize(light.edgeU);
    const auto z = light.normal * -1.f;
    const auto y = math::Normalize(math::Cross(z, x));
    auto view = math::Mat4::Identity();
    const std::array axes{x, y, z};
    for (size_t row = 0; row < 3; ++row)
    {
        view.At(row, 0) = axes[row].x;
        view.At(row, 1) = axes[row].y;
        view.At(row, 2) = axes[row].z;
        view.At(row, 3) = -math::Dot(axes[row], light.center);
    }
    float farPlane = 1.f;
    for (const auto& instance : snapshot.allInstances)
    {
        const auto bounds = math::TransformBounds(view, instance.bounds);
        farPlane = std::max(farPlane, -bounds.minimum.z * 1.05f + .1f);
    }
    return math::Perspective(170, 1, .01f, farPlane) * view;
}
} // namespace renderlab::renderer
