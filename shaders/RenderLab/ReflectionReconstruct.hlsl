#include "ReflectionReconstruction.hlsli"
Texture2D<float4> Source : register(t0);
Texture2D<float4> Auxiliary : register(t1);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> Output : register(u2);
cbuffer TemporalParameters : register(b3)
{
    row_major float4x4 InverseViewProjection, PreviousViewProjection;
    float4 CameraPosition, PreviousCameraPosition;
    float4 Options;
};
Texture2D<float4> NormalRoughness : register(t4);
Texture2D<float> Depth : register(t5);
Texture2D<float4> AlbedoMetallic : register(t6);
Texture2D<float4> BrdfLut : register(t7);

bool Geometry(int2 q, uint2 size, float3 position, float3 n, float roughness, float footprint,
              out float weight)
{
    weight = 0;
    if (any(q < 0) || any(q >= int2(size))) return false;
    float d = Depth.Load(int3(q,0));
    float4 nr = NormalRoughness.Load(int3(q,0));
    if (d <= 1e-7 || nr.w < 0 || abs(nr.w-roughness) > .08) return false;
    float plane = abs(dot(ReflectionPosition(q,size,d,InverseViewProjection)-position,n));
    float agreement = saturate(dot(SafeNormalize(nr.xyz),n));
    if (plane > footprint || agreement < .9) return false;
    weight = exp(-plane/max(footprint*.25,1e-5)) * pow(agreement,64);
    return true;
}
float3 Demodulate(int2 pixel, uint2 size)
{
    float3 p = ReflectionPosition(pixel,size,Depth.Load(int3(pixel,0)),InverseViewProjection);
    float3 factor = ReflectionMaterial(p,NormalRoughness.Load(int3(pixel,0)),
        AlbedoMetallic.Load(int3(pixel,0)),CameraPosition.xyz,BrdfLut);
    return max(0,Source.Load(int3(pixel,0)).rgb)/factor;
}

[numthreads(8,8,1)]
void Prepare(uint3 id : SV_DispatchThreadID)
{
    uint width,height; Output.GetDimensions(width,height);
    if (id.x >= width || id.y >= height) return;
    int2 pixel = int2(id.xy); uint2 size = uint2(width,height);
    float4 nr = NormalRoughness.Load(int3(pixel,0));
    float d = Depth.Load(int3(pixel,0));
    Output[pixel] = 0;
    if (d <= 1e-7 || nr.w < 0) return;
    float3 p = ReflectionPosition(pixel,size,d,InverseViewProjection), n = SafeNormalize(nr.xyz);
    float footprint = ReflectionFootprint(pixel,size,d,p,InverseViewProjection);
    float3 sum = 0; float total = 0;
    float inverseDistance = 0, hitWeight = 0, missWeight = 0;
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
    {
        int2 q = pixel+int2(x,y);
        float weight;
        if (!Geometry(q,size,p,n,nr.w,footprint,weight)) continue;
        weight *= exp(-.75*(x*x+y*y));
        sum += Demodulate(q,size)*weight;
        total += weight;
        float hit = Auxiliary.Load(int3(q,0)).x;
        if (hit > 0)
        {
            inverseDistance += weight/max(.01,hit);
            hitWeight += weight;
        }
        else if (hit < 0) missWeight += weight;
    }
    // Preserve rare high-energy samples. A neighborhood cap lost real secondary highlights.
    float3 center = Demodulate(pixel,size);
    float blend = smoothstep(.05,.2,nr.w);
    float3 radiance = lerp(center,sum/max(total,1e-6),blend);
    // In-lobe harmonic distance is less sensitive to distant random hits. Never gates radiance.
    float distance = hitWeight > 0 ? hitWeight/max(inverseDistance,1e-6) : missWeight > 0 ? 10000 : 0;
    Output[pixel] = float4(radiance,distance);
}

void Filter(uint3 id, int stride)
{
    uint width,height; Output.GetDimensions(width,height);
    if (id.x >= width || id.y >= height) return;
    int2 pixel = int2(id.xy); uint2 size = uint2(width,height);
    float4 center = Source.Load(int3(pixel,0));
    Output[pixel] = center;
    float4 nr = NormalRoughness.Load(int3(pixel,0));
    float d = Depth.Load(int3(pixel,0));
    if (d <= 1e-7 || nr.w <= .05) return;
    float3 p = ReflectionPosition(pixel,size,d,InverseViewProjection), n = SafeNormalize(nr.xyz);
    float footprint = ReflectionFootprint(pixel,size,d,p,InverseViewProjection);
    float4 moments = Auxiliary.Load(int3(pixel,0));
    float luminance = ReflectionLuminance(center.rgb);
    float variance = max(0,moments.y-moments.x*moments.x);
    // A short history cannot estimate variance reliably. Give newly visible pixels a wider filter.
    float sigma = max(.02,3*sqrt(variance/max(center.a,1)) +
                           (.03+.5*saturate(1-center.a/8))*max(luminance,moments.z));
    float3 sum = center.rgb; float total = 1;
    for (int y = -2; y <= 2; ++y)
    for (int x = -2; x <= 2; ++x)
    {
        if (x == 0 && y == 0) continue;
        int2 q = pixel+int2(x,y)*stride;
        float weight;
        if (!Geometry(q,size,p,n,nr.w,footprint,weight)) continue;
        float3 value = Source.Load(int3(q,0)).rgb;
        float difference = abs(ReflectionLuminance(value)-luminance);
        weight *= exp(-.5*(x*x+y*y)-difference/sigma);
        sum += value*weight; total += weight;
    }
    float strength = smoothstep(.05,.2,nr.w);
    Output[pixel] = float4(lerp(center.rgb,sum/total,strength),center.a);
}
[numthreads(8,8,1)] void Filter1(uint3 id : SV_DispatchThreadID) { Filter(id,1); }
[numthreads(8,8,1)] void Filter2(uint3 id : SV_DispatchThreadID) { Filter(id,2); }
[numthreads(8,8,1)] void Filter4(uint3 id : SV_DispatchThreadID) { Filter(id,4); }

[numthreads(8,8,1)]
void Resolve(uint3 id : SV_DispatchThreadID)
{
    uint width,height; Output.GetDimensions(width,height);
    if (id.x >= width || id.y >= height) return;
    int2 pixel = int2(id.xy); uint2 size = uint2(width,height);
    float4 reference = Auxiliary.Load(int3(pixel,0));
    float4 nr = NormalRoughness.Load(int3(pixel,0));
    float d = Depth.Load(int3(pixel,0));
    Output[pixel] = reference;
    if (d <= 1e-7 || nr.w <= .05 || Options.y >= 2048) return;
    float3 p = ReflectionPosition(pixel,size,d,InverseViewProjection);
    float3 factor = ReflectionMaterial(p,nr,AlbedoMetallic.Load(int3(pixel,0)),CameraPosition.xyz,BrdfLut);
    // Fade only at display resolve; never weaken the recursive history or reuse progressive means.
    float blend = smoothstep(.05,.15,nr.w)*saturate(1-Options.y/2048);
    Output[pixel] = float4(lerp(reference.rgb,Source.Load(int3(pixel,0)).rgb*factor,blend),reference.a);
}
