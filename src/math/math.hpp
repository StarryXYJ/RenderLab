#pragma once

#include <array>
#include <cstddef>

namespace renderlab::math
{
struct Vec3
{
    float x = 0, y = 0, z = 0;
    bool operator==(const Vec3&) const = default;
};
Vec3 operator+(Vec3 a, Vec3 b);
Vec3 operator-(Vec3 a, Vec3 b);
Vec3 operator*(Vec3 a, float b);
float Dot(Vec3 a, Vec3 b);
Vec3 Cross(Vec3 a, Vec3 b);
float Length(Vec3 a);
Vec3 Normalize(Vec3 a);
/// @brief XYZW rotation quaternion; identity looks along local -Z for scene cameras.
struct Quaternion
{
    float x = 0, y = 0, z = 0, w = 1;
    /// @brief Builds Rz * Ry * Rx from XYZ angles in degrees.
    static Quaternion EulerDegrees(Vec3 angles);
};
/// @brief Composes rotations: b is applied first, then a.
Quaternion operator*(Quaternion a, Quaternion b);
/** @brief Row-major CPU storage, column-vector multiplication; Pack converts GPU layout. */
struct Mat4
{
    std::array<float, 16> values{};
    float& At(size_t row, size_t column)
    {
        return values[row * 4 + column];
    }
    float At(size_t row, size_t column) const
    {
        return values[row * 4 + column];
    }
    static Mat4 Identity();
};
Mat4 operator*(const Mat4& a, const Mat4& b);
/// @brief Inverts a finite nonsingular matrix; throws for invalid transforms.
Mat4 Inverse(const Mat4& matrix);
Mat4 Transpose(const Mat4& matrix);
/// @brief Builds T * R * S, normalizing the quaternion and rejecting singular transforms.
Mat4 Compose(Vec3 translation, Quaternion rotation, Vec3 scale);
/// @brief Right-handed, local -Z perspective with 0..1 reversed depth (near=1, far=0).
Mat4 Perspective(float verticalFovDegrees, float aspect, float nearPlane, float farPlane);
Vec3 Point(const Mat4& matrix, Vec3 point);
Vec3 Direction(const Mat4& matrix, Vec3 direction);
/// @brief Axis-aligned bounds in the coordinate space of their owner.
struct Bounds
{
    Vec3 minimum{}, maximum{};
};
Bounds TransformBounds(const Mat4& world, Bounds bounds);
/// @brief Conservative six-plane test; intersecting world bounds remain visible.
bool Visible(const Mat4& viewProjection, Bounds worldBounds);
} // namespace renderlab::math
