#ifndef RENDERLAB_AREA_SHADOW_INCLUDED
#define RENDERLAB_AREA_SHADOW_INCLUDED
#include <renderlab/generated/types.hlsli>
float AreaShadowDistance(float depth, float nearPlane, float farPlane)
{
    return nearPlane * farPlane / max(nearPlane + depth * (farPlane - nearPlane), 1e-8);
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
    float2 searchRadius = clamp(lightExtent * focalScale / (4 * max(lightInfo.w, 1e-5)) *
                                saturate((clip.w - lightInfo.w) / clip.w), texel, .12.xx);
    float blockerDistance = 0;
    float blockers = 0;
    // Include the center separately to avoid missing a small blocker between grid samples.
    [unroll] for (int k = 0; k < 17; ++k)
    {
        float2 offset = k == 16 ? 0 : (float2(k % 4, k / 4) + .5) / 2 - 1;
        float2 location = uv + offset * searchRadius;
        if (any(location < 0) || any(location >= 1)) continue;
        int2 tap = clamp(int2(location * float2(width, height)), 0, int2(width, height) - 1);
        float depth = shadowMap.Load(int3(tap, 0)).x;
        float receiver = projected.z + dot(depthGradient, (float2(tap) + .5) * texel - uv);
        if (depth > receiver + depthBias)
        {
            blockerDistance += AreaShadowDistance(depth, lightInfo.w, lightInfo.z);
            blockers += 1;
        }
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
        bool outside = any(location < 0) || any(location >= 1);
        int2 tap = clamp(int2(location * float2(width, height)), 0, int2(width, height) - 1);
        float depth = shadowMap.Load(int3(tap, 0)).x;
        float receiver = projected.z + dot(depthGradient, (float2(tap) + .5) * texel - uv);
        visible += outside || receiver + depthBias >= depth ? 1 : 0;
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
