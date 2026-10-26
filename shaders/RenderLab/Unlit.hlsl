#include "Core.hlsli"
#include "Alpha.hlsli"
struct Attributes { float3 positionOS : POSITION; float3 normalOS : NORMAL; float2 uv : TEXCOORD0; };
struct Varyings { float4 positionCS : SV_Position; float2 uv : TEXCOORD0; };
Varyings VSMain(Attributes input)
{
    Varyings output;
    output.positionCS = TransformObjectToHClip(input.positionOS);
    output.uv = input.uv;
    return output;
}
float4 PSMain(Varyings input) : SV_Target
{
    float4 color = Material.BaseColor * BaseMap.Sample(BaseSampler, input.uv);
    ApplyAlphaCutoff(color.a);
    return float4(color.rgb, SurfaceAlpha(color.a));
}
