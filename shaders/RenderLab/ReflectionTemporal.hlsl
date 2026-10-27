#include "ReflectionReconstruction.hlsli"
Texture2D<float4> Current : register(t0);
Texture2D<float4> Previous : register(t1);
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
Texture2D<float4> PreviousWorld : register(t4);
Texture2D<float4> PreviousNormal : register(t5);
Texture2D<float4> HitGuide : register(t6);
Texture2D<float> Depth : register(t7);
Texture2D<float4> NormalRoughness : register(t8);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> WorldGuide : register(u9);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> NormalGuide : register(u10);
Texture2D<float4> PreviousMoments : register(t11);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> Moments : register(u12);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> Diagnostics : register(u13);

struct History
{
    float3 radiance;
    float4 moments;
    float count, support, distance;
};
History Reproject(float3 target, float3 expectedSurface, float3 n, float roughness,
                  float footprint, uint2 size)
{
    History result = (History)0;
    float4 clip = mul(PreviousViewProjection,float4(target,1));
    if (clip.w <= 1e-7 || clip.z <= 0 || clip.z > clip.w) return result;
    float2 uv = float2(clip.x/clip.w*.5+.5,.5-clip.y/clip.w*.5);
    if (any(uv < 0) || any(uv >= 1)) return result;
    float2 previousPixel = uv*size-.5, fraction = frac(previousPixel);
    int2 base = int2(floor(previousPixel));
    for (int y = 0; y < 2; ++y)
    for (int x = 0; x < 2; ++x)
    {
        int2 q = base+int2(x,y);
        if (any(q < 0) || any(q >= int2(size))) continue;
        float4 oldNormal = PreviousNormal.Load(int3(q,0));
        float4 oldWorld = PreviousWorld.Load(int3(q,0));
        float4 old = Previous.Load(int3(q,0));
        float3 delta = oldWorld.xyz-expectedSurface;
        if (old.a < 1 || oldNormal.w < 0 || abs(oldNormal.w-roughness) > .04 ||
            dot(SafeNormalize(oldNormal.xyz),n) < .95 ||
            length(delta) > footprint*2 || abs(dot(delta,n)) > footprint*.5) continue;
        float weight = (x == 0 ? 1-fraction.x : fraction.x)*
                       (y == 0 ? 1-fraction.y : fraction.y);
        result.radiance += old.rgb*weight;
        result.moments += PreviousMoments.Load(int3(q,0))*weight;
        result.count += min(old.a,Options.w)*weight;
        result.distance += oldWorld.w*weight;
        result.support += weight;
    }
    if (result.support > 1e-5)
    {
        result.radiance /= result.support;
        result.moments /= result.support;
        result.count /= result.support;
        result.distance /= result.support;
    }
    return result;
}

