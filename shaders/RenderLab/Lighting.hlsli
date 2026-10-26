#ifndef RENDERLAB_LIGHTING_INCLUDED
#define RENDERLAB_LIGHTING_INCLUDED
#include <renderlab/generated/types.hlsli>
#include "Math.hlsli"

struct DirectionalLight { float3 direction; float3 color; };
DirectionalLight GetMainLight(SceneLightingData lighting)
{
    DirectionalLight light;
    light.direction = lighting.MainLightDirection.xyz;
    light.color = lighting.MainLightColor.rgb * lighting.MainLightColor.a;
    return light;
}
float3 LightingBlinnPhong(float3 baseColor, float3 specularColor, float shininess,
                          float3 normalWS, float3 viewDirWS, SceneLightingData lighting)
{
    DirectionalLight light = GetMainLight(lighting);
    float3 n = SafeNormalize(normalWS);
    float diffuse = saturate(dot(n, light.direction));
    float3 h = SafeNormalize(light.direction + SafeNormalize(viewDirWS));
    float specular = diffuse > 0 ? pow(saturate(dot(n, h)), max(shininess, 1)) : 0;
    return baseColor * lighting.AmbientColor.rgb
         + light.color * (baseColor * diffuse + specularColor * specular);
}
#endif
