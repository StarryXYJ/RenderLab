#include <renderlab/generated/parameters.hlsli>
#include "Color.hlsli"
float4 VSMain(float2 position : POSITION) : SV_Position
{
    return float4(position, 0, 1);
}
float4 PSMain(float4 position : SV_Position) : SV_Target
{
    uint width, height;
    HdrColor.GetDimensions(width, height);
    float2 sourceSize = float2(width, height);
    float scale = min(DisplayParameters.ViewportSize.x / width, DisplayParameters.ViewportSize.y / height);
    float2 offset = (DisplayParameters.ViewportSize - sourceSize * scale) * 0.5;
    float2 sourcePosition = (position.xy - offset) / scale;
    if (any(sourcePosition < 0) || any(sourcePosition >= sourceSize))
        return float4(0.02, 0.02, 0.02, 1);
    float3 hdr = HdrColor.Load(int3(int2(sourcePosition), 0)).rgb;
    // The sRGB attachment performs the only display encoding, including raw linear previews.
    return float4(DisplayParameters.RawView != 0 ? saturate(hdr)
        : ToneMapACES(hdr, DisplayParameters.Exposure), 1);
}
