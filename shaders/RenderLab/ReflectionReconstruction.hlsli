#ifndef RENDERLAB_REFLECTION_RECONSTRUCTION_INCLUDED
#define RENDERLAB_REFLECTION_RECONSTRUCTION_INCLUDED
#include "ImageBasedLighting.hlsli"

float ReflectionLuminance(float3 value)
{
    return dot(max(value,0),float3(.2126,.7152,.0722));
}
float ReflectionNoiseTolerance(float mean)
{
    return .1+.2*max(0,mean);
}
float ReflectionNoise(float variance, float mean)
{
    float tolerance = ReflectionNoiseTolerance(mean);
    return variance/(variance+tolerance*tolerance);
}
float3 ReflectionPosition(int2 pixel, uint2 size, float depth, float4x4 inverseVP)
{
    float2 ndc = float2((pixel.x+.5)*2/size.x-1,1-(pixel.y+.5)*2/size.y);
    float4 world = mul(inverseVP,float4(ndc,depth,1));
    return world.xyz/world.w;
}
float ReflectionFootprint(int2 pixel, uint2 size, float depth, float3 position, float4x4 inverseVP)
{
    return max(1e-5,max(
        length(ReflectionPosition(pixel+int2(1,0),size,depth,inverseVP)-position),
        length(ReflectionPosition(pixel+int2(0,1),size,depth,inverseVP)-position)));
}
float3 ReflectionMaterial(float3 position, float4 nr, float4 albedo,
                          float3 camera, Texture2D<float4> lut)
{
    float nv = saturate(dot(SafeNormalize(nr.xyz),SafeNormalize(camera-position)));
    float2 brdf = EnvironmentBrdf(nv,clamp(nr.w,.045,1),lut);
    // A floor avoids unstable division on dark metals; the same factor is restored at resolve.
    return max(.02,lerp(.04.xxx,albedo.rgb,albedo.a)*brdf.x+brdf.y);
}
#endif
