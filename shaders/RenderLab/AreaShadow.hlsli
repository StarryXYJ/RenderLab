#ifndef RENDERLAB_AREA_SHADOW_INCLUDED
#define RENDERLAB_AREA_SHADOW_INCLUDED
#include <renderlab/generated/types.hlsli>
float AreaShadowDistance(float depth, float nearPlane, float farPlane)
{
    return nearPlane * farPlane / max(nearPlane + depth * (farPlane - nearPlane), 1e-8);
}
// Interpolate the results of depth comparisons, not the shadow-map depths.
float AreaShadowBilinearVisibility(Texture2D<float4> shadowMap, float2 location,
                                   float2 receiverUv, float receiverDepth, float depthBias,
                                   float2 depthGradient, uint2 dimensions)
{
    float2 texel = 1.0 / float2(dimensions);
    float2 position = location * float2(dimensions) - .5;
    int2 base = int2(floor(position));
    float2 fraction = frac(position);
    float result = 0;
    [unroll] for (int y = 0; y < 2; ++y)
    [unroll] for (int x = 0; x < 2; ++x)
    {
        int2 tap = base + int2(x, y);
        bool outside = any(tap < 0) || any(tap >= int2(dimensions));
        float depth = shadowMap.Load(int3(clamp(tap, 0, int2(dimensions) - 1), 0)).x;
        float receiver = receiverDepth + dot(depthGradient, (float2(tap) + .5) * texel - receiverUv);
        float2 weight = float2(x == 0 ? 1 - fraction.x : fraction.x,
                               y == 0 ? 1 - fraction.y : fraction.y);
        result += (outside || receiver + depthBias >= depth ? 1 : 0) * weight.x * weight.y;
    }
    return result;
}
float2 AreaShadowBilinearBlockers(Texture2D<float4> shadowMap, float2 location,
                                  float2 receiverUv, float receiverDepth, float depthBias,
                                  float2 depthGradient, uint2 dimensions, float nearPlane,
                                  float farPlane)
{
    float2 texel = 1.0 / float2(dimensions);
    float2 position = location * float2(dimensions) - .5;
    int2 base = int2(floor(position));
    float2 fraction = frac(position);
    float2 result = 0;
    [unroll] for (int y = 0; y < 2; ++y)
    [unroll] for (int x = 0; x < 2; ++x)
    {
        int2 tap = base + int2(x, y);
        if (any(tap < 0) || any(tap >= int2(dimensions))) continue;
        float depth = shadowMap.Load(int3(tap, 0)).x;
        float receiver = receiverDepth + dot(depthGradient, (float2(tap) + .5) * texel - receiverUv);
        float2 weight = float2(x == 0 ? 1 - fraction.x : fraction.x,
                               y == 0 ? 1 - fraction.y : fraction.y);
        float w = depth > receiver + depthBias ? weight.x * weight.y : 0;
        result += float2(AreaShadowDistance(depth, nearPlane, farPlane) * w, w);
    }
    return result;
}
float SampleAreaShadow(float4x4 matrix, float4 parameters, float4 lightInfo,
                       Texture2D<float4> shadowMap, float3 position, float3 normal,
                       float2 depthGradient)
{
    float4 clip = mul(matrix, float4(position + normal * parameters.z, 1));
    if (clip.w <= 0) return 1;
    float3 projected = clip.xyz / clip.w;
    float2 uv = projected.xy * float2(.5, -.5) + .5;
    if (any(uv < 0) || any(uv > 1) || projected.z < 0 || projected.z > 1) return 1;
    uint width, height;
    shadowMap.GetDimensions(width, height);
    float2 texel = 1.0 / float2(width, height);
    // Perspective reversed depth is approximately near / distance.
    float depthBias = parameters.y / max(clip.w * clip.w, 1e-6);
    const float focalScale = .08748866; // 1 / tan(170 degrees / 2)
    float2 lightExtent = max(lightInfo.xy, 0);
    // Limit the search to the projected emitter footprint at the receiver. A search
    // based on the tiny shadow near plane samples unrelated walls far from the ray and
    // incorrectly turns room corners into huge occluders.
    float2 searchRadius = clamp(lightExtent * focalScale / (2 * max(clip.w, 1e-5)),
                                texel, .12.xx);
    float blockerDistance = 0;
    float blockers = 0;
    // Include the center separately to avoid missing a small blocker between grid samples.
    [loop] for (int k = 0; k < 17; ++k)
    {
        float2 offset = k == 16 ? 0 : (float2(k % 4, k / 4) + .5) / 2 - 1;
        float2 location = uv + offset * searchRadius;
        float2 sample = AreaShadowBilinearBlockers(shadowMap, location, uv, projected.z,
                                                   depthBias, depthGradient, uint2(width, height),
                                                   lightInfo.w, lightInfo.z);
        blockerDistance += sample.x;
        blockers += sample.y;
    }
    if (blockers == 0) return 1;
    blockerDistance /= blockers;
    float separation = max(0, clip.w - blockerDistance);
    float2 filterRadius = clamp(lightExtent * focalScale * separation /
                                (4 * max(blockerDistance * clip.w, 1e-6)), texel, .12.xx);
    float visible = 0;
    [unroll] for (int y = 0; y < 4; ++y)
    [unroll] for (int x = 0; x < 4; ++x)
    {
        float2 offset = (float2(x, y) + .5) / 2 - 1;
        float2 location = uv + offset * filterRadius;
        visible += AreaShadowBilinearVisibility(shadowMap, location, uv, projected.z,
                                                depthBias, depthGradient, uint2(width, height));
    }
    return visible / 16;
}
float3 AreaShadowProjected(float4 clip)
{
    float3 projected = clip.xyz / (abs(clip.w) > 1e-6 ? clip.w : 1e-6);
    return float3(projected.xy * float2(.5, -.5) + .5, projected.z);
}
float2 AreaShadowDepthGradient(float4x4 matrix, float4 parameters, float3 position, float3 normal)
{
    float3 n = normalize(normal);
    float3 tangent = normalize(cross(abs(n.z) < .999 ? float3(0, 0, 1) : float3(0, 1, 0), n));
    float3 bitangent = cross(n, tangent);
    float3 origin = position + n * parameters.z;
    float4 clip = mul(matrix, float4(origin, 1));
    float stepSize = max(.001, abs(clip.w) * .01);
    float3 center = AreaShadowProjected(clip);
    float3 dx = AreaShadowProjected(mul(matrix, float4(origin + tangent * stepSize, 1))) - center;
    float3 dy = AreaShadowProjected(mul(matrix, float4(origin + bitangent * stepSize, 1))) - center;
    float determinant = dx.x * dy.y - dx.y * dy.x;
    return abs(determinant) > 1e-12
        ? float2(dx.z * dy.y - dy.z * dx.y, dy.z * dx.x - dx.z * dy.x) / determinant : 0;
}
float2 AreaShadowVisibility(SceneAreaShadowData shadow, Texture2D<float4> first,
                            Texture2D<float4> second, float3 position, float3 normal)
{
    float2 gradient0 = AreaShadowDepthGradient(shadow.Matrix0, shadow.Parameters, position, normal);
    float2 gradient1 = AreaShadowDepthGradient(shadow.Matrix1, shadow.Parameters, position, normal);
    float2 result = 1;
    if (shadow.Parameters.x > 0)
        result.x = SampleAreaShadow(shadow.Matrix0, shadow.Parameters, shadow.Light0, first, position, normal, gradient0);
    if (shadow.Parameters.x > 1)
        result.y = SampleAreaShadow(shadow.Matrix1, shadow.Parameters, shadow.Light1, second, position, normal, gradient1);
    return result;
}
#endif
