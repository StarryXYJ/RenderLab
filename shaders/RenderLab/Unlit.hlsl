#include <renderlab/generated/parameters.hlsli>
#include "Core.hlsli"
#include "Alpha.hlsli"
struct Attributes { float3 positionOS : POSITION; float3 normalOS : NORMAL; float2 uv : TEXCOORD0; };
struct Varyings
{
    float4 positionCS : SV_Position;
    float2 uv : TEXCOORD0;
    float3 normalWS : TEXCOORD1;
};
Varyings VSMain(Attributes input)
{
    Varyings output;
    output.positionCS = TransformObjectToHClip(input.positionOS, SceneObject.ObjectToWorld, SceneView.ViewProjection);
    output.uv = input.uv;
    output.normalWS = TransformObjectToWorldNormal(input.normalOS, SceneObject.NormalToWorld);
    return output;
}

struct GBufferOutput
{
    float4 albedoMetallic : SV_Target0;
    float4 normalRoughness : SV_Target1;
    float4 emissionOcclusion : SV_Target2;
};

GBufferOutput PSGBuffer(Varyings input, bool frontFace : SV_IsFrontFace)
{
    float4 color = Material.BaseColor * BaseMap.Sample(BaseSampler, input.uv);
#if RENDERLAB_ALPHA_CLIP
    ApplyAlphaCutoff(color.a, Material.AlphaCutoff);
#endif
    GBufferOutput output;
    output.albedoMetallic = float4(color.rgb, 0);
    output.normalRoughness = float4(frontFace ? input.normalWS : -input.normalWS, -1);
    output.emissionOcclusion = float4(0, 0, 0, 1);
    return output;
}
float4 PSMain(Varyings input) : SV_Target
{
    float4 color = Material.BaseColor * BaseMap.Sample(BaseSampler, input.uv);
#if RENDERLAB_ALPHA_CLIP
    ApplyAlphaCutoff(color.a, Material.AlphaCutoff);
#endif
    return EncodeSurfaceOutput(color.rgb, color.a, Material.Surface, Material.Blend);
}