[numthreads(8,8,1)]
void Temporal(uint3 id : SV_DispatchThreadID)
{
    uint width,height; Output.GetDimensions(width,height);
    if (id.x >= width || id.y >= height) return;
    int2 pixel = int2(id.xy); uint2 size = uint2(width,height);
    float3 current = Current.Load(int3(pixel,0)).rgb;
    float4 nr = NormalRoughness.Load(int3(pixel,0));
    float depth = Depth.Load(int3(pixel,0));
    WorldGuide[pixel] = 0; NormalGuide[pixel] = float4(0,0,0,-1);
    Output[pixel] = float4(current,0); Moments[pixel] = 0;
    if (Options.z > 0) Diagnostics[pixel] = 0;
    else if (all(pixel == 0)) Diagnostics[int2(0,0)] = 0;
    if (depth <= 1e-7 || nr.w < 0) return;
    float3 p = ReflectionPosition(pixel,size,depth,InverseViewProjection), n = SafeNormalize(nr.xyz);
    float distance = HitGuide.Load(int3(pixel,0)).w;
    WorldGuide[pixel] = float4(p,distance); NormalGuide[pixel] = nr;
    if (nr.w <= .05 || Options.y >= 2048) return;
    float footprint = ReflectionFootprint(pixel,size,depth,p,InverseViewProjection);
    float3 mean = 0, square = 0; float neighbors = 0;
    for (int y = -2; y <= 2; ++y)
    for (int x = -2; x <= 2; ++x)
    {
        int2 q = pixel+int2(x,y);
        if (any(q < 0) || any(q >= int2(size))) continue;
        float d = Depth.Load(int3(q,0));
        float4 other = NormalRoughness.Load(int3(q,0));
        if (d <= 1e-7 || other.w < 0 || abs(other.w-nr.w) > .04 ||
            dot(SafeNormalize(other.xyz),n) < .95 ||
            abs(dot(ReflectionPosition(q,size,d,InverseViewProjection)-p,n)) > footprint*.75) continue;
        float3 value = Current.Load(int3(q,0)).rgb;
        mean += value; square += value*value; neighbors += 1;
    }
    mean /= max(1,neighbors);
    float3 deviation = sqrt(max(0,square/max(1,neighbors)-mean*mean));
    float spatialVariance = dot(deviation*deviation,float3(.2126,.7152,.0722));
    float luminance = ReflectionLuminance(current);
    History history = (History)0;
    float path = 0;
    if (Options.x > 0)
    {
        history = Reproject(p,p,n,nr.w,footprint,size);
        path = history.support >= .1 ? 1 : 0;
        // Stabilize the in-lobe guide along the receiver; random distance deltas never gate reuse.
        if (history.support >= .1 && distance > 0 && distance < 9999 &&
            history.distance > 0 && history.distance < 9999)
            distance = lerp(distance,history.distance,.75);
        if (distance > 0 && distance < 9999 && nr.w < .4)
        {
            float3 view = SafeNormalize(CameraPosition.xyz-p);
            float3 virtualPosition = p-view*distance;
            float3 previousRay = virtualPosition-PreviousCameraPosition.xyz;
            float denominator = dot(previousRay,n);
            if (abs(denominator) > 1e-6)
            {
                float t = dot(p-PreviousCameraPosition.xyz,n)/denominator;
                if (t > 0 && t < 1)
                {
                    float3 expected = PreviousCameraPosition.xyz+previousRay*t;
                    History reflected = Reproject(virtualPosition,expected,n,nr.w,footprint,size);
                    float3 scale = max(.05,abs(mean)+deviation);
                    float reflectedError = length((reflected.radiance-mean)/scale);
                    float surfaceError = length((history.radiance-mean)/scale);
                    float hitError = abs(reflected.distance-distance)/max(.1,distance);
                    if (reflected.support >= .5 && hitError < .25+nr.w &&
                        (history.support < .1 || reflectedError <= surfaceError*1.2+.1))
                    {
                        history = reflected; path = 2;
                    }
                }
            }
        }
    }
    WorldGuide[pixel] = float4(p,distance);
    float count = history.support >= .1 ? history.count*saturate(history.support*2) : 0;
    // Normalize valid taps, then account for incomplete support in the actual effective length.
    float blend = count/(count+1);
    float3 lower = max(0,mean-3*deviation), upper = mean+3*deviation+.02;
    float3 clipped = clamp(history.radiance,lower,upper);
    float3 radiance = lerp(current,clipped,blend);
    float alpha = 1/(min(count,7)+1);
    float fast = lerp(luminance,history.moments.z,1-alpha);
    float first = lerp(luminance,history.moments.x,blend);
    float second = lerp(luminance*luminance,history.moments.y,blend);
    // A new pixel bootstraps variance spatially; one sample is not a clean signal.
    float variance = max(max(0,second-first*first),spatialVariance*saturate(1-count/8));
    Moments[pixel] = float4(first,first*first+variance,fast,spatialVariance);
    Output[pixel] = float4(radiance,min(count+1,Options.w));
    if (Options.z > 0)
        Diagnostics[pixel] = float4(blend,min(count+1,Options.w),count > 0 ? path : 0,distance);
}
