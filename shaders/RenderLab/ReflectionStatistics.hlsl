#include "ReflectionReconstruction.hlsli"
Texture2D<float4> Current : register(t0);
Texture2D<float4> Previous : register(t1);
RWStructuredBuffer<float2> Output : register(u2);
// Per progressive slot: unfiltered centered M2/N and stabilized spatial-filter control.
cbuffer ReflectionParameters : register(b3)
{
    row_major float4x4 InverseViewProjection;
    float4 CameraPosition, DirectionToLight, LightColor, Environment;
    float4 Options;
};
StructuredBuffer<float2> PreviousStatistics : register(t4);
Texture2D<float4> NormalRoughness : register(t5);
Texture2D<float4> AlbedoMetallic : register(t6);
Texture2D<float> Depth : register(t7);
Texture2D<float4> BrdfLut : register(t8);

float3 Material(int2 pixel, uint2 size)
{
    float3 p = ReflectionPosition(pixel,size,Depth.Load(int3(pixel,0)),InverseViewProjection);
    return ReflectionMaterial(p,NormalRoughness.Load(int3(pixel,0)),
        AlbedoMetallic.Load(int3(pixel,0)),CameraPosition.xyz,BrdfLut);
}

[numthreads(8,8,1)]
void Statistics(uint3 id : SV_DispatchThreadID)
{
    uint width,height; Current.GetDimensions(width,height);
    if (id.x >= width || id.y >= height) return;
    int2 pixel = int2(id.xy); uint2 size = uint2(width,height);
    uint index = id.y*width+id.x;
    if (Options.y >= Options.w)
    {
        Output[index] = PreviousStatistics[index];
        return;
    }
    float d = Depth.Load(int3(pixel,0));
    float4 nr = NormalRoughness.Load(int3(pixel,0));
    Output[index] = 0;
    if (d <= 1e-7 || nr.w <= .05) return;
    float3 factor = Material(pixel,size);
    float sample = ReflectionLuminance(Current.Load(int3(pixel,0)).rgb/factor);
    float count = Options.y+1;
    float2 old = 0;
    float mean = sample, moment = 0;
    if (Options.y > 0)
    {
        old = PreviousStatistics[index];
        float previousMean = ReflectionLuminance(Previous.Load(int3(pixel,0)).rgb/factor);
        float delta = sample-previousMean;
        mean = previousMean+delta/count;
        // Welford's centered M2/N stays bounded and consumes the original sample exactly once.
        moment = max(0,old.x+(delta*(sample-mean)-old.x)/count);
    }
    float varianceOfMean = moment/max(1,count-1);
    // Stabilize the uncertainty estimate along the primary surface; M2 itself stays unfiltered.
    {
        float3 p = ReflectionPosition(pixel,size,d,InverseViewProjection), n = SafeNormalize(nr.xyz);
        float footprint = ReflectionFootprint(pixel,size,d,p,InverseViewProjection);
        float sum = 0, square = 0, total = 0, spatialMoment = 0;
        for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
        {
            int2 q = pixel+int2(x,y);
            if (any(q < 0) || any(q >= int2(size))) continue;
            float depth = Depth.Load(int3(q,0));
            float4 normal = NormalRoughness.Load(int3(q,0));
            if (depth <= 1e-7 || abs(normal.w-nr.w) > .04 ||
                dot(SafeNormalize(normal.xyz),n) < .95 ||
                abs(dot(ReflectionPosition(q,size,depth,InverseViewProjection)-p,n)) > footprint*.75)
                continue;
            float value = count < 8 ?
                ReflectionLuminance(Current.Load(int3(q,0)).rgb/Material(q,size)) : 0;
            if (Options.y > 0) spatialMoment += PreviousStatistics[q.y*width+q.x].x;
            sum += value; square += value*value; total += 1;
        }
        float spatial = max(0,square/max(1,total)-pow(sum/max(1,total),2));
        varianceOfMean = max(varianceOfMean,spatial/count*saturate(1-count/8));
        varianceOfMean = max(varianceOfMean,spatialMoment/max(1,total)/max(1,count-1));
    }
    float target = ReflectionNoise(varianceOfMean,mean);
    // Start conservatively; bounded changes avoid abrupt expansion after a late bright sample.
    float control = count <= 1 ? 1 : old.y+clamp((target-old.y)*.125,-.04,.02);
    Output[index] = float2(moment,saturate(control));
}
