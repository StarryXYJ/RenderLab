#ifndef RENDERLAB_IBL_INCLUDED
#define RENDERLAB_IBL_INCLUDED
#include "BRDF.hlsli"

// Atlas layout matches renderer/environment.hpp. Bilinear loads wrap longitude, clamp latitude
// within each tile and interpolate roughness explicitly; no native mip/cube dependency.
float3 EnvironmentTile(float3 direction, uint tile)
{
    float angle = Frame.EnvironmentParameters.y;
    float3 d = float3(cos(angle) * direction.x - sin(angle) * direction.z, direction.y,
        sin(angle) * direction.x + cos(angle) * direction.z);
    float2 uv = float2(atan2(d.z, d.x) / (2 * RenderLabPi) + 0.5,
        acos(clamp(d.y, -1, 1)) / RenderLabPi);
    float2 p = uv * float2(64, 32) - 0.5;
    int2 a = int2(floor(p));
    float2 f = frac(p);
    int x0 = (a.x % 64 + 64) % 64, x1 = (x0 + 1) % 64;
    int y0 = clamp(a.y, 0, 31) + int(tile) * 32;
    int y1 = clamp(a.y + 1, 0, 31) + int(tile) * 32;
    return lerp(lerp(EnvironmentAtlas.Load(int3(x0, y0, 0)).rgb,
        EnvironmentAtlas.Load(int3(x1, y0, 0)).rgb, f.x),
        lerp(EnvironmentAtlas.Load(int3(x0, y1, 0)).rgb,
        EnvironmentAtlas.Load(int3(x1, y1, 0)).rgb, f.x), f.y)
        * Frame.EnvironmentParameters.x;
}
float2 EnvironmentBrdf(float nv, float roughness)
{
    float2 p = saturate(float2(nv, roughness)) * 64 - 0.5;
    int2 a = int2(floor(p));
    float2 f = frac(p);
    return lerp(lerp(BrdfLut.Load(int3(clamp(a, 0, 63), 0)).rg,
        BrdfLut.Load(int3(clamp(a + int2(1, 0), 0, 63), 0)).rg, f.x),
        lerp(BrdfLut.Load(int3(clamp(a + int2(0, 1), 0, 63), 0)).rg,
        BrdfLut.Load(int3(clamp(a + 1, 0, 63), 0)).rg, f.x), f.y);
}
float3 EvaluateImageBasedLighting(MaterialSurface surface, float3 viewDirWS)
{
    float3 n = surface.normalWS, v = SafeNormalize(viewDirWS);
    float nv = saturate(dot(n, v));
    float roughness = surface.perceptualRoughness;
    float3 f0 = lerp(0.04.xxx, surface.baseColor, surface.metallic);
    float3 f = f0 + (max((1 - roughness).xxx, f0) - f0) * pow(1 - nv, 5);
    float level = roughness * 5;
    float3 reflected = reflect(-v, n);
    uint low = uint(floor(level));
    float3 radiance = lerp(EnvironmentTile(reflected, low),
        EnvironmentTile(reflected, min(low + 1, 5)), frac(level));
    float2 brdf = EnvironmentBrdf(nv, roughness);
    float3 diffuse = (1 - f) * (1 - surface.metallic) * surface.baseColor * EnvironmentTile(n, 6);
    return (diffuse + radiance * (f0 * brdf.x + brdf.y)) * surface.occlusion;
}
#endif
