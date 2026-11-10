#include "RectAreaLight.hlsli"
RaytracingAccelerationStructure Scene : register(t0);
Texture2D<float> CameraDepth : register(t1);
Texture2D<float4> NormalRoughness : register(t2);
cbuffer RayQueryParameters : register(b3)
{
    row_major float4x4 InverseViewProjection;
    float4 DirectionToLight;
    // Normal bias, ray minimum distance, ray maximum distance, enabled.
    float4 ShadowOptions;
    float4 RectLightHeader;
    float4 RectLightData[6];
};
#ifdef SPIRV
[[vk::image_format("rgba8")]]
#endif
RWTexture2D<float4> Visibility : register(u4);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    Visibility.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
        return;
    float visibility = 1;
    const float depth = CameraDepth.Load(int3(id.xy, 0));
    if (depth > 1e-7 && ShadowOptions.w > 0)
    {
        const float2 ndc = float2((id.x + 0.5) * 2 / width - 1,
                                  1 - (id.y + 0.5) * 2 / height);
        const float4 world = mul(InverseViewProjection, float4(ndc, depth, 1));
        const float3 normal = normalize(NormalRoughness.Load(int3(id.xy, 0)).xyz);
        RayDesc ray;
        ray.Origin = world.xyz / world.w + normal * ShadowOptions.x;
        ray.Direction = normalize(DirectionToLight.xyz);
        ray.TMin = ShadowOptions.y;
        ray.TMax = ShadowOptions.z;
        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
        query.TraceRayInline(Scene, RAY_FLAG_NONE, 0x01, ray);
        while (query.Proceed()) {}
        visibility = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0 : 1;
    }
    float2 areaVisibility = 1;
    if (depth > 1e-7 && NormalRoughness.Load(int3(id.xy, 0)).w >= 0)
    {
        float2 ndc = float2((id.x + .5) * 2 / width - 1, 1 - (id.y + .5) * 2 / height);
        float4 world = mul(InverseViewProjection, float4(ndc, depth, 1));
        float3 position = world.xyz / world.w;
        float3 normal = normalize(NormalRoughness.Load(int3(id.xy, 0)).xyz);
        [loop] for (uint i = 0; i < min((uint)RectLightHeader.x, 2u); ++i)
        {
            float4 center = RectLightData[3 * i];
            float4 u = RectLightData[3 * i + 1];
            float4 v = RectLightData[3 * i + 2];
            float visible = 0;
            [unroll] for (uint y = 0; y < 4; ++y)
            [unroll] for (uint x = 0; x < 4; ++x)
            {
                float3 samplePosition = center.xyz + ((x + .5) / 4 - .5) * u.xyz + ((y + .5) / 4 - .5) * v.xyz;
                float3 origin = position + normal * ShadowOptions.x;
                float3 delta = samplePosition - origin;
                float distance = length(delta);
                if (distance <= ShadowOptions.y * 2) continue;
                RayDesc ray;
                ray.Origin = origin;
                ray.Direction = delta / distance;
                ray.TMin = ShadowOptions.y;
                ray.TMax = distance - ShadowOptions.y;
                RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
                query.TraceRayInline(Scene, RAY_FLAG_NONE, 0x01, ray);
                while (query.Proceed()) {}
                bool blocked = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
                [loop] for (uint j = 0; j < min((uint)RectLightHeader.x, 2u); ++j)
                {
                    if (i == j) continue;
                    RectEmitter other;
                    other.center = RectLightData[3 * j].xyz;
                    other.edgeU = RectLightData[3 * j + 1].xyz;
                    other.edgeV = RectLightData[3 * j + 2].xyz;
                    float3 areaNormal = cross(other.edgeU, other.edgeV);
                    other.area = length(areaNormal);
                    other.normal = other.area > 0 ? -areaNormal / other.area : 0;
                    float hitDistance;
                    blocked = blocked || IntersectRectEmitter(other, ray.Origin, ray.Direction,
                                                               ray.TMin, ray.TMax, hitDistance);
                }
                visible += blocked ? 0 : 1;
            }
            areaVisibility[i] = visible / 16;
        }
    }
    Visibility[id.xy] = float4(visibility, areaVisibility, 1);
}
