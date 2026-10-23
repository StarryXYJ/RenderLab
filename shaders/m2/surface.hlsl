#include <renderlab/generated/parameters.hlsli>
#include "tools/color.hlsli"

struct VertexInput
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
};
struct Varyings
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
Varyings VSMain(VertexInput input)
{
    Varyings output;
    output.position = mul(View.ViewProjection, float4(input.position, 1)) + Draw.Offset;
    output.uv = input.uv;
    return output;
}
float4 PSMain(Varyings input) : SV_Target0
{
    float4 sampled = BaseMap.Sample(BaseSampler, input.uv);
    return float4(ApplyDetail(Material.BaseColor.rgb) * sampled.rgb, Material.BaseColor.a);
}
float4 PSAlternate(Varyings input) : SV_Target0
{
    return float4(Material.Roughness, input.uv * 0, 1);
}
