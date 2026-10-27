#include "ImageBasedLighting.hlsli"
Texture2D<float4> Radiance : register(t0);
Texture2D<float> HitGuide : register(t1);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> Output : register(u2);
cbuffer ReflectionParameters : register(b3)
{
    row_major float4x4 InverseViewProjection;
    float4 CameraPosition, DirectionToLight, LightColor, Environment;
    float4 Options;
};
Texture2D<float4> NormalRoughness : register(t5);
Texture2D<float4> AlbedoMetallic : register(t6);
Texture2D<float> Depth : register(t7);

float3 Position(int2 pixel, uint2 size, float depth)
{
    float2 ndc = float2((pixel.x + .5) * 2 / size.x - 1, 1 - (pixel.y + .5) * 2 / size.y);
    float4 world = mul(InverseViewProjection, float4(ndc, depth, 1));
    return world.xyz / world.w;
}

[numthreads(8,8,1)]
void Denoise(uint3 id : SV_DispatchThreadID)
{
    uint width, height; Output.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;
    int2 pixel = int2(id.xy);
    uint2 size = uint2(width, height);
    float4 center = Radiance.Load(int3(pixel, 0));
    float4 nr = NormalRoughness.Load(int3(pixel, 0));
    float depth = Depth.Load(int3(pixel, 0));
    float samples = min(Options.y + 1, Options.w);
    // Fade to the unfiltered estimator; sharp mirrors must not be spatially blurred.
    float strength = smoothstep(.05, .15, nr.w) * saturate(1 - samples / 2048) / (1 + samples / 64);
    if (depth <= 1e-7 || nr.w < 0 || strength <= 0)
    {
        Output[pixel] = center;
        return;
    }
    float3 position = Position(pixel, size, depth), n = SafeNormalize(nr.xyz);
    float4 material = AlbedoMetallic.Load(int3(pixel, 0));
    float distance = HitGuide.Load(int3(pixel, 0)).x;
    // Estimate a world-space pixel footprint from the actual projection at this depth.
    float footprint = max(length(Position(pixel + int2(1,0), size, depth) - position),
                          length(Position(pixel + int2(0,1), size, depth) - position));
    float planeTolerance = max(footprint * .25, 1e-5);
    float3 sum = center.rgb;
    float total = 1;
    for (int y = -2; y <= 2; ++y)
    for (int x = -2; x <= 2; ++x)
    {
        int2 q = pixel + int2(x,y);
        if ((x == 0 && y == 0) || any(q < 0) || any(q >= int2(size))) continue;
        float d = Depth.Load(int3(q, 0));
        float4 neighbor = NormalRoughness.Load(int3(q, 0));
        if (d <= 1e-7 || neighbor.w < 0) continue;
        float3 delta = Position(q, size, d) - position;
        if (abs(dot(delta,n)) > 3 * planeTolerance) continue;
        float normalAgreement = saturate(dot(n, SafeNormalize(neighbor.xyz)));
        float4 otherMaterial = AlbedoMetallic.Load(int3(q, 0));
        float weight = exp(-.3 * (x*x + y*y)) * pow(normalAgreement, 64);
        weight *= exp(-abs(dot(delta,n)) / planeTolerance);
        weight *= exp(-32 * abs(neighbor.w - nr.w) - 16 * abs(otherMaterial.a - material.a));
        weight *= exp(-8 * length(otherMaterial.rgb - material.rgb));
        // This is a stochastic, current-frame guide, not a stable reflected geometry buffer.
        // Use a soft penalty at rough surfaces; a hard hit/miss gate biases low-spp radiance.
        float hitDistance = HitGuide.Load(int3(q, 0)).x;
        if (distance > 0 && hitDistance > 0)
            weight *= exp(-abs(distance-hitDistance) / max(.01, min(distance,hitDistance) * (.1 + 2*nr.w)));
        else if ((distance > 0) != (hitDistance > 0))
            weight *= lerp(.1, .75, nr.w);
        sum += Radiance.Load(int3(q, 0)).rgb * weight;
        total += weight;
    }
    // Preserve accumulated hit fraction in alpha; filtered radiance is display-only.
    Output[pixel] = float4(lerp(center.rgb, sum / total, strength), center.a);
}
