#include "RaySurface.hlsli"
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
Texture2D<float4> BrdfLut : register(t15);
struct Payload { float3 radiance; uint hit; };

uint Hash(uint value)
{
    value ^= value >> 16; value *= 0x7feb352d;
    value ^= value >> 15; value *= 0x846ca68b;
    return value ^ (value >> 16);
}
float Random(inout uint seed)
{
    seed = Hash(seed);
    return (seed >> 8) * (1.0 / 16777216.0);
}
[shader("raygeneration")]
void RayGeneration()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 size = DispatchRaysDimensions().xy;
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
    uint seed = Hash(pixel.x + pixel.y * size.x + uint(Options.x) * 230400 + 1);
    float2 xi = float2(Random(seed), Random(seed));
    float a = roughness * roughness;
    float cosTheta = sqrt((1 - xi.y) / (1 + (a * a - 1) * xi.y));
    float sinTheta = sqrt(max(0, 1 - cosTheta * cosTheta));
    float3 tangent = SafeNormalize(cross(abs(n.y) < .999 ? float3(0,1,0) : float3(1,0,0), n));
    float3 h = SafeNormalize(tangent * (cos(2 * RenderLabPi * xi.x) * sinTheta) +
        cross(n, tangent) * (sin(2 * RenderLabPi * xi.x) * sinTheta) + n * cosTheta);
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
    float4 albedo = AlbedoMetallic.Load(int3(pixel, 0));
    float3 fresnel = FresnelSchlick(saturate(dot(v, h)), lerp(.04.xxx, albedo.rgb, albedo.a));
    float weight = 4 * VisibilitySmithGGX(nv, nl, roughness) * nl * saturate(dot(v, h)) /
        max(dot(n, h), 1e-5);
    Radiance[pixel] = float4(payload.radiance * fresnel * weight, payload.hit);
}
[shader("miss")]
void Miss(inout Payload payload)
{
    payload.radiance = EnvironmentTile(WorldRayDirection(), 0, EnvironmentAtlas, Environment);
}
[shader("closesthit")]
void ClosestHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    bool unlit;
    MaterialSurface surface = EvaluateRaySurface(InstanceID(), PrimitiveIndex(), attributes.barycentrics, unlit);
    payload.hit = 1;
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
