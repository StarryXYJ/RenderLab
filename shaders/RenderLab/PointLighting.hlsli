#ifndef RENDERLAB_POINT_LIGHTING_INCLUDED
#define RENDERLAB_POINT_LIGHTING_INCLUDED
#include "BRDF.hlsli"

float3 EvaluatePointLights(SceneLightingData lights, float3 position, float3 normal,
                           float3 view, float3 baseColor, float metallic, float roughness)
{
    float3 result = 0;
    if (lights.PointLightCount.x > 0)
    {
        float4 pr = lights.PointPositionRange0;
        float4 ci = lights.PointColorIntensity0;
        float3 delta = pr.xyz - position;
        float distance2 = dot(delta, delta);
        float q = distance2 / (pr.w * pr.w);
        float falloff = saturate(1 - q * q);
        result += EvaluateBRDF(baseColor, metallic, roughness, normal, view,
                               delta * rsqrt(max(distance2, 1e-8))) * ci.rgb * ci.a *
                  (falloff * falloff / max(distance2, 1e-4));
    }
    if (lights.PointLightCount.x > 1)
    {
        float4 pr = lights.PointPositionRange1;
        float4 ci = lights.PointColorIntensity1;
        float3 delta = pr.xyz - position;
        float distance2 = dot(delta, delta);
        float q = distance2 / (pr.w * pr.w);
        float falloff = saturate(1 - q * q);
        result += EvaluateBRDF(baseColor, metallic, roughness, normal, view,
                               delta * rsqrt(max(distance2, 1e-8))) * ci.rgb * ci.a *
                  (falloff * falloff / max(distance2, 1e-4));
    }
    if (lights.PointLightCount.x > 2)
    {
        float4 pr = lights.PointPositionRange2;
        float4 ci = lights.PointColorIntensity2;
        float3 delta = pr.xyz - position;
        float distance2 = dot(delta, delta);
        float q = distance2 / (pr.w * pr.w);
        float falloff = saturate(1 - q * q);
        result += EvaluateBRDF(baseColor, metallic, roughness, normal, view,
                               delta * rsqrt(max(distance2, 1e-8))) * ci.rgb * ci.a *
                  (falloff * falloff / max(distance2, 1e-4));
    }
    if (lights.PointLightCount.x > 3)
    {
        float4 pr = lights.PointPositionRange3;
        float4 ci = lights.PointColorIntensity3;
        float3 delta = pr.xyz - position;
        float distance2 = dot(delta, delta);
        float q = distance2 / (pr.w * pr.w);
        float falloff = saturate(1 - q * q);
        result += EvaluateBRDF(baseColor, metallic, roughness, normal, view,
                               delta * rsqrt(max(distance2, 1e-8))) * ci.rgb * ci.a *
                  (falloff * falloff / max(distance2, 1e-4));
    }
    if (lights.PointLightCount.x > 4)
    {
        float4 pr = lights.PointPositionRange4;
        float4 ci = lights.PointColorIntensity4;
        float3 delta = pr.xyz - position;
        float distance2 = dot(delta, delta);
        float q = distance2 / (pr.w * pr.w);
        float falloff = saturate(1 - q * q);
        result += EvaluateBRDF(baseColor, metallic, roughness, normal, view,
                               delta * rsqrt(max(distance2, 1e-8))) * ci.rgb * ci.a *
                  (falloff * falloff / max(distance2, 1e-4));
    }
    if (lights.PointLightCount.x > 5)
    {
        float4 pr = lights.PointPositionRange5;
        float4 ci = lights.PointColorIntensity5;
        float3 delta = pr.xyz - position;
        float distance2 = dot(delta, delta);
        float q = distance2 / (pr.w * pr.w);
        float falloff = saturate(1 - q * q);
        result += EvaluateBRDF(baseColor, metallic, roughness, normal, view,
                               delta * rsqrt(max(distance2, 1e-8))) * ci.rgb * ci.a *
                  (falloff * falloff / max(distance2, 1e-4));
    }
    if (lights.PointLightCount.x > 6)
    {
        float4 pr = lights.PointPositionRange6;
        float4 ci = lights.PointColorIntensity6;
        float3 delta = pr.xyz - position;
        float distance2 = dot(delta, delta);
        float q = distance2 / (pr.w * pr.w);
        float falloff = saturate(1 - q * q);
        result += EvaluateBRDF(baseColor, metallic, roughness, normal, view,
                               delta * rsqrt(max(distance2, 1e-8))) * ci.rgb * ci.a *
                  (falloff * falloff / max(distance2, 1e-4));
    }
    if (lights.PointLightCount.x > 7)
    {
        float4 pr = lights.PointPositionRange7;
        float4 ci = lights.PointColorIntensity7;
        float3 delta = pr.xyz - position;
        float distance2 = dot(delta, delta);
        float q = distance2 / (pr.w * pr.w);
        float falloff = saturate(1 - q * q);
        result += EvaluateBRDF(baseColor, metallic, roughness, normal, view,
                               delta * rsqrt(max(distance2, 1e-8))) * ci.rgb * ci.a *
                  (falloff * falloff / max(distance2, 1e-4));
    }
    return result;
}
#endif
