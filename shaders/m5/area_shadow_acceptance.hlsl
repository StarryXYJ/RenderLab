#include "../RenderLab/AreaShadow.hlsli"
Texture2D<float4> ShadowMap : register(t1);
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
}
