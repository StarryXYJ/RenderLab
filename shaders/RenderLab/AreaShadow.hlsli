#ifndef RENDERLAB_AREA_SHADOW_INCLUDED
#define RENDERLAB_AREA_SHADOW_INCLUDED
#include <renderlab/generated/types.hlsli>
float SampleAreaShadow(float4x4 matrix, float4 parameters, Texture2D<float4> shadowMap,
                       float3 position, float3 normal)
{
    float4 clip = mul(matrix, float4(position + normal * parameters.z, 1));
    if (clip.w <= 0) return 1;
    float3 projected = clip.xyz / clip.w;
    float2 uv = projected.xy * float2(.5, -.5) + .5;
    if (any(uv < 0) || any(uv > 1) || projected.z < 0 || projected.z > 1) return 1;
    uint width, height;
    shadowMap.GetDimensions(width, height);
    int2 pixel = int2(uv * float2(width, height));
    // Perspective reversed depth is approximately near / distance. Scale the bias
    // quadratically so it corresponds to a roughly constant world-space offset.
    float depthBias = parameters.y / max(clip.w * clip.w, 1e-6);
    float visible = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
    [unroll] for (int x = -1; x <= 1; ++x)
    {
        int2 tap = pixel + int2(x, y);
        bool outside = any(tap < 0) || any(tap >= int2(width, height));
        float depth = shadowMap.Load(int3(clamp(tap, 0, int2(width, height) - 1), 0)).x;
        visible += outside || projected.z + depthBias >= depth ? 1 : 0;
    }
    return visible / 9;
}
float2 AreaShadowVisibility(SceneAreaShadowData shadow, Texture2D<float4> first,
                            Texture2D<float4> second, float3 position, float3 normal)
{
    float2 result = 1;
    if (shadow.Parameters.x > 0)
        result.x = SampleAreaShadow(shadow.Matrix0, shadow.Parameters, first, position, normal);
    if (shadow.Parameters.x > 1)
        result.y = SampleAreaShadow(shadow.Matrix1, shadow.Parameters, second, position, normal);
    return result;
}
#endif
