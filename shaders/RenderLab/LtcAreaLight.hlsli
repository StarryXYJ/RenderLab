#ifndef RENDERLAB_LTC_AREA_LIGHT_INCLUDED
#define RENDERLAB_LTC_AREA_LIGHT_INCLUDED

#include "BRDF.hlsli"
#include "RectAreaLight.hlsli"

// The 2016 selfshadow LTC tables are distributed with Ltc_LICENSE.txt.
// Heitz, Dupuy, Hill, Neubelt: Real-Time Polygonal-Light Shading with LTCs, TOG 35(4).
float4 LtcLookup(Texture2D<float4> table, float roughness, float nv)
{
    float2 coordinate = float2(saturate(roughness), sqrt(saturate(1 - nv))) * 63;
    int2 first = int2(floor(coordinate));
    int2 second = min(first + 1, 63);
    float2 weight = coordinate - first;
    float4 a = lerp(table.Load(int3(first.x, first.y, 0)),
                    table.Load(int3(second.x, first.y, 0)), weight.x);
    float4 b = lerp(table.Load(int3(first.x, second.y, 0)),
                    table.Load(int3(second.x, second.y, 0)), weight.x);
    return lerp(a, b, weight.y);
}
float LtcEdge(float3 a, float3 b)
{
    float cosine = clamp(dot(a, b), -1, 1);
    float sine = length(cross(a, b));
    if (sine < 1e-7) return 0;
    return cross(a, b).z * (acos(cosine) / (2 * RenderLabPi * sine));
}
// Integrate normalized cosine over a horizon-clipped convex quadrilateral.
float LtcPolygon(float3 vertices[4])
{
    float3 clipped[5];
    uint count = 0;
    float3 previous = vertices[3];
    [unroll] for (uint i = 0; i < 4; ++i)
    {
        float3 current = vertices[i];
        bool previousInside = previous.z > 0, currentInside = current.z > 0;
        if (previousInside != currentInside)
            clipped[count++] = lerp(previous, current, previous.z / (previous.z - current.z));
        if (currentInside) clipped[count++] = current;
        previous = current;
    }
    if (count < 3 || count > 5) return 0;
    [loop] for (uint i = 0; i < count; ++i)
    {
        float length2 = dot(clipped[i], clipped[i]);
        if (!(length2 > 1e-12) || !isfinite(length2)) return 0;
        clipped[i] *= rsqrt(length2);
    }
    float sum = 0;
    [loop] for (uint j = 0; j < count; ++j)
        sum += LtcEdge(clipped[j], clipped[(j + 1) % count]);
    return abs(sum);
}
float LtcEvaluatePolygon(RectEmitter light, float3 position, float3 tangent,
                         float3 bitangent, float3 normal, float4 matrix)
{
    float3 corners[4];
    corners[0] = light.center - .5 * light.edgeU - .5 * light.edgeV - position;
    corners[1] = light.center + .5 * light.edgeU - .5 * light.edgeV - position;
    corners[2] = light.center + .5 * light.edgeU + .5 * light.edgeV - position;
    corners[3] = light.center - .5 * light.edgeU + .5 * light.edgeV - position;
    [unroll] for (uint i = 0; i < 4; ++i)
    {
        float3 local = float3(dot(corners[i], tangent), dot(corners[i], bitangent),
                              dot(corners[i], normal));
        corners[i] = float3(matrix.x * local.x + matrix.z * local.z, local.y,
                            matrix.y * local.x + matrix.w * local.z);
    }
    return LtcPolygon(corners);
}
// Unshadowed direct radiance; shared by Forward and Deferred in the eventual integration.
float3 EvaluateRectLtc(RectEmitter light, float3 emitted, float3 position, float3 normal,
                       float3 view, float3 baseColor, float metallic, float roughness,
                       Texture2D<float4> matrixTable, Texture2D<float4> amplitudeTable)
{
    if (dot(light.normal, position - light.center) <= 0) return 0;
    normal = SafeNormalize(normal);
    view = SafeNormalize(view);
    float nv = saturate(dot(normal, view));
    if (nv <= 0) return 0;
    float3 tangent = view - normal * nv;
    if (dot(tangent, tangent) < 1e-8)
        tangent = cross(abs(normal.z) < .999 ? float3(0, 0, 1) : float3(0, 1, 0), normal);
    tangent = SafeNormalize(tangent);
    float3 bitangent = cross(normal, tangent);
    float4 matrix = LtcLookup(matrixTable, roughness, nv);
    float4 magnitude = LtcLookup(amplitudeTable, roughness, nv);
    float diffuseIntegral = LtcEvaluatePolygon(light, position, tangent, bitangent, normal,
                                               float4(1, 0, 0, 1));
    float specularIntegral = LtcEvaluatePolygon(light, position, tangent, bitangent, normal, matrix);
    float3 f0 = lerp(.04.xxx, baseColor, metallic);
    float3 lightDir = SafeNormalize(light.center - position);
    float3 fresnel = FresnelSchlick(saturate(dot(view, SafeNormalize(view + lightDir))), f0);
    float3 diffuse = (1 - fresnel) * (1 - metallic) * baseColor * diffuseIntegral;
    float3 specular = (f0 * magnitude.x + (1 - f0) * magnitude.y) * specularIntegral;
    return max(0, emitted * (diffuse + specular));
}
#endif
