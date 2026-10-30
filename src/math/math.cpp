#include "math/math.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>
#include <stdexcept>

#include <DirectXMath.h>

namespace renderlab::math
{
namespace
{
DirectX::XMMATRIX XM_CALLCONV Load(const Mat4& matrix)
{
    DirectX::XMFLOAT4X4 value;
    static_assert(sizeof(value) == sizeof(matrix.values));
    std::memcpy(&value, matrix.values.data(), sizeof(value));
    return DirectX::XMLoadFloat4x4(&value);
}
Mat4 XM_CALLCONV Store(DirectX::FXMMATRIX matrix)
{
    DirectX::XMFLOAT4X4 value;
    DirectX::XMStoreFloat4x4(&value, matrix);
    Mat4 result;
    std::memcpy(result.values.data(), &value, sizeof(value));
    return result;
}
void ValidateLinearTransform(const Mat4& matrix)
{
    float values[3][3]{};
    for (size_t r = 0; r < 3; ++r)
    {
        for (size_t c = 0; c < 4; ++c)
            if (!std::isfinite(matrix.At(r, c)))
                throw std::invalid_argument("Nonfinite matrix");
        for (size_t c = 0; c < 3; ++c)
            values[r][c] = matrix.At(r, c);
    }
    // Match Inverse's pivot rejection without constructing a discarded inverse of affine TRS.
    for (size_t c = 0; c < 3; ++c)
    {
        size_t pivot = c;
        for (size_t r = c + 1; r < 3; ++r)
            if (std::abs(values[r][c]) > std::abs(values[pivot][c]))
                pivot = r;
        if (std::abs(values[pivot][c]) < 1e-10f)
            throw std::invalid_argument("Singular transform");
        for (size_t k = c; k < 3; ++k)
            std::swap(values[c][k], values[pivot][k]);
        const auto divisor = values[c][c];
        for (size_t k = c + 1; k < 3; ++k)
            values[c][k] /= divisor;
        for (size_t r = c + 1; r < 3; ++r)
        {
            const auto factor = values[r][c];
            for (size_t k = c + 1; k < 3; ++k)
                values[r][k] -= factor * values[c][k];
        }
    }
}
} // namespace
Vec3 operator+(Vec3 a, Vec3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Vec3 operator-(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Vec3 operator*(Vec3 a, float b)
{
    return {a.x * b, a.y * b, a.z * b};
}
float Dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec3 Cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float Length(Vec3 a)
{
    return std::sqrt(Dot(a, a));
}
Vec3 Normalize(Vec3 a)
{
    const float length = Length(a);
    if (!std::isfinite(length) || length < 1e-8f)
        throw std::invalid_argument("Cannot normalize zero/nonfinite vector");
    return a * (1 / length);
}
Quaternion Quaternion::EulerDegrees(Vec3 angles)
{
    const auto factor = std::numbers::pi_v<float> / 360;
    const auto x = angles.x * factor, y = angles.y * factor, z = angles.z * factor;
    const auto cx = std::cos(x), sx = std::sin(x), cy = std::cos(y), sy = std::sin(y),
               cz = std::cos(z), sz = std::sin(z);
    return {sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz, cx * cy * sz - sx * sy * cz,
            cx * cy * cz + sx * sy * sz};
}
Quaternion operator*(Quaternion a, Quaternion b)
{
    // DirectXMath returns Q2 * Q1; RenderLab applies b first, then a.
    const auto first = DirectX::XMVectorSet(b.x, b.y, b.z, b.w);
    const auto second = DirectX::XMVectorSet(a.x, a.y, a.z, a.w);
    DirectX::XMFLOAT4 result;
    DirectX::XMStoreFloat4(&result, DirectX::XMQuaternionMultiply(first, second));
    return {result.x, result.y, result.z, result.w};
}
Mat4 Mat4::Identity()
{
    Mat4 result;
    for (size_t i = 0; i < 4; ++i)
        result.At(i, i) = 1;
    return result;
}
Mat4 operator*(const Mat4& a, const Mat4& b)
{
    // Ordinary A * B is independent of the library's row-vector transform convention.
    return Store(DirectX::XMMatrixMultiply(Load(a), Load(b)));
}
Mat4 Inverse(const Mat4& a)
{
    float values[4][8]{};
    for (size_t r = 0; r < 4; ++r)
        for (size_t c = 0; c < 4; ++c)
        {
            if (!std::isfinite(a.At(r, c)))
                throw std::invalid_argument("Nonfinite matrix");
            values[r][c] = a.At(r, c);
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
    Mat4 result;
    for (size_t r = 0; r < 4; ++r)
        for (size_t c = 0; c < 4; ++c)
            result.At(r, c) = values[r][c + 4];
    return result;
}
Mat4 Transpose(const Mat4& a)
{
    return Store(DirectX::XMMatrixTranspose(Load(a)));
}
Mat4 Compose(Vec3 t, Quaternion q, Vec3 scale)
{
    const auto norm = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!std::isfinite(norm) || norm < 1e-8f)
        throw std::invalid_argument("Invalid quaternion");
    q.x /= norm;
    q.y /= norm;
    q.z /= norm;
    q.w /= norm;
    Mat4 r = Mat4::Identity();
    r.At(0, 0) = 1 - 2 * (q.y * q.y + q.z * q.z);
    r.At(0, 1) = 2 * (q.x * q.y - q.z * q.w);
    r.At(0, 2) = 2 * (q.x * q.z + q.y * q.w);
    r.At(1, 0) = 2 * (q.x * q.y + q.z * q.w);
    r.At(1, 1) = 1 - 2 * (q.x * q.x + q.z * q.z);
    r.At(1, 2) = 2 * (q.y * q.z - q.x * q.w);
    r.At(2, 0) = 2 * (q.x * q.z - q.y * q.w);
    r.At(2, 1) = 2 * (q.y * q.z + q.x * q.w);
    r.At(2, 2) = 1 - 2 * (q.x * q.x + q.y * q.y);
    for (size_t row = 0; row < 3; ++row)
    {
        r.At(row, 0) *= scale.x;
        r.At(row, 1) *= scale.y;
        r.At(row, 2) *= scale.z;
    }
    r.At(0, 3) = t.x;
    r.At(1, 3) = t.y;
    r.At(2, 3) = t.z;
    ValidateLinearTransform(r);
    return r;
}
Mat4 Perspective(float fov, float aspect, float nearPlane, float farPlane)
{
    if (!std::isfinite(fov) || !std::isfinite(aspect) || !std::isfinite(nearPlane) ||
        !std::isfinite(farPlane) || fov <= 0 || fov >= 179 || aspect <= 0 || nearPlane <= 0 ||
        farPlane <= nearPlane)
        throw std::invalid_argument("Invalid camera projection");
    const float y = 1 / std::tan(fov * std::numbers::pi_v<float> / 360);
    Mat4 result;
    result.At(0, 0) = y / aspect;
    result.At(1, 1) = y;
    result.At(2, 2) = nearPlane / (farPlane - nearPlane);
    result.At(2, 3) = farPlane * nearPlane / (farPlane - nearPlane);
    result.At(3, 2) = -1;
    return result;
}
Vec3 Point(const Mat4& m, Vec3 p)
{
    return {m.At(0, 0) * p.x + m.At(0, 1) * p.y + m.At(0, 2) * p.z + m.At(0, 3),
            m.At(1, 0) * p.x + m.At(1, 1) * p.y + m.At(1, 2) * p.z + m.At(1, 3),
            m.At(2, 0) * p.x + m.At(2, 1) * p.y + m.At(2, 2) * p.z + m.At(2, 3)};
}
Vec3 Direction(const Mat4& m, Vec3 p)
{
    return {m.At(0, 0) * p.x + m.At(0, 1) * p.y + m.At(0, 2) * p.z,
            m.At(1, 0) * p.x + m.At(1, 1) * p.y + m.At(1, 2) * p.z,
            m.At(2, 0) * p.x + m.At(2, 1) * p.y + m.At(2, 2) * p.z};
}
Bounds TransformBounds(const Mat4& m, Bounds b)
{
    std::array<float, 3> minimum{}, maximum{};
    for (size_t row = 0; row < 3; ++row)
    {
        const auto x = m.At(row, 0), y = m.At(row, 1), z = m.At(row, 2), t = m.At(row, 3);
        // Axis support corners preserve Point's operation order, including nonuniform scale/shear.
        minimum[row] = x * (x < 0 ? b.maximum.x : b.minimum.x) +
                       y * (y < 0 ? b.maximum.y : b.minimum.y) +
                       z * (z < 0 ? b.maximum.z : b.minimum.z) + t;
        maximum[row] = x * (x < 0 ? b.minimum.x : b.maximum.x) +
                       y * (y < 0 ? b.minimum.y : b.maximum.y) +
                       z * (z < 0 ? b.minimum.z : b.maximum.z) + t;
    }
    return {{minimum[0], minimum[1], minimum[2]}, {maximum[0], maximum[1], maximum[2]}};
}
bool Visible(const Mat4& m, Bounds b)
{
    std::array<bool, 6> outside{true, true, true, true, true, true};
    for (unsigned i = 0; i < 8; ++i)
    {
        const Vec3 p{i & 1 ? b.maximum.x : b.minimum.x, i & 2 ? b.maximum.y : b.minimum.y,
                     i & 4 ? b.maximum.z : b.minimum.z};
        const auto v = Point(m, p);
        const auto w = m.At(3, 0) * p.x + m.At(3, 1) * p.y + m.At(3, 2) * p.z + m.At(3, 3);
        outside[0] = outside[0] && v.x < -w;
        outside[1] = outside[1] && v.x > w;
        outside[2] = outside[2] && v.y < -w;
        outside[3] = outside[3] && v.y > w;
        outside[4] = outside[4] && v.z < 0;
        outside[5] = outside[5] && v.z > w;
    }
    return std::ranges::none_of(outside, [](bool value) { return value; });
}
} // namespace renderlab::math
