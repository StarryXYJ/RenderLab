#ifndef RENDERLAB_CORE_INCLUDED
#define RENDERLAB_CORE_INCLUDED
#include <renderlab/generated/parameters.hlsli>
#include "Math.hlsli"

float3 TransformObjectToWorld(float3 p) { return mul(Draw.ObjectToWorld, float4(p, 1)).xyz; }
float3 TransformWorldToView(float3 p) { return mul(Frame.View, float4(p, 1)).xyz; }
float4 TransformWorldToHClip(float3 p) { return mul(Frame.ViewProjection, float4(p, 1)); }
float4 TransformObjectToHClip(float3 p) { return TransformWorldToHClip(TransformObjectToWorld(p)); }
float3 TransformObjectToWorldDir(float3 d) { return SafeNormalize(mul((float3x3)Draw.ObjectToWorld, d)); }
float3 TransformObjectToWorldNormal(float3 n) { return SafeNormalize(mul((float3x3)Draw.NormalToWorld, n)); }
float3 GetWorldSpaceViewDir(float3 p) { return Frame.CameraPosition.xyz - p; }
#endif
