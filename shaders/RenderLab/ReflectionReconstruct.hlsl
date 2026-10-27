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
Texture2D<float4> Progressive : register(t8);
StructuredBuffer<float2> Statistics : register(t9);

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

float3 ReconstructionSignal(int2 pixel, uint2 size, bool firstScale, out float variance)
{
    float4 temporal = Source.Load(int3(pixel,0));
    if (!firstScale)
    {
        variance = temporal.a;
        return temporal.rgb;
    }
    float4 moments = Auxiliary.Load(int3(pixel,0));
    float2 statistics = Statistics[pixel.y*size.x+pixel.x];
    float count = max(1,Options.y);
    variance = statistics.x/max(1,count-1);
    float3 p = ReflectionPosition(pixel,size,Depth.Load(int3(pixel,0)),InverseViewProjection);
    float3 factor = ReflectionMaterial(p,NormalRoughness.Load(int3(pixel,0)),
        AlbedoMetallic.Load(int3(pixel,0)),CameraPosition.xyz,BrdfLut);
    float3 progressive = Progressive.Load(int3(pixel,0)).rgb/factor;
    // Sample support and measured uncertainty continuously reduce the finite-history prior.
    float mean = ReflectionLuminance(progressive);
    float tolerance = ReflectionNoiseTolerance(mean);
    float instant = variance/(variance+tolerance*tolerance);
    float support = max(0,count-1)/(temporal.a+count);
    float weight = max(statistics.y,instant)*temporal.a/(temporal.a+max(0,count-1));
    float3 difference = progressive-temporal.rgb;
    float uncertainty = variance+tolerance*tolerance/max(.01,1-statistics.y);
    // A stationary mean that disagrees beyond its uncertainty should not inherit prior bias.
    weight /= 1+support*dot(difference,difference)/(3*uncertainty);
    float temporalVariance = max(0,moments.y-moments.x*moments.x)/max(1,temporal.a);
    variance = lerp(variance,temporalVariance,weight);
    if (count < 8) variance = max(variance,temporalVariance*saturate(1-count/8));
    return lerp(progressive,temporal.rgb,weight);
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
    float variance;
    float3 signal = ReconstructionSignal(pixel,size,stride == 1,variance);
    float luminance = ReflectionLuminance(signal);
    float bootstrap = .02+.5*saturate(1-Options.y/8);
    float sigma = max(.02,3*sqrt(variance)+bootstrap*luminance);
    float control = Statistics[pixel.y*width+pixel.x].y;
    float instant = ReflectionNoise(variance,luminance);
    // Retire broad scales first. Control is temporally stabilized before any spatial gather.
    float scaleControl = stride == 1 ? saturate(4*max(control,instant)) : pow(control,stride);
    float strength = smoothstep(.05,.15,nr.w)*scaleControl;
    float3 sum = signal; float total = 1, neighborVariance = 0;
    for (int y = -2; y <= 2; ++y)
    for (int x = -2; x <= 2; ++x)
    {
        if (x == 0 && y == 0) continue;
        int2 q = pixel+int2(x,y)*stride;
        float weight;
        if (!Geometry(q,size,p,n,nr.w,footprint,weight)) continue;
        float otherVariance;
        float3 value = ReconstructionSignal(q,size,stride == 1,otherVariance);
        float guide = stride == 1 ? ReflectionLuminance(Source.Load(int3(pixel,0)).rgb)
                                 : luminance;
        float otherGuide = stride == 1 ? ReflectionLuminance(Source.Load(int3(q,0)).rgb) :
                                        ReflectionLuminance(value);
        float difference = abs(otherGuide-guide);
        weight *= exp(-(stride == 1 ? .25 : .5)*(x*x+y*y)-difference/sigma);
        sum += value*weight; total += weight;
        neighborVariance += weight*weight*otherVariance;
    }
    float centerWeight = 1-strength+strength/total;
    float filteredVariance = centerWeight*centerWeight*variance+
        strength*strength/(total*total)*neighborVariance;
    // Spatial and temporal samples overlap; retain a conservative covariance floor.
    filteredVariance = max(variance*.25,filteredVariance);
    Output[pixel] = float4(lerp(signal,sum/total,strength),filteredVariance);
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
    float strength = smoothstep(.05,.15,nr.w)*saturate(1-Options.y/2048);
    Output[pixel] = float4(lerp(reference.rgb,Source.Load(int3(pixel,0)).rgb*factor,strength),reference.a);
}
