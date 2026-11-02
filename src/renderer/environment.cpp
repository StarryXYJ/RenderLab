#include "renderer/environment.hpp"

#ifdef RENDERLAB_ENVIRONMENT_SIMD
#include "environment_ispc.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace renderlab::renderer
{
namespace
{
constexpr float Pi = std::numbers::pi_v<float>;
constexpr uint32_t Samples = 128;
#ifdef RENDERLAB_ENVIRONMENT_SIMD
static_assert(EnvironmentWidth == 64 && EnvironmentHeight == 32 && EnvironmentLevels == 6 &&
                  Samples == 128,
              "Update the fixed ISPC atlas ABI together with the C++ constants");
#endif
float RadicalInverse(uint32_t bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xaaaaaaaau) >> 1);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xccccccccu) >> 2);
    bits = ((bits & 0x0f0f0f0fu) << 4) | ((bits & 0xf0f0f0f0u) >> 4);
    bits = ((bits & 0x00ff00ffu) << 8) | ((bits & 0xff00ff00u) >> 8);
    return float(bits) * 2.3283064365386963e-10f;
}
struct Frame
{
    math::Vec3 normal, tangent, bitangent;
    math::Vec3 Transform(math::Vec3 local) const
    {
        return {tangent.x * local.x + bitangent.x * local.y + normal.x * local.z,
                tangent.y * local.x + bitangent.y * local.y + normal.y * local.z,
                tangent.z * local.x + bitangent.z * local.y + normal.z * local.z};
    }
};
math::Vec3 ImportanceGgx(uint32_t i, float roughness)
{
    const auto phi = 2 * Pi * float(i) / Samples;
    const auto xi = RadicalInverse(i), a = roughness * roughness;
    const auto z = std::sqrt((1 - xi) / (1 + (a * a - 1) * xi));
    const auto r = std::sqrt(std::max(0.f, 1 - z * z));
    return {r * std::cos(phi), r * std::sin(phi), z};
}
float Linear(uint8_t byte)
{
    const float c = float(byte) / 255;
    return c <= .04045f ? c / 12.92f : std::pow((c + .055f) / 1.055f, 2.4f);
}
struct Tables
{
    std::array<float, 256> linear;
    std::array<Frame, EnvironmentWidth * EnvironmentHeight> frames;
    std::array<std::array<math::Vec3, Samples>, EnvironmentLevels + 1> directions;
#ifdef RENDERLAB_ENVIRONMENT_SIMD
    std::array<ispc::EnvironmentFrame, EnvironmentWidth * EnvironmentHeight> simdFrames;
    std::array<ispc::EnvironmentDirection, (EnvironmentLevels + 1) * Samples> simdDirections;
#endif
    Tables()
    {
        for (size_t i = 0; i < linear.size(); ++i)
            linear[i] = Linear(static_cast<uint8_t>(i));
        for (uint32_t y = 0; y < EnvironmentHeight; ++y)
            for (uint32_t x = 0; x < EnvironmentWidth; ++x)
            {
                const auto phi = ((float(x) + .5f) / EnvironmentWidth - .5f) * 2 * Pi;
                const auto theta = (float(y) + .5f) / EnvironmentHeight * Pi;
                const math::Vec3 n{std::sin(theta) * std::cos(phi), std::cos(theta),
                                   std::sin(theta) * std::sin(phi)};
                const auto up = std::abs(n.y) < .999f ? math::Vec3{0, 1, 0} : math::Vec3{1, 0, 0};
                const auto tangent = math::Normalize(math::Cross(up, n));
                frames[size_t(y) * EnvironmentWidth + x] = {n, tangent, math::Cross(n, tangent)};
            }
        for (uint32_t tile = 1; tile <= EnvironmentLevels; ++tile)
            for (uint32_t i = 0; i < Samples; ++i)
                if (tile == EnvironmentLevels)
                {
                    const auto r = std::sqrt(RadicalInverse(i));
                    const auto angle = 2 * Pi * float(i) / Samples;
                    directions[tile][i] = {r * std::cos(angle), r * std::sin(angle),
                                           std::sqrt(1 - r * r)};
                }
                else
                    directions[tile][i] = ImportanceGgx(i, float(tile) / (EnvironmentLevels - 1));
#ifdef RENDERLAB_ENVIRONMENT_SIMD
        for (size_t i = 0; i < frames.size(); ++i)
        {
            const auto& f = frames[i];
            simdFrames[i] = {f.normal.x,  f.normal.y,    f.normal.z,    f.tangent.x,  f.tangent.y,
                             f.tangent.z, f.bitangent.x, f.bitangent.y, f.bitangent.z};
        }
        for (size_t tile = 0; tile < directions.size(); ++tile)
            for (size_t i = 0; i < Samples; ++i)
            {
                const auto d = directions[tile][i];
                simdDirections[tile * Samples + i] = {d.x, d.y, d.z};
            }
#endif
    }
};
const Tables& GetTables()
{
    static const Tables tables;
    return tables;
}
math::Vec3 Sample(const assets::Image& source, const Tables& tables, math::Vec3 direction)
{
    const float u = std::atan2(direction.z, direction.x) / (2 * Pi) + .5f;
    const float v = std::acos(std::clamp(direction.y, -1.f, 1.f)) / Pi;
    const float x = u * float(source.width) - .5f, y = v * float(source.height) - .5f;
    const auto ix = int(std::floor(x)), iy = int(std::floor(y));
    const auto fetch = [&](int px, int py)
    {
        // atan2 restricts bilinear taps to [-1,width], including the longitude seam.
        px = px < 0 ? px + int(source.width) : px;
        px = px >= int(source.width) ? px - int(source.width) : px;
        py = std::clamp(py, 0, int(source.height) - 1);
        const auto offset = (size_t(py) * source.width + px) * 4;
        return math::Vec3{tables.linear[source.rgba[offset]],
                          tables.linear[source.rgba[offset + 1]],
                          tables.linear[source.rgba[offset + 2]]};
    };
    const auto fx = x - std::floor(x), fy = y - std::floor(y);
    const auto a = fetch(ix, iy), b = fetch(ix + 1, iy);
    const auto c = fetch(ix, iy + 1), d = fetch(ix + 1, iy + 1);
    const auto interpolate = [&](float v0, float v1, float v2, float v3)
    { return (v0 * (1 - fx) + v1 * fx) * (1 - fy) + (v2 * (1 - fx) + v3 * fx) * fy; };
    return {interpolate(a.x, b.x, c.x, d.x), interpolate(a.y, b.y, c.y, d.y),
            interpolate(a.z, b.z, c.z, d.z)};
}
void Store(EnvironmentImage& image, uint32_t x, uint32_t y, math::Vec3 value)
{
    const auto offset = (size_t(y) * image.width + x) * 4;
    image.pixels[offset] = value.x;
    image.pixels[offset + 1] = value.y;
    image.pixels[offset + 2] = value.z;
    image.pixels[offset + 3] = 1;
}
} // namespace
EnvironmentImage PrefilterEnvironment(const assets::Image* source, math::Vec3 fallback)
{
    if (source &&
        (!source->width || !source->height || source->width > 8192 || source->height > 8192 ||
         source->rgba.size() != size_t(source->width) * source->height * 4))
        throw std::invalid_argument("Invalid environment image");
    if (!std::isfinite(fallback.x) || !std::isfinite(fallback.y) || !std::isfinite(fallback.z) ||
        fallback.x < 0 || fallback.y < 0 || fallback.z < 0)
        throw std::invalid_argument("Invalid fallback radiance");
    EnvironmentImage result{EnvironmentWidth, EnvironmentHeight * (EnvironmentLevels + 1)};
    result.pixels.resize(size_t(result.width) * result.height * 4);
    if (!source)
    {
        for (uint32_t y = 0; y < result.height; ++y)
            for (uint32_t x = 0; x < result.width; ++x)
                Store(result, x, y, fallback);
        return result;
    }
    const auto& tables = GetTables();
#ifdef RENDERLAB_ENVIRONMENT_SIMD
    // Keep the unfiltered tile on the exact scalar sampling path at source-resolution seams.
    for (uint32_t y = 0; y < EnvironmentHeight; ++y)
        for (uint32_t x = 0; x < EnvironmentWidth; ++x)
            Store(result, x, y,
                  Sample(*source, tables, tables.frames[size_t(y) * EnvironmentWidth + x].normal));
    ispc::FilterEnvironment(
        source->rgba.data(), static_cast<int32_t>(source->width),
        static_cast<int32_t>(source->height), tables.linear.data(), tables.simdFrames.data(),
        tables.simdDirections.data(), static_cast<int32_t>(EnvironmentWidth * EnvironmentHeight),
        static_cast<int32_t>(result.width * result.height), result.pixels.data());
#else
    for (uint32_t tile = 0; tile <= EnvironmentLevels; ++tile)
        for (uint32_t y = 0; y < EnvironmentHeight; ++y)
            for (uint32_t x = 0; x < EnvironmentWidth; ++x)
            {
                const auto& frame = tables.frames[size_t(y) * EnvironmentWidth + x];
                const auto n = frame.normal;
                math::Vec3 sum{};
                float weight = 0;
                if (tile == 0)
                {
                    sum = Sample(*source, tables, n);
                    weight = 1;
                }
                else
                    for (uint32_t i = 0; i < Samples; ++i)
                    {
                        math::Vec3 l;
                        float w;
                        if (tile == EnvironmentLevels)
                        {
                            l = frame.Transform(tables.directions[tile][i]);
                            w = 1;
                        }
                        else
                        {
                            const auto h = frame.Transform(tables.directions[tile][i]);
                            const auto nh2 = 2 * (n.x * h.x + n.y * h.y + n.z * h.z);
                            l = {h.x * nh2 - n.x, h.y * nh2 - n.y, h.z * nh2 - n.z};
                            w = std::max(0.f, n.x * l.x + n.y * l.y + n.z * l.z);
                        }
                        const auto sampled = Sample(*source, tables, l);
                        sum.x += sampled.x * w;
                        sum.y += sampled.y * w;
                        sum.z += sampled.z * w;
                        weight += w;
                    }
                const auto inverseWeight = 1 / std::max(weight, 1e-6f);
                Store(result, x, tile * EnvironmentHeight + y,
                      {sum.x * inverseWeight, sum.y * inverseWeight, sum.z * inverseWeight});
            }
#endif
    return result;
}
namespace
{
EnvironmentImage BuildEnvironmentBrdf()
{
    EnvironmentImage result{64, 64};
    result.pixels.resize(size_t(result.width) * result.height * 4);
    for (uint32_t y = 0; y < result.height; ++y)
    {
        std::array<math::Vec3, Samples> halfVectors;
        const auto roughness = (float(y) + .5f) / result.height;
        for (uint32_t i = 0; i < Samples; ++i)
            halfVectors[i] = ImportanceGgx(i, roughness);
        for (uint32_t x = 0; x < result.width; ++x)
        {
            const auto nv = (float(x) + .5f) / result.width;
            const math::Vec3 v{std::sqrt(1 - nv * nv), 0, nv};
            float scale = 0, bias = 0;
            for (uint32_t i = 0; i < Samples; ++i)
            {
                const auto h = halfVectors[i];
                const auto vh = std::max(0.f, math::Dot(v, h));
                const auto l = h * (2 * vh) - v;
                if (l.z <= 0)
                    continue;
                const auto k = roughness * roughness / 2;
                const auto g = [&](float nd) { return nd / (nd * (1 - k) + k); };
                const auto visibility = g(nv) * g(l.z) * vh / std::max(h.z * nv, 1e-6f);
                const auto fc = std::pow(1 - vh, 5.f);
                scale += (1 - fc) * visibility;
                bias += fc * visibility;
            }
            Store(result, x, y, {scale / Samples, bias / Samples, 0});
        }
    }
    return result;
}
} // namespace
EnvironmentImage IntegrateEnvironmentBrdf()
{
    // Pure, fixed-size CPU data may survive Device/Renderer rebuilds; callers own their copy.
    static const auto result = BuildEnvironmentBrdf();
    return result;
}
} // namespace renderlab::renderer
