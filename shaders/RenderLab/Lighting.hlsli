#ifndef RENDERLAB_LIGHTING_INCLUDED
#define RENDERLAB_LIGHTING_INCLUDED
#include "Core.hlsli"

struct DirectionalLight { float3 direction; float3 color; };
DirectionalLight GetMainLight()
{
    DirectionalLight light;
    light.direction = Frame.MainLightDirection.xyz;
    light.color = Frame.MainLightColor.rgb * Frame.MainLightColor.a;
    return light;
}
float3 LightingBlinnPhong(float3 baseColor, float3 specularColor, float shininess,
                          float3 normalWS, float3 viewDirWS)
{
    DirectionalLight light = GetMainLight();
    float3 n = SafeNormalize(normalWS);
    float diffuse = saturate(dot(n, light.direction));
    float3 h = SafeNormalize(light.direction + SafeNormalize(viewDirWS));
    float specular = diffuse > 0 ? pow(saturate(dot(n, h)), max(shininess, 1)) : 0;
    return baseColor * Frame.AmbientColor.rgb
         + light.color * (baseColor * diffuse + specularColor * specular);
}
#endif
