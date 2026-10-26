#include <renderlab/generated/parameters.hlsli>
#include "Color.hlsli"
float4 VSMain(float2 position : POSITION) : SV_Position
{
    return float4(position, 0, 1);
}
float4 PSMain(float4 position : SV_Position) : SV_Target
{
    // One output texel per input texel. The sRGB attachment performs the only display encoding.
    float3 hdr = HdrColor.Load(int3(int2(position.xy), 0)).rgb;
    return float4(ToneMapACES(hdr, DisplayParameters.Exposure), 1);
}
