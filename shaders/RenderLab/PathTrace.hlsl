#include "RaySurface.hlsli"
#include "PathBsdf.hlsli"
#include "ImageBasedLighting.hlsli"

RaytracingAccelerationStructure Scene : register(t0);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> Radiance : register(u1);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> SurfaceGuide : register(u2);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> MaterialGuide : register(u3);
RWStructuredBuffer<float2> BatchMoments : register(u17);
cbuffer PathParameters : register(b4)
{
    row_major float4x4 InverseViewProjection;
    float4 CameraPosition, DirectionToLight, LightColor, Environment;
    float4 Options;
};
Texture2D<float4> EnvironmentAtlas : register(t13);

struct PathPayload
{
    float3 position;
    uint hit;
    float3 normal;
    uint unlit;
    float3 albedo;
    float metallic;
    float3 emission;
    float roughness;
};

float3 TracePath(uint2 pixel, uint2 size, uint sampleIndex, out float4 surfaceGuide,
                 out float4 materialGuide)
{
    uint pixelIndex = pixel.x + pixel.y * size.x;
    float2 jitter = PathSample(sampleIndex, pixelIndex, 0);
    float2 ndc = float2((pixel.x + jitter.x) * 2 / size.x - 1,
                        1 - (pixel.y + jitter.y) * 2 / size.y);
    float4 world = mul(InverseViewProjection, float4(ndc, .0001, 1));
    RayDesc ray;
    ray.Origin = CameraPosition.xyz;
    ray.Direction = SafeNormalize(world.xyz / world.w - ray.Origin);
    ray.TMin = .001;
    ray.TMax = 10000;
    float3 throughput = 1;
    float3 result = 0;
    surfaceGuide = 0;
    materialGuide = 0;
    [loop] for (uint bounce = 0; bounce < 4; ++bounce)
    {
        PathPayload payload = (PathPayload)0;
        TraceRay(Scene, RAY_FLAG_NONE, 0x02, 0, 1, 0, ray, payload);
        if (bounce == 0 && payload.hit != 0)
        {
            // Negative distance bypasses filtering for Unlit surfaces.
            surfaceGuide = float4(payload.normal,
                length(payload.position - CameraPosition.xyz) * (payload.unlit != 0 ? -1 : 1));
            materialGuide = float4(payload.albedo, payload.roughness);
        }
        if (payload.hit == 0)
        {
            result += throughput * EnvironmentTile(ray.Direction, 0, EnvironmentAtlas, Environment);
            break;
        }
        if (payload.unlit != 0)
        {
            result += throughput * payload.albedo;
            break;
        }
        result += throughput * payload.emission;
        float3 lightDirection = SafeNormalize(DirectionToLight.xyz);
        float nl = saturate(dot(payload.normal, lightDirection));
        if (nl > 0 && LightColor.w > 0)
        {
            bool visible = true;
            if (Options.z > 0)
            {
                RayDesc shadow;
                shadow.Origin = payload.position + payload.normal * .002;
                shadow.Direction = lightDirection;
                shadow.TMin = .001;
                shadow.TMax = 10000;
                RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
                query.TraceRayInline(Scene, RAY_FLAG_NONE, 0x01, shadow);
                while (query.Proceed()) {}
                visible = query.CommittedStatus() != COMMITTED_TRIANGLE_HIT;
            }
            if (visible)
                result += throughput * EvaluateBRDF(payload.albedo, payload.metallic,
                    payload.roughness, payload.normal, -ray.Direction, lightDirection) *
                    LightColor.rgb * LightColor.w;
        }
        float2 xi = PathSample(sampleIndex, pixelIndex, bounce + 1);
        float choice = PathSample(sampleIndex, pixelIndex, bounce + 5).x;
        float3 direction = SamplePathBsdf(payload.normal, -ray.Direction, payload.roughness,
                                          xi, choice);
        float pdf = PathBsdfPdf(payload.normal, -ray.Direction, direction, payload.roughness);
        if (pdf <= 0) break;
        throughput *= EvaluateBRDF(payload.albedo, payload.metallic, payload.roughness,
                                   payload.normal, -ray.Direction, direction) / pdf;
        ray.Origin = payload.position + payload.normal * .002;
        ray.Direction = direction;
    }
    return result;
}

[shader("raygeneration")]
void RayGeneration()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 size = DispatchRaysDimensions().xy;
    uint pixelIndex = pixel.x + pixel.y * size.x;
    uint count = min(uint(Environment.z), uint(Options.w) - min(uint(Options.y), uint(Options.w)));
    float3 sum = 0;
    float mean = 0, m2 = 0;
    float4 guide = 0, material = 0;
    [loop] for (uint i = 0; i < count; ++i)
    {
        float4 sampleGuide, sampleMaterial;
        float3 sample = TracePath(pixel, size, uint(Options.x) + i, sampleGuide, sampleMaterial);
        if (i == 0) { guide = sampleGuide; material = sampleMaterial; }
        sum += sample;
        float lum = dot(sample, float3(.2126, .7152, .0722));
        float delta = lum - mean;
        mean += delta / (i + 1);
        m2 += delta * (lum - mean);
    }
    SurfaceGuide[pixel] = guide;
    MaterialGuide[pixel] = material;
    Radiance[pixel] = float4(count > 0 ? sum / count : 0, 1);
    BatchMoments[pixelIndex] = float2(mean, m2);
}

[shader("miss")]
void Miss(inout PathPayload payload) { payload.hit = 0; }

[shader("closesthit")]
void ClosestHit(inout PathPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    bool unlit;
    MaterialSurface surface = EvaluateRaySurface(InstanceID(), PrimitiveIndex(),
                                                  attributes.barycentrics, unlit);
    payload.hit = 1;
    payload.position = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
    payload.normal = surface.normalWS;
    payload.unlit = unlit ? 1 : 0;
    payload.albedo = surface.baseColor;
    payload.metallic = surface.metallic;
    payload.roughness = surface.perceptualRoughness;
    payload.emission = surface.emission;
}
