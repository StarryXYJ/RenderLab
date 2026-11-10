#ifndef RENDERLAB_AREA_LIGHTING_INCLUDED
#define RENDERLAB_AREA_LIGHTING_INCLUDED
#include "LtcAreaLight.hlsli"
float3 EvaluateAreaLights(SceneAreaLightingData lights, float3 position, float3 normal,
                          float3 view, float3 baseColor, float metallic, float roughness,
                          Texture2D<float4> matrixTable, Texture2D<float4> amplitudeTable)
{
    float3 result = 0;
    [loop] for (uint i = 0; i < min((uint)lights.Header.x, 2u); ++i)
    {
        float4 center = i == 0 ? lights.Center0 : lights.Center1;
        float4 u = i == 0 ? lights.EdgeU0 : lights.EdgeU1;
        float4 v = i == 0 ? lights.EdgeV0 : lights.EdgeV1;
        RectEmitter light;
        light.center = center.xyz;
        light.edgeU = u.xyz;
        light.edgeV = v.xyz;
        float3 crossUV = cross(light.edgeU, light.edgeV);
        light.area = length(crossUV);
        light.normal = -crossUV / light.area;
        result += EvaluateRectLtc(light, float3(center.w, u.w, v.w), position, normal,
                                  view, baseColor, metallic, roughness,
                                  matrixTable, amplitudeTable);
    }
    return result;
}
#endif
