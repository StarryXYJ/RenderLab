#ifndef RENDERLAB_SHADOWS_INCLUDED
#define RENDERLAB_SHADOWS_INCLUDED
// Reversed depth: the receiver is lit when it is at least as close to the light as the map.
// The map stores raster depth in R, avoiding sampled-depth requirements in the base RHI.
float MainLightVisibility(float3 positionWS, float3 normalWS)
{
    if (Frame.MainLightShadowParameters.w == 0)
        return 1;
    float3 offsetPosition = positionWS + normalWS * Frame.MainLightShadowParameters.z;
    float4 clipPosition = mul(Frame.MainLightShadowMatrix, float4(offsetPosition, 1));
    float3 ndc = clipPosition.xyz / clipPosition.w;
    float2 uv = ndc.xy * float2(0.5, -0.5) + 0.5;
    // Compare each PCF tap at the receiver plane's depth, rather than the center depth.
    // This removes planar self-shadowing caused by oblique surfaces across the 3x3 footprint.
    float2 du = ddx(uv), dv = ddy(uv);
    float2 dz = float2(ddx(ndc.z), ddy(ndc.z));
    float determinant = du.x * dv.y - du.y * dv.x;
    float2 depthGradient = abs(determinant) > 1e-10
        ? float2(dz.x * dv.y - dz.y * du.y, dz.y * du.x - dz.x * dv.x) / determinant
        : float2(0, 0);
    if (any(uv < 0) || any(uv > 1) || ndc.z < 0 || ndc.z > 1)
        return 1;
    uint width, height;
    MainLightShadow.GetDimensions(width, height);
    int2 pixel = int2(uv * float2(width, height));
    float visibility = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            int2 tap = pixel + int2(x, y);
            bool outside = any(tap < 0) || any(tap >= int2(width, height));
            float closest = MainLightShadow.Load(int3(clamp(tap, 0, int2(width, height) - 1), 0)).r;
            float receiver = ndc.z + dot(depthGradient,
                (float2(tap) + 0.5) / float2(width, height) - uv);
            visibility += outside || receiver + Frame.MainLightShadowParameters.y >= closest ? 1 : 0;
        }
    return visibility / 9;
}
#endif
