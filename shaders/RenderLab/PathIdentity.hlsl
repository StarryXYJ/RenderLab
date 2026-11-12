#include "RaySampling.hlsli"
#include "RectAreaLight.hlsli"

RaytracingAccelerationStructure Scene : register(t0);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> Identity : register(u1);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> PreviousPosition : register(u2);
StructuredBuffer<float4> PreviousTransforms : register(t3);
cbuffer PathParameters : register(b4)
{
    row_major float4x4 InverseViewProjection;
    float4 CameraPosition, DirectionToLight, LightColor, Environment;
    float4 Options;
    float4 RectLightHeader;
    float4 RectLightData[6];
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    Identity.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;
    uint pixel = PathSamplePixel(id.x + id.y * width, uint(Environment.w));
    float2 jitter = PathSample(uint(Options.x), pixel, 0);
    float2 ndc = float2((id.x + jitter.x) * 2 / width - 1,
                        1 - (id.y + jitter.y) * 2 / height);
    float4 projected = mul(InverseViewProjection, float4(ndc, .0001, 1));
    RayDesc ray;
    ray.Origin = CameraPosition.xyz;
    ray.Direction = SafeNormalize(projected.xyz / projected.w - ray.Origin);
    ray.TMin = .001;
    ray.TMax = 10000;
    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(Scene, RAY_FLAG_NONE, 0x02, ray);
    while (query.Proceed()) {}
    bool hit = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    float distance = hit ? query.CommittedRayT() : ray.TMax;
    float kind = hit ? 1 : 0;
    float index = hit ? float(query.CommittedInstanceID()) : 0;
    [loop] for (uint i = 0; i < min((uint)RectLightHeader.x, 2u); ++i)
    {
        RectEmitter light;
        light.center = RectLightData[3 * i].xyz;
        light.edgeU = RectLightData[3 * i + 1].xyz;
        light.edgeV = RectLightData[3 * i + 2].xyz;
        float3 areaNormal = cross(light.edgeU, light.edgeV);
        light.area = length(areaNormal);
        light.normal = light.area > 0 ? -areaNormal / light.area : 0;
        float lightDistance;
        if (IntersectRectEmitter(light, ray.Origin, ray.Direction, ray.TMin, distance, lightDistance))
        {
            kind = 2;
            index = float(i);
            distance = lightDistance;
        }
    }
    Identity[id.xy] = float4(kind, index, 0, distance);
    PreviousPosition[id.xy] = 0;
    if (kind == 1)
    {
        uint base = uint(index) * 4;
        if (PreviousTransforms[base + 3].w > 0)
        {
            float4 current = float4(ray.Origin + ray.Direction * distance, 1);
            PreviousPosition[id.xy] = float4(dot(PreviousTransforms[base], current),
                                               dot(PreviousTransforms[base + 1], current),
                                               dot(PreviousTransforms[base + 2], current), 1);
        }
    }
}
