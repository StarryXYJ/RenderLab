#ifndef RENDERLAB_BRDF_INCLUDED
#define RENDERLAB_BRDF_INCLUDED
#include "Math.hlsli"
static const float RenderLabPi = 3.14159265359;
float3 FresnelSchlick(float cosTheta, float3 f0)
{
    float f = pow(1 - saturate(cosTheta), 5);
    return f0 + (1 - f0) * f;
}
float DistributionGGX(float nh, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = nh * nh * (a2 - 1) + 1;
    return a2 / max(RenderLabPi * d * d, 1e-8);
}
// Height-correlated Smith visibility: includes the 1/(4*NdotL*NdotV) factor.
float VisibilitySmithGGX(float nv, float nl, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float v = nl * sqrt(nv * nv * (1 - a2) + a2);
    float l = nv * sqrt(nl * nl * (1 - a2) + a2);
    return 0.5 / max(v + l, 1e-6);
}
float3 EvaluateBRDF(float3 baseColor, float metallic, float roughness,
                    float3 n, float3 v, float3 l)
{
    float nl = saturate(dot(n, l));
    float nv = saturate(dot(n, v));
    float3 h = SafeNormalize(v + l);
    float3 f = FresnelSchlick(saturate(dot(v, h)), lerp(0.04.xxx, baseColor, metallic));
    float3 diffuse = (1 - f) * (1 - metallic) * baseColor / RenderLabPi;
    float3 specular = f * DistributionGGX(saturate(dot(n, h)), roughness)
        * VisibilitySmithGGX(nv, nl, roughness);
    return (diffuse + specular) * nl;
}
#endif
