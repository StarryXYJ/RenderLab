#ifndef RENDERLAB_PATH_BSDF_INCLUDED
#define RENDERLAB_PATH_BSDF_INCLUDED
#include "RaySampling.hlsli"

// Equal-probability mixture of cosine diffuse and visible-GGX reflection.
float PathBsdfPdf(float3 n, float3 v, float3 l, float roughness)
{
    float nv = dot(n, v), nl = dot(n, l);
    if (nv <= 0 || nl <= 0) return 0;
    float3 h = SafeNormalize(v + l);
    float alpha = roughness * roughness;
    float root = sqrt(alpha * alpha + (1 - alpha * alpha) * nv * nv);
    float specularPdf = DistributionGGX(saturate(dot(n, h)), roughness) / (2 * (nv + root));
    return .5 * (nl / RenderLabPi + specularPdf);
}

float3 SamplePathBsdf(float3 n, float3 v, float roughness, float2 xi, float choice)
{
    float3 tangent = SafeNormalize(cross(abs(n.y) < .999 ? float3(0, 1, 0) :
                                        float3(1, 0, 0), n));
    float3 bitangent = cross(n, tangent);
    if (choice < .5)
    {
        float3 local = SampleCosineHemisphere(xi);
        return SafeNormalize(tangent * local.x + bitangent * local.y + n * local.z);
    }
    float3 localView = float3(dot(v, tangent), dot(v, bitangent), dot(v, n));
    float3 localNormal = SampleVisibleGGX(localView, roughness * roughness, xi);
    float3 halfVector = SafeNormalize(tangent * localNormal.x + bitangent * localNormal.y +
                                      n * localNormal.z);
    return reflect(-v, halfVector);
}
#endif
