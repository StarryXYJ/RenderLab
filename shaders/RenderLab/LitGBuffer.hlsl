#include "LitSurface.hlsli"

struct GBufferOutput
{
    float4 albedoMetallic : SV_Target0;
    float4 normalRoughness : SV_Target1;
    float4 emissionOcclusion : SV_Target2;
};

GBufferOutput PSGBuffer(Varyings input, bool frontFace : SV_IsFrontFace)
{
    const MaterialSurface surface = EvaluateLitSurface(input, frontFace);
    GBufferOutput output;
    output.albedoMetallic = float4(surface.baseColor, surface.metallic);
    output.normalRoughness = float4(surface.normalWS, surface.perceptualRoughness);
    output.emissionOcclusion = float4(surface.emission, surface.occlusion);
    return output;
}
