#include "RaySurface.hlsli"
#include "RaySampling.hlsli"
#include "ImageBasedLighting.hlsli"
RaytracingAccelerationStructure Scene : register(t0);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> Radiance : register(u1);
Texture2D<float> Depth : register(t2);
Texture2D<float4> NormalRoughness : register(t3);
cbuffer ReflectionParameters : register(b4)
{
    row_major float4x4 InverseViewProjection;
    float4 CameraPosition, DirectionToLight, LightColor, Environment;
    float4 Options;
};
Texture2D<float4> AlbedoMetallic : register(t12);
Texture2D<float4> EnvironmentAtlas : register(t13);
#ifndef RENDERLAB_REFLECTION_GUIDE
#define RENDERLAB_REFLECTION_GUIDE 1
#endif
#if RENDERLAB_REFLECTION_GUIDE
#ifdef SPIRV
[[vk::image_format("r32f")]]
#endif
RWTexture2D<float> HitGuide : register(u14);
#endif
Texture2D<float4> BrdfLut : register(t15);
struct Payload { float3 radiance; float distance; };

[shader("raygeneration")]
void RayGeneration()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 size = DispatchRaysDimensions().xy;
#if RENDERLAB_REFLECTION_GUIDE
    HitGuide[pixel] = 0;
#endif
    float depth = Depth.Load(int3(pixel, 0));
    float4 normalRoughness = NormalRoughness.Load(int3(pixel, 0));
    if (depth <= 1e-7 || normalRoughness.w < 0)
    {
        Radiance[pixel] = 0;
        return;
    }
    float2 ndc = float2((pixel.x + .5) * 2 / size.x - 1, 1 - (pixel.y + .5) * 2 / size.y);
    float4 world = mul(InverseViewProjection, float4(ndc, depth, 1));
    float3 position = world.xyz / world.w;
    float3 n = SafeNormalize(normalRoughness.xyz), v = SafeNormalize(CameraPosition.xyz - position);
    float roughness = clamp(normalRoughness.w, .045, 1);
    float2 xi = ReflectionSample(uint(Options.x), pixel.x + pixel.y * size.x);
    float3 tangent = SafeNormalize(cross(abs(n.y) < .999 ? float3(0,1,0) : float3(1,0,0), n));
    float3 bitangent = cross(n, tangent);
    float3 localView = float3(dot(v, tangent), dot(v, bitangent), dot(v, n));
    float3 localNormal = SampleVisibleGGX(localView, roughness * roughness, xi);
    float3 h = SafeNormalize(tangent * localNormal.x + bitangent * localNormal.y + n * localNormal.z);
    float3 direction = reflect(-v, h);
    float nl = dot(n, direction), nv = saturate(dot(n, v));
    if (nl <= 0 || nv <= 1e-5)
    {
        Radiance[pixel] = 0;
        return;
    }
    RayDesc ray;
    ray.Origin = position + n * .002;
    ray.Direction = direction;
    ray.TMin = .001;
    ray.TMax = 10000;
    Payload payload = {0.xxx, 0};
    TraceRay(Scene, RAY_FLAG_NONE, 0x02, 0, 1, 0, ray, payload);
    // Positive distance: hit, -1: environment miss, 0: rejected/invalid sample.
#if RENDERLAB_REFLECTION_GUIDE
    HitGuide[pixel] = payload.distance;
#endif
    float4 albedo = AlbedoMetallic.Load(int3(pixel, 0));
    float3 fresnel = FresnelSchlick(saturate(dot(v, h)), lerp(.04.xxx, albedo.rgb, albedo.a));
    float weight = VisibleGGXWeight(nv, nl, roughness);
    Radiance[pixel] = float4(payload.radiance * fresnel * weight, payload.distance > 0 ? 1 : 0);
}
[shader("miss")]
void Miss(inout Payload payload)
{
    payload.distance = -1;
    payload.radiance = EnvironmentTile(WorldRayDirection(), 0, EnvironmentAtlas, Environment);
}
[shader("closesthit")]
void ClosestHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    bool unlit;
    MaterialSurface surface = EvaluateRaySurface(InstanceID(), PrimitiveIndex(), attributes.barycentrics, unlit);
    payload.distance = RayTCurrent();
    if (unlit)
    {
        payload.radiance = surface.baseColor;
        return;
    }
    float3 position = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
    float visibility = 1;
    if (Options.z > 0 && LightColor.w > 0)
    {
        RayDesc ray;
        ray.Origin = position + surface.normalWS * .002;
        ray.Direction = SafeNormalize(DirectionToLight.xyz);
        ray.TMin = .001; ray.TMax = 10000;
        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
        query.TraceRayInline(Scene, RAY_FLAG_NONE, 0x01, ray);
        while (query.Proceed()) {}
        visibility = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0 : 1;
    }
    payload.radiance = surface.emission + EvaluateBRDF(surface.baseColor, surface.metallic,
        surface.perceptualRoughness, surface.normalWS, -WorldRayDirection(), SafeNormalize(DirectionToLight.xyz)) *
        LightColor.rgb * LightColor.w * visibility +
        EvaluateImageBasedLighting(surface, -WorldRayDirection(), Environment, EnvironmentAtlas, BrdfLut);
}
