#include <renderlab/generated/parameters.hlsli>
#include "Core.hlsli"
#include "Lighting.hlsli"
#include "MaterialSampling.hlsli"
#include "ImageBasedLighting.hlsli"
#include "Alpha.hlsli"
#include "Shadows.hlsli"

struct Attributes { float3 positionOS : POSITION; float3 normalOS : NORMAL; float2 uv : TEXCOORD0; };
struct Varyings { float4 positionCS : SV_Position; float3 positionWS : TEXCOORD0; float3 normalWS : TEXCOORD1; float2 uv : TEXCOORD2; };
Varyings VSMain(Attributes input)
{
    Varyings output;
    output.positionCS = TransformObjectToHClip(input.positionOS, SceneObject.ObjectToWorld, SceneView.ViewProjection);
    output.positionWS = TransformObjectToWorld(input.positionOS, SceneObject.ObjectToWorld);
    output.normalWS = TransformObjectToWorldNormal(input.normalOS, SceneObject.NormalToWorld);
    output.uv = input.uv;
    return output;
}
float4 PSMain(Varyings input, bool frontFace : SV_IsFrontFace) : SV_Target
{
    RasterSamplingContext context;
    context.uv = input.uv;
    context.uvDx = ddx(input.uv);
    context.uvDy = ddy(input.uv);
    context.positionDx = ddx(input.positionWS);
    context.positionDy = ddy(input.positionWS);
    context.normalWS = frontFace ? input.normalWS : -input.normalWS;
    MaterialFactors factors;
    factors.baseColor = Material.BaseColor; factors.metallic = Material.Metallic;
    factors.roughness = Material.Roughness; factors.normalScale = Material.NormalScale;
    factors.occlusionStrength = Material.OcclusionStrength; factors.emission = Material.Emission;
    MaterialSurface surface = EvaluateMaterial(factors, context, BaseMap, MetallicRoughnessMap,
        NormalMap, OcclusionMap, EmissionMap, BaseSampler);
    float3 shadowCoordinate = ProjectMainLightShadow(SceneShadow, input.positionWS, SafeNormalize(context.normalWS));
    RasterShadowSamplingContext shadowSampling = MakeShadowSamplingContext(shadowCoordinate,
        ddx(shadowCoordinate), ddy(shadowCoordinate));
    float visibility = MainLightVisibility(SceneShadow, MainLightShadow, shadowSampling);
#if RENDERLAB_ALPHA_CLIP
    ApplyAlphaCutoff(surface.alpha, Material.AlphaCutoff);
#endif
    float3 viewDirWS = GetWorldSpaceViewDir(input.positionWS, SceneView.CameraPosition.xyz);
    DirectionalLight light = GetMainLight(SceneLighting);
    float3 direct = EvaluateBRDF(surface.baseColor, surface.metallic, surface.perceptualRoughness,
        surface.normalWS, SafeNormalize(viewDirWS), light.direction) * light.color
        * visibility;
    return EncodeSurfaceOutput(direct + EvaluateImageBasedLighting(surface, viewDirWS, SceneLighting.EnvironmentParameters, EnvironmentAtlas, BrdfLut) + surface.emission,
        surface.alpha, Material.Surface, Material.Blend);
}
