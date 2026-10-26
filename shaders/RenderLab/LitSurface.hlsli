#include <renderlab/generated/parameters.hlsli>
#include "Core.hlsli"
#include "MaterialSampling.hlsli"
#include "Alpha.hlsli"

struct Attributes
{
    float3 positionOS : POSITION;
    float3 normalOS : NORMAL;
    float2 uv : TEXCOORD0;
};

struct Varyings
{
    float4 positionCS : SV_Position;
    float3 positionWS : TEXCOORD0;
    float3 normalWS : TEXCOORD1;
    float2 uv : TEXCOORD2;
};

Varyings VSMain(Attributes input)
{
    Varyings output;
    output.positionCS = TransformObjectToHClip(input.positionOS, SceneObject.ObjectToWorld,
                                               SceneView.ViewProjection);
    output.positionWS = TransformObjectToWorld(input.positionOS, SceneObject.ObjectToWorld);
    output.normalWS = TransformObjectToWorldNormal(input.normalOS, SceneObject.NormalToWorld);
    output.uv = input.uv;
    return output;
}

MaterialSurface EvaluateLitSurface(Varyings input, bool frontFace)
{
    RasterSamplingContext context;
    context.uv = input.uv;
    context.uvDx = ddx(input.uv);
    context.uvDy = ddy(input.uv);
    context.positionDx = ddx(input.positionWS);
    context.positionDy = ddy(input.positionWS);
    context.normalWS = frontFace ? input.normalWS : -input.normalWS;
    MaterialFactors factors;
    factors.baseColor = Material.BaseColor;
    factors.metallic = Material.Metallic;
    factors.roughness = Material.Roughness;
    factors.normalScale = Material.NormalScale;
    factors.occlusionStrength = Material.OcclusionStrength;
    factors.emission = Material.Emission;
    MaterialSurface surface = EvaluateMaterial(factors, context, BaseMap, MetallicRoughnessMap,
                                               NormalMap, OcclusionMap, EmissionMap, BaseSampler);
#if RENDERLAB_ALPHA_CLIP
    ApplyAlphaCutoff(surface.alpha, Material.AlphaCutoff);
#endif
    return surface;
}
