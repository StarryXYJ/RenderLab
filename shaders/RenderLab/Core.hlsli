#ifndef RENDERLAB_CORE_INCLUDED
#define RENDERLAB_CORE_INCLUDED
#include "Math.hlsli"

float3 TransformObjectToWorld(float3 p, float4x4 objectToWorld)
{
    return mul(objectToWorld, float4(p, 1)).xyz;
}
float3 TransformWorldToView(float3 p, float4x4 worldToView)
{
    return mul(worldToView, float4(p, 1)).xyz;
}
float4 TransformWorldToHClip(float3 p, float4x4 viewProjection)
{
    return mul(viewProjection, float4(p, 1));
}
float4 TransformObjectToHClip(float3 p, float4x4 objectToWorld, float4x4 viewProjection)
{
    return TransformWorldToHClip(TransformObjectToWorld(p, objectToWorld), viewProjection);
}
float3 TransformObjectToWorldDir(float3 d, float4x4 objectToWorld)
{
    return SafeNormalize(mul((float3x3)objectToWorld, d));
}
float3 TransformObjectToWorldNormal(float3 n, float4x4 normalToWorld)
{
    return SafeNormalize(mul((float3x3)normalToWorld, n));
}
float3 GetWorldSpaceViewDir(float3 p, float3 cameraPositionWS)
{
    return cameraPositionWS - p;
}
#endif
