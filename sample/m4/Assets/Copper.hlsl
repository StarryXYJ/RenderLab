#include "RenderLab/Lighting.hlsli"

struct Attributes { float3 positionOS : POSITION; float3 normalOS : NORMAL; float2 uv : TEXCOORD0; };
struct Varyings { float4 positionCS : SV_Position; float3 positionWS : TEXCOORD0; float3 normalWS : TEXCOORD1; float2 uv : TEXCOORD2; };
Varyings VSMain(Attributes input)
{
    Varyings output;
    output.positionCS = TransformObjectToHClip(input.positionOS);
    output.positionWS = TransformObjectToWorld(input.positionOS);
    output.normalWS = TransformObjectToWorldNormal(input.normalOS);
    output.uv = input.uv;
    return output;
}
float4 PSMain(Varyings input, bool frontFace : SV_IsFrontFace) : SV_Target
{
    float3 base = Material.BaseColor.rgb * BaseMap.Sample(BaseSampler, input.uv).rgb;
    return float4(LightingBlinnPhong(base, Material.SpecularColor, Material.Shininess,
        frontFace ? input.normalWS : -input.normalWS, GetWorldSpaceViewDir(input.positionWS)), 1);
}
