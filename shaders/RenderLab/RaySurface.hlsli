#ifndef RENDERLAB_RAY_SURFACE_INCLUDED
#define RENDERLAB_RAY_SURFACE_INCLUDED
#include "Surface.hlsli"
#include "BRDF.hlsli"

struct RayMeshData { uint vertexOffset; uint indexOffset; uint2 reserved; };
struct RayInstanceData { uint mesh; uint material; uint2 reserved; };
struct RayMaterialData
{
    float4 baseMetallic;
    float4 emissionRoughness;
    uint4 maps;
    uint4 normalFlags;
    float4 options;
};
struct RayVertexData { float4 positionNormalX; float4 normalYZUV; };
StructuredBuffer<RayVertexData> RayVertices : register(t5);
StructuredBuffer<uint> RayIndices : register(t6);
StructuredBuffer<RayMeshData> RayMeshes : register(t7);
StructuredBuffer<RayInstanceData> RayInstances : register(t8);
StructuredBuffer<RayMaterialData> RayMaterials : register(t9);
Texture2D<float4> RayAtlas : register(t10);
SamplerState RaySampler : register(s11);

float4 SampleRayMap(uint tile, float2 uv)
{
    // 64x64 payload, one clamped border texel, eight columns. One mip in the current RHI.
    float2 origin = float2(tile % 8, tile / 8) * 66 + 1;
    return RayAtlas.SampleLevel(RaySampler, (origin + saturate(uv) * 64) / 528, 0);
}
MaterialSurface EvaluateRaySurface(uint instanceIndex, uint primitive, float2 barycentrics,
                                   out bool unlit)
{
    RayInstanceData instance = RayInstances[instanceIndex];
    RayMeshData mesh = RayMeshes[instance.mesh];
    RayMaterialData material = RayMaterials[instance.material];
    uint firstIndex = mesh.indexOffset + primitive * 3;
    uint3 vertexIndices = uint3(RayIndices[firstIndex], RayIndices[firstIndex + 1], RayIndices[firstIndex + 2]) + mesh.vertexOffset;
    float3 weights = float3(1 - barycentrics.x - barycentrics.y, barycentrics);
    float3 n = 0;
    float2 uv = 0;
    float3 positions[3];
    float2 uvs[3];
    [unroll] for (uint i = 0; i < 3; ++i)
    {
        RayVertexData vertex = RayVertices[vertexIndices[i]];
        positions[i] = vertex.positionNormalX.xyz;
        uvs[i] = vertex.normalYZUV.zw;
        n += float3(vertex.positionNormalX.w, vertex.normalYZUV.xy) * weights[i];
        uv += uvs[i] * weights[i];
    }
    n = SafeNormalize(mul(n, (float3x3)WorldToObject3x4()));
    if (dot(n, WorldRayDirection()) > 0) n = -n;
    float3 p1 = mul(ObjectToWorld3x4(), float4(positions[1] - positions[0], 0));
    float3 p2 = mul(ObjectToWorld3x4(), float4(positions[2] - positions[0], 0));
    float2 d1 = uvs[1] - uvs[0], d2 = uvs[2] - uvs[0];
    float determinant = d1.x * d2.y - d1.y * d2.x;
    if (abs(determinant) > 1e-8)
    {
        float3 tangent = SafeNormalize((p1 * d2.y - p2 * d1.y) / determinant);
        tangent = SafeNormalize(tangent - n * dot(n, tangent));
        float3 bitangentUV = (p2 * d1.x - p1 * d2.x) / determinant;
        float handedness = dot(cross(n, tangent), bitangentUV) < 0 ? -1 : 1;
        float3 bitangent = SafeNormalize(cross(n, tangent)) * handedness;
        float3 normalTS = SampleRayMap(material.normalFlags.x, uv).xyz * 2 - 1;
        normalTS.xy *= material.options.x;
        n = SafeNormalize(tangent * normalTS.x + bitangent * normalTS.y + n * normalTS.z);
    }
    float4 mr = SampleRayMap(material.maps.y, uv);
    MaterialSurface surface;
    surface.baseColor = material.baseMetallic.rgb * SampleRayMap(material.maps.x, uv).rgb;
    surface.alpha = 1;
    surface.normalWS = n;
    surface.metallic = saturate(material.baseMetallic.w * mr.b);
    surface.perceptualRoughness = clamp(material.emissionRoughness.w * mr.g, .045, 1);
    surface.emission = material.emissionRoughness.rgb * SampleRayMap(material.maps.w, uv).rgb;
    surface.occlusion = lerp(1, SampleRayMap(material.maps.z, uv).r, material.options.y);
    unlit = material.normalFlags.y != 0;
    return surface;
}
#endif
