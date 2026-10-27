#include "ImageBasedLighting.hlsli"
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

float3 Position(int2 pixel, uint2 size, float depth)
{
    float2 ndc = float2((pixel.x+.5)*2/size.x-1, 1-(pixel.y+.5)*2/size.y);
    float4 world = mul(InverseViewProjection, float4(ndc, depth, 1));
    return world.xyz/world.w;
}

[numthreads(8,8,1)]
void Temporal(uint3 id : SV_DispatchThreadID)
{
    uint width, height; Output.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;
    int2 pixel = int2(id.xy);
    uint2 size = uint2(width,height);
    float4 current = Current.Load(int3(pixel,0));
    float4 nr = NormalRoughness.Load(int3(pixel,0));
    float depth = Depth.Load(int3(pixel,0));
    WorldGuide[pixel] = 0;
    NormalGuide[pixel] = float4(0,0,0,-1);
    Output[pixel] = float4(current.rgb,0);
    if (depth <= 1e-7 || nr.w < 0) return;
    float3 position = Position(pixel,size,depth), n = SafeNormalize(nr.xyz);
    float distance = HitGuide.Load(int3(pixel,0)).x;
    WorldGuide[pixel] = float4(position,distance);
    NormalGuide[pixel] = nr;
    float strength = smoothstep(.05,.15,nr.w) * saturate(1-Options.y/2048) /
                     (1+Options.y/64);
    if (strength <= 0) return;
    Output[pixel] = float4(current.rgb,1);
    if (Options.x <= 0) return;
    float4 clip = mul(PreviousViewProjection,float4(position,1));
    if (clip.w <= 1e-7 || clip.z <= 0 || clip.z > clip.w) return;
    float2 uv = float2(clip.x/clip.w*.5+.5,.5-clip.y/clip.w*.5);
    if (any(uv < 0) || any(uv >= 1)) return;
    float footprint = max(length(Position(pixel+int2(1,0),size,depth)-position),
                          length(Position(pixel+int2(0,1),size,depth)-position));
    footprint = max(footprint,1e-5);
    float2 previousPixel = uv*size-.5, fraction = frac(previousPixel);
    int2 base = int2(floor(previousPixel));
    float3 history = 0;
    float count = 0, support = 0;
    // Test each bilinear tap before interpolation to avoid mixing an occluder into history.
    for (int y = 0; y < 2; ++y)
    for (int x = 0; x < 2; ++x)
    {
        int2 q = base+int2(x,y);
        if (any(q < 0) || any(q >= int2(size))) continue;
        float4 oldNormal = PreviousNormal.Load(int3(q,0));
        float4 oldWorld = PreviousWorld.Load(int3(q,0));
        float4 old = Previous.Load(int3(q,0));
        float3 delta = oldWorld.xyz-position;
        if (old.a < 1 || oldNormal.w < 0 || abs(oldNormal.w-nr.w) > .04 ||
            dot(SafeNormalize(oldNormal.xyz),n) < .95 ||
            length(delta) > footprint*2 || abs(dot(delta,n)) > footprint*.5) continue;
        float weight = (x == 0 ? 1-fraction.x : fraction.x) *
                       (y == 0 ? 1-fraction.y : fraction.y);
        // Distance is stochastic: use soft confidence, never assume exact correspondence.
        if (distance > 0 && oldWorld.w > 0)
            weight *= exp(-abs(distance-oldWorld.w) /
                          max(.01,min(distance,oldWorld.w)*(.2+2*nr.w)));
        else if ((distance > 0) != (oldWorld.w > 0))
            weight *= lerp(.1,.8,nr.w);
        history += old.rgb*weight;
        count += min(old.a,Options.w)*weight;
        support += weight;
    }
    if (support < .1) return;
    history /= support;
    count /= support;
    float3 low = current.rgb, high = current.rgb;
    float3 mean = current.rgb, square = current.rgb*current.rgb;
    float neighbors = 1;
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
    {
        int2 q = pixel+int2(x,y);
        if ((x == 0 && y == 0) || any(q < 0) || any(q >= int2(size))) continue;
        float d = Depth.Load(int3(q,0));
        float4 other = NormalRoughness.Load(int3(q,0));
        if (d <= 1e-7 || other.w < 0 || abs(other.w-nr.w) > .04 ||
            dot(SafeNormalize(other.xyz),n) < .95 ||
            abs(dot(Position(q,size,d)-position,n)) > footprint*.75) continue;
        float3 value = Current.Load(int3(q,0)).rgb;
        low = min(low,value); high = max(high,value);
        mean += value; square += value*value; neighbors += 1;
    }
    mean /= neighbors;
    float3 deviation = sqrt(max(0,square/neighbors-mean*mean));
    history = clamp(history,max(low,mean-2*deviation),min(high,mean+2*deviation));
    float3 view = SafeNormalize(CameraPosition.xyz-position);
    float3 oldView = SafeNormalize(PreviousCameraPosition.xyz-position);
    float viewConfidence = exp(-64*(1-saturate(dot(view,oldView)))/max(.01,nr.w*nr.w));
    float blend = count/(count+1) * strength * saturate(support) * viewConfidence;
    Output[pixel] = float4(lerp(current.rgb,history,blend),min(count+1,Options.w));
}
