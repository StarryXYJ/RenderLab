#include "../RenderLab/AreaShadow.hlsli"
#include "../RenderLab/AreaVisibilityFilter.hlsli"
Texture2D<float4> ShadowMap : register(t1);
Texture2D<float4> FilterDepth : register(t2);
Texture2D<float4> FilterNormal : register(t3);
RWStructuredBuffer<float4> Result : register(u0);
[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    float4x4 matrix = float4x4(1, 0, 0, 0, 0, 1, 0, 0,
                               0, 0, 0, .00970291, 0, 0, 0, 3);
    float4 parameters = float4(1, .0000009, 0, 0);
    float4 smallLight = float4(.01, .01, 100, .01);
    float4 largeLight = float4(4, 4, 100, .01);
    float small = SampleAreaShadow(matrix, parameters, smallLight, ShadowMap, float3(-.09, 0, 0), 0, 0);
    float large = SampleAreaShadow(matrix, parameters, largeLight, ShadowMap, float3(-.09, 0, 0), 0, 0);
    float blocked = SampleAreaShadow(matrix, parameters, largeLight, ShadowMap, float3(-2, 0, 0), 0, 0);
    float open = SampleAreaShadow(matrix, parameters, largeLight, ShadowMap, float3(1, 0, 0), 0, 0);
    Result[0] = float4(small, large, blocked, open);
    float nearBlocker = SampleAreaShadow(matrix, parameters, largeLight, ShadowMap, float3(-.09, 1, 0), 0, 0);
    float farBlocker = SampleAreaShadow(matrix, parameters, largeLight, ShadowMap, float3(-.09, -1, 0), 0, 0);
    float selfShadow = SampleAreaShadow(matrix, parameters, largeLight, ShadowMap, float3(2, 0, 0), 0, 0);
    float4x4 slopeMatrix = float4x4(1, 0, 0, 0, 0, 1, 0, 0,
                                    0, -.0005, 0, .00970291, 0, 0, 0, 3);
    float2 gradient = AreaShadowDepthGradient(slopeMatrix, parameters, float3(2, 0, 0), float3(0, 0, 1));
    float corrected = SampleAreaShadow(slopeMatrix, parameters, largeLight, ShadowMap,
                                       float3(2, 0, 0), float3(0, 0, 1), gradient);
    Result[1] = float4(nearBlocker, farBlocker, selfShadow, corrected);
    Result[2] = float4(gradient, 0, 0);
    // Fine receiver motion across a flat shadow edge should not jump in whole taps.
    [loop] for (uint i = 0; i < 64; ++i)
    {
        float x = -.3 + .6 * i / 63;
        float visibility = SampleAreaShadow(matrix, parameters, largeLight, ShadowMap,
                                            float3(x, 1, 0), 0, 0);
        Result[3 + i] = float4(visibility, 0, 0, 0);
        float y = -.4 + .8 * i / 63;
        float depthTransition = SampleAreaShadow(matrix, parameters, largeLight, ShadowMap,
                                                 float3(-.09, y, 0), 0, 0);
        Result[67 + i] = float4(depthTransition, 0, 0, 0);
    }
    float2 smoothLeft = FilterAreaVisibility(ShadowMap, FilterDepth, FilterNormal, int2(31, 3), int2(64, 64), .5, float3(0, 0, 1), .01);
    float2 smoothRight = FilterAreaVisibility(ShadowMap, FilterDepth, FilterNormal, int2(32, 3), int2(64, 64), .5, float3(0, 0, 1), .01);
    float2 depthLeft = FilterAreaVisibility(ShadowMap, FilterDepth, FilterNormal, int2(31, 7), int2(64, 64), .5, float3(0, 0, 1), .01);
    float2 depthRight = FilterAreaVisibility(ShadowMap, FilterDepth, FilterNormal, int2(32, 7), int2(64, 64), .8, float3(0, 0, 1), .01);
    float2 normalLeft = FilterAreaVisibility(ShadowMap, FilterDepth, FilterNormal, int2(31, 11), int2(64, 64), .5, float3(0, 0, 1), .01);
    float2 normalRight = FilterAreaVisibility(ShadowMap, FilterDepth, FilterNormal, int2(32, 11), int2(64, 64), .5, float3(1, 0, 0), .01);
    Result[131] = float4(smoothLeft.x, smoothRight.x, depthLeft.x, depthRight.x);
    Result[132] = float4(normalLeft.x, normalRight.x, smoothLeft.y, smoothRight.y);
}
