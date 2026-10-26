#include "../RenderLab/RaySurface.hlsli"
RaytracingAccelerationStructure Scene : register(t0);
RWStructuredBuffer<uint> Result : register(u1);
struct Payload { float3 radiance; uint hit; };
[shader("raygeneration")]
void RayGeneration()
{
    RayDesc ray;
    ray.Origin = float3(0, 0, -1);
    ray.Direction = float3(0, 0, 1);
    ray.TMin = .001;
    ray.TMax = 10;
    Payload payload = {0.xxx, 0};
    TraceRay(Scene, RAY_FLAG_NONE, 0xff, 0, 1, 0, ray, payload);
    Result[0] = payload.hit == 0 ? 0 : (payload.radiance.r > .5 && payload.radiance.g < .1 ? 1 : 2);
}
[shader("miss")]
void Miss(inout Payload payload) { payload.radiance = 0; }
[shader("closesthit")]
void ClosestHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    bool unlit;
    MaterialSurface surface = EvaluateRaySurface(InstanceID(), PrimitiveIndex(), attributes.barycentrics, unlit);
    payload.hit = 1;
    payload.radiance = surface.emission + EvaluateBRDF(surface.baseColor, surface.metallic,
        surface.perceptualRoughness, surface.normalWS, -WorldRayDirection(), float3(0, 0, -1));
}
