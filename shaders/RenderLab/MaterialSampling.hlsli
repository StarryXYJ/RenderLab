#ifndef RENDERLAB_MATERIAL_SAMPLING_INCLUDED
#define RENDERLAB_MATERIAL_SAMPLING_INCLUDED
#include "Core.hlsli"
#include "Surface.hlsli"

// Raster entry computes derivatives before divergent branches. Future ray entry points must
// supply explicit LOD/footprints instead of calling this raster-only sampling function.
struct RasterSamplingContext
{
    float2 uv, uvDx, uvDy;
    float3 positionDx, positionDy, normalWS;
};
float3 NormalFromMap(RasterSamplingContext context, float3 normalTS)
{
    float3 n = SafeNormalize(context.normalWS);
    float determinant = context.uvDx.x * context.uvDy.y - context.uvDx.y * context.uvDy.x;
    if (abs(determinant) < 1e-8)
        return n;
    float3 t = (context.positionDx * context.uvDy.y - context.positionDy * context.uvDx.y)
        / determinant;
    float3 b = (context.positionDy * context.uvDx.x - context.positionDx * context.uvDy.x)
        / determinant;
    t -= n * dot(n, t);
    if (dot(t, t) < 1e-12 || dot(b, b) < 1e-12)
        return n;
    t = SafeNormalize(t);
    float handedness = dot(cross(n, t), b) < 0 ? -1 : 1;
    b = cross(n, t) * handedness;
    return SafeNormalize(t * normalTS.x + b * normalTS.y + n * normalTS.z);
}
MaterialSurface EvaluateMaterial(RasterSamplingContext context)
{
    MaterialSurface surface;
    float4 base = Material.BaseColor * BaseMap.SampleGrad(BaseSampler, context.uv,
        context.uvDx, context.uvDy);
    float4 mr = MetallicRoughnessMap.SampleGrad(BaseSampler, context.uv,
        context.uvDx, context.uvDy);
    float3 normalTS = NormalMap.SampleGrad(BaseSampler, context.uv,
        context.uvDx, context.uvDy).xyz * 2 - 1;
    normalTS.xy *= Material.NormalScale;
    surface.baseColor = max(base.rgb, 0);
    surface.alpha = saturate(base.a);
    surface.normalWS = NormalFromMap(context, normalTS);
    surface.metallic = saturate(Material.Metallic * mr.b);
    surface.perceptualRoughness = clamp(Material.Roughness * mr.g, 0.045, 1);
    surface.occlusion = lerp(1, OcclusionMap.SampleGrad(BaseSampler, context.uv,
        context.uvDx, context.uvDy).r, Material.OcclusionStrength);
    surface.emission = Material.Emission * EmissionMap.SampleGrad(BaseSampler, context.uv,
        context.uvDx, context.uvDy).rgb;
    return surface;
}
#endif
