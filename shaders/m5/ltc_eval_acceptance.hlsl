#include "../RenderLab/LtcAreaLight.hlsli"
RWStructuredBuffer<float4> Result : register(u0);
Texture2D<float4> MatrixTable : register(t1);
Texture2D<float4> AmplitudeTable : register(t2);
[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint scenario = id.x;
    RectEmitter light;
    light.center = float3(scenario == 2 ? .7 : 0, 0, scenario == 3 ? .4 : 2);
    light.edgeU = float3(2, 0, scenario == 3 ? 2 : 0);
    light.edgeV = float3(scenario == 2 ? .6 : 0, 2, 0);
    light.normal = scenario == 3 ? normalize(float3(1, 0, -1)) : float3(0, 0, -1);
    light.area = 4;
    float nv = scenario == 2 ? .5 : 1;
    float3 view = float3(sqrt(1 - nv * nv), 0, nv);
    float metallic = scenario == 1 ? 1 : 0;
    float roughness = scenario == 1 ? .5 : .8;
    float3 value = EvaluateRectLtc(light, 1, 0, float3(0, 0, 1), view,
                                   float3(.7, .4, .2), metallic, roughness,
                                   MatrixTable, AmplitudeTable);
    Result[scenario] = float4(value, 1);
    light.normal = -light.normal;
    Result[scenario + 4] = float4(EvaluateRectLtc(light, 1, 0, float3(0, 0, 1), view,
                                                   float3(.7, .4, .2), metallic, roughness,
                                                   MatrixTable, AmplitudeTable), 0);
}
