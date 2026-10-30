#include "math/math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

namespace
{
using namespace renderlab;
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
math::Mat4 CornerBoundsMatrix(math::Vec3 t, math::Quaternion q, math::Vec3 scale)
{
    const auto norm = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!std::isfinite(norm) || norm < 1e-8f)
        throw std::invalid_argument("Invalid quaternion");
    q.x /= norm;
    q.y /= norm;
    q.z /= norm;
    q.w /= norm;
    math::Mat4 m = math::Mat4::Identity();
    m.At(0, 0) = (1 - 2 * (q.y * q.y + q.z * q.z)) * scale.x;
    m.At(0, 1) = 2 * (q.x * q.y - q.z * q.w) * scale.y;
    m.At(0, 2) = 2 * (q.x * q.z + q.y * q.w) * scale.z;
    m.At(1, 0) = 2 * (q.x * q.y + q.z * q.w) * scale.x;
    m.At(1, 1) = (1 - 2 * (q.x * q.x + q.z * q.z)) * scale.y;
    m.At(1, 2) = 2 * (q.y * q.z - q.x * q.w) * scale.z;
    m.At(2, 0) = 2 * (q.x * q.z - q.y * q.w) * scale.x;
    m.At(2, 1) = 2 * (q.y * q.z + q.x * q.w) * scale.y;
    m.At(2, 2) = (1 - 2 * (q.x * q.x + q.y * q.y)) * scale.z;
    m.At(0, 3) = t.x;
    m.At(1, 3) = t.y;
    m.At(2, 3) = t.z;
    return m;
}
// The original float pivot contract remains a test reference, not a runtime fallback.
void OriginalInverseValidation(const math::Mat4& m)
{
    float values[4][8]{};
    for (size_t r = 0; r < 4; ++r)
        for (size_t c = 0; c < 4; ++c)
        {
            if (!std::isfinite(m.At(r, c)))
                throw std::invalid_argument("Nonfinite matrix");
            values[r][c] = m.At(r, c);
            values[r][c + 4] = r == c ? 1.f : 0.f;
        }
    for (size_t c = 0; c < 4; ++c)
    {
        size_t pivot = c;
        for (size_t r = c + 1; r < 4; ++r)
            if (std::abs(values[r][c]) > std::abs(values[pivot][c]))
                pivot = r;
        if (std::abs(values[pivot][c]) < 1e-10f)
            throw std::invalid_argument("Singular transform");
        for (size_t k = 0; k < 8; ++k)
            std::swap(values[c][k], values[pivot][k]);
        const auto divisor = values[c][c];
        for (auto& value : values[c])
            value /= divisor;
        for (size_t r = 0; r < 4; ++r)
            if (r != c)
            {
                const auto factor = values[r][c];
                for (size_t k = 0; k < 8; ++k)
                    values[r][k] -= factor * values[c][k];
            }
    }
}
template <class F> bool Accepted(F&& work)
{
    try
    {
        work();
        return true;
    }
    catch (const std::invalid_argument&)
    {
        return false;
    }
}
math::Bounds Corners(const math::Mat4& matrix, math::Bounds bounds)
{
    math::Bounds result{{INFINITY, INFINITY, INFINITY}, {-INFINITY, -INFINITY, -INFINITY}};
    for (unsigned i = 0; i < 8; ++i)
    {
        const auto p = math::Point(matrix, {i & 1 ? bounds.maximum.x : bounds.minimum.x,
                                            i & 2 ? bounds.maximum.y : bounds.minimum.y,
                                            i & 4 ? bounds.maximum.z : bounds.minimum.z});
        result.minimum = {std::min(result.minimum.x, p.x), std::min(result.minimum.y, p.y),
                          std::min(result.minimum.z, p.z)};
        result.maximum = {std::max(result.maximum.x, p.x), std::max(result.maximum.y, p.y),
                          std::max(result.maximum.z, p.z)};
    }
    return result;
}
void Contracts()
{
    static_assert(sizeof(math::Vec3) == 12 && sizeof(math::Mat4) == 64);
    std::mt19937 random(0x4b4202);
    std::uniform_real_distribution<float> angle(-180, 180), position(-1000, 1000), extent(.01f, 30),
        scale(.1f, 10);
    for (unsigned i = 0; i < 5000; ++i)
    {
        const math::Vec3 t{position(random), position(random), position(random)};
        const auto q =
            math::Quaternion::EulerDegrees({angle(random), angle(random), angle(random)});
        const math::Vec3 s{scale(random), scale(random), i % 2 ? -scale(random) : scale(random)};
        const auto raw = CornerBoundsMatrix(t, q, s);
        const auto composed = math::Compose(t, q, s);
        Require(raw.values == composed.values, "Compose changed matrix data");
        Require(Accepted([&] { OriginalInverseValidation(raw); }),
                "Reference rejected nonsingular fixture");
        const auto shear = composed * math::Compose({}, math::Quaternion::EulerDegrees({17, 43, 9}),
                                                    {.7f, 2, 1.3f});
        const math::Bounds bounds{{-extent(random), -extent(random), -extent(random)},
                                  {extent(random), extent(random), extent(random)}};
        Require(math::TransformBounds(shear, bounds) == Corners(shear, bounds),
                "Axis support bounds differ from eight-corner float reference");
        const auto inverse = math::Inverse(shear);
        const auto identity = shear * inverse;
        for (size_t r = 0; r < 4; ++r)
            for (size_t c = 0; c < 4; ++c)
                Require(std::abs(identity.At(r, c) - (r == c ? 1.f : 0.f)) < .004f,
                        "Sheared inverse identity residual");
    }
    for (float value :
         {0.f, 1e-12f, 1e-10f, 1e-9f, 1e-6f, 1.f, 1e6f, std::numeric_limits<float>::infinity(),
          std::numeric_limits<float>::quiet_NaN()})
        for (unsigned axis = 0; axis < 3; ++axis)
            for (unsigned orientation = 0; orientation < 32; ++orientation)
            {
                const auto q = math::Quaternion::EulerDegrees(
                    {float(orientation * 11), float(orientation * 7), float(orientation * 13)});
                math::Vec3 s{1, 1, 1};
                if (axis == 0)
                    s.x = value;
                else if (axis == 1)
                    s.y = value;
                else
                    s.z = value;
                const auto raw = CornerBoundsMatrix({}, q, s);
                Require(Accepted([&] { OriginalInverseValidation(raw); }) ==
                            Accepted([&] { math::Compose({}, q, s); }),
                        "Compose changed finite/singular rejection boundary");
            }
    const auto inf = std::numeric_limits<float>::infinity();
    Require(!Accepted([&] { math::Compose({inf, 0, 0}, {}, {1, 1, 1}); }),
            "Nonfinite translation accepted");
    Require(!Accepted([] { math::Compose({}, {0, 0, 0, 0}, {1, 1, 1}); }),
            "Zero quaternion accepted");
    const math::Bounds point{{1, 2, 3}, {1, 2, 3}};
    Require(math::TransformBounds(math::Mat4::Identity(), point) == point,
            "Degenerate bounds changed");
}
} // namespace
int main()
{
    try
    {
        Contracts();
        std::cout << "Math contracts passed: 5000 shear/mirror/bounds cases and rejection edges\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
