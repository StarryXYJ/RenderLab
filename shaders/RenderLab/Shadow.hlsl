#include "Core.hlsli"
#include "Alpha.hlsli"
struct Attributes { float3 positionOS : POSITION; float3 normalOS : NORMAL; float2 uv : TEXCOORD0; };
struct Varyings { float4 positionCS : SV_Position; float2 uv : TEXCOORD0; };
Varyings VSMain(Attributes input)
{
    Varyings output;
    output.positionCS = mul(Frame.MainLightShadowMatrix,
        float4(TransformObjectToWorld(input.positionOS), 1));
    output.uv = input.uv;
    return output;
}
float4 PSMain(Varyings input) : SV_Target
{
#if RENDERLAB_ALPHA_CLIP
    ApplyAlphaCutoff(Material.BaseColor.a * BaseMap.Sample(BaseSampler, input.uv).a, Material.AlphaCutoff);
#endif
    return float4(input.positionCS.zzz, 1);
}
