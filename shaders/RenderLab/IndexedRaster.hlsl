#include <renderlab/generated/types.hlsli>
#include "DrawParameters.hlsli"
#include "Core.hlsli"
#include "MaterialSampling.hlsli"
#include "Lighting.hlsli"
#include "ImageBasedLighting.hlsli"
#include "Shadows.hlsli"
#include "Alpha.hlsli"

struct IndexedInstance
{
    row_major float4x4 world, normal, previousWorld;
    float4 minimum, maximum;
    uint4 indices, reserved;
};
struct IndexedMaterial
{
    float4 baseColor, emissionMetallic, factors;
    uint4 maps, flags;
    uint4 reserved0, reserved1, reserved2;
};
ConstantBuffer<SceneViewData> SceneView : register(b0);
ConstantBuffer<SceneLightingData> SceneLighting : register(b1);
ConstantBuffer<SceneShadowData> SceneShadow : register(b2);
StructuredBuffer<IndexedInstance> Objects : register(t0, space2);
StructuredBuffer<IndexedMaterial> Materials : register(t1, space2);
StructuredBuffer<uint> VisibleInstances : register(t2, space2);
Texture2D<float4> MaterialTextures[64] : register(t16, space1);
SamplerState MaterialSampler : register(s0, space1);
Texture2D<float4> EnvironmentAtlas : register(t8);
Texture2D<float4> BrdfLut : register(t9);
Texture2D<float4> MainLightShadow : register(t10);

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
    nointerpolation uint material : TEXCOORD3;
};
Varyings VSMain(Attributes input, DrawParameters draw)
{
    IndexedInstance object = Objects[VisibleInstances[GetInstanceIndex(draw)]];
    Varyings output;
    output.positionCS = TransformObjectToHClip(input.positionOS, object.world,
                                               SceneView.ViewProjection);
    output.positionWS = TransformObjectToWorld(input.positionOS, object.world);
    output.normalWS = TransformObjectToWorldNormal(input.normalOS, object.normal);
    output.uv = input.uv;
    output.material = object.indices.y;
    return output;
}
MaterialSurface Surface(Varyings input, bool frontFace)
{
    IndexedMaterial material = Materials[input.material];
    RasterSamplingContext context;
    context.uv = input.uv;
    context.uvDx = ddx(input.uv);
    context.uvDy = ddy(input.uv);
    context.positionDx = ddx(input.positionWS);
    context.positionDy = ddy(input.positionWS);
    context.normalWS = frontFace ? input.normalWS : -input.normalWS;
    MaterialFactors factors;
    factors.baseColor = material.baseColor;
    factors.metallic = material.emissionMetallic.w;
    factors.roughness = material.factors.x;
    factors.normalScale = material.factors.y;
    factors.occlusionStrength = material.factors.z;
    factors.emission = material.emissionMetallic.xyz;
    MaterialSurface surface = EvaluateMaterial(factors, context,
        MaterialTextures[NonUniformResourceIndex(material.maps.x)],
        MaterialTextures[NonUniformResourceIndex(material.maps.y)],
        MaterialTextures[NonUniformResourceIndex(material.maps.z)],
        MaterialTextures[NonUniformResourceIndex(material.maps.w)],
        MaterialTextures[NonUniformResourceIndex(material.flags.x)], MaterialSampler);
    if (material.flags.z != 0) ApplyAlphaCutoff(surface.alpha, material.factors.w);
    if (material.flags.y != 0)
    {
        surface.normalWS = input.normalWS;
        surface.perceptualRoughness = -1;
        surface.metallic = 0;
        surface.emission = 0;
        surface.occlusion = 1;
    }
    return surface;
}
float4 PSForward(Varyings input, bool frontFace : SV_IsFrontFace) : SV_Target
{
    MaterialSurface surface = Surface(input, frontFace);
    if (surface.perceptualRoughness < 0) return float4(surface.baseColor, 1);
    float3 shadowCoordinate = ProjectMainLightShadow(SceneShadow, input.positionWS,
                                                    SafeNormalize(surface.normalWS));
    RasterShadowSamplingContext shadowSampling = MakeShadowSamplingContext(
        shadowCoordinate, ddx(shadowCoordinate), ddy(shadowCoordinate));
    float visibility = MainLightVisibility(SceneShadow, MainLightShadow, shadowSampling);
    float3 viewDirWS = GetWorldSpaceViewDir(input.positionWS, SceneView.CameraPosition.xyz);
    DirectionalLight light = GetMainLight(SceneLighting);
    float3 direct = EvaluateBRDF(surface.baseColor, surface.metallic,
        surface.perceptualRoughness, surface.normalWS, SafeNormalize(viewDirWS), light.direction)
        * light.color * visibility;
    float3 indirect = EvaluateImageBasedLighting(surface, viewDirWS,
        SceneLighting.EnvironmentParameters, EnvironmentAtlas, BrdfLut);
    return float4(direct + indirect + surface.emission, 1);
}
struct GBufferOutput
{
    float4 albedoMetallic : SV_Target0;
    float4 normalRoughness : SV_Target1;
    float4 emissionOcclusion : SV_Target2;
};
GBufferOutput PSGBuffer(Varyings input, bool frontFace : SV_IsFrontFace)
{
    MaterialSurface surface = Surface(input, frontFace);
    // Preserve the baseline Unlit GBuffer's front/back face normal convention.
    if (surface.perceptualRoughness < 0 && !frontFace) surface.normalWS = -surface.normalWS;
    GBufferOutput output;
    output.albedoMetallic = float4(surface.baseColor, surface.metallic);
    output.normalRoughness = float4(surface.normalWS, surface.perceptualRoughness);
    output.emissionOcclusion = float4(surface.emission, surface.occlusion);
    return output;
}
