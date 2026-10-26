#ifndef RENDERLAB_SHADOWS_INCLUDED
#define RENDERLAB_SHADOWS_INCLUDED
// Reversed depth: the receiver is lit when it is at least as close to the light as the map.
// The map stores raster depth in R, avoiding sampled-depth requirements in the base RHI.
#include <renderlab/generated/types.hlsli>
struct RasterShadowSamplingContext { float3 coordinate, dx, dy; };
RasterShadowSamplingContext MakeShadowSamplingContext(float3 coordinate, float3 dx, float3 dy)
{
    RasterShadowSamplingContext context;
    context.coordinate = coordinate; context.dx = dx; context.dy = dy;
    return context;
}
float3 ProjectMainLightShadow(SceneShadowData shadow, float3 positionWS, float3 normalWS)
{
    float3 offsetPosition = positionWS + normalWS * shadow.MainLightShadowParameters.z;
    float4 clipPosition = mul(shadow.MainLightShadowMatrix, float4(offsetPosition, 1));
    float3 ndc = clipPosition.xyz / clipPosition.w;
    return float3(ndc.xy * float2(0.5, -0.5) + 0.5, ndc.z);
}
float MainLightVisibility(SceneShadowData shadow, Texture2D<float4> shadowMap,
    RasterShadowSamplingContext context)
{
    if (shadow.MainLightShadowParameters.w == 0) return 1;
    float2 uv = context.coordinate.xy;
    float2 du = context.dx.xy, dv = context.dy.xy;
    float2 dz = float2(context.dx.z, context.dy.z);
    float determinant = du.x * dv.y - du.y * dv.x;
    float2 depthGradient = abs(determinant) > 1e-10
        ? float2(dz.x * dv.y - dz.y * du.y, dz.y * du.x - dz.x * dv.x) / determinant
        : float2(0, 0);
    if (any(uv < 0) || any(uv > 1) || context.coordinate.z < 0 || context.coordinate.z > 1)
        return 1;
    uint width, height;
    shadowMap.GetDimensions(width, height);
    int2 pixel = int2(uv * float2(width, height));
    float visibility = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            int2 tap = pixel + int2(x, y);
            bool outside = any(tap < 0) || any(tap >= int2(width, height));
            float closest = shadowMap.Load(int3(clamp(tap, 0, int2(width, height) - 1), 0)).r;
            float receiver = context.coordinate.z + dot(depthGradient,
                (float2(tap) + 0.5) / float2(width, height) - uv);
            visibility += outside || receiver + shadow.MainLightShadowParameters.y >= closest ? 1 : 0;
        }
    return visibility / 9;
}
#endif
