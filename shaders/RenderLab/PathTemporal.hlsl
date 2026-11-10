#include "RaySampling.hlsli"
Texture2D<float4> Current : register(t0);
Texture2D<float4> Surface : register(t1);
Texture2D<float4> Material : register(t2);
cbuffer TemporalParameters : register(b3)
{
    row_major float4x4 InverseViewProjection, PreviousViewProjection;
    float4 CameraPosition, PreviousCameraPosition;
    float4 Options;
};
Texture2D<float4> Previous : register(t4);
Texture2D<float4> PreviousWorld : register(t5);
Texture2D<float4> PreviousNormal : register(t6);
Texture2D<float4> PreviousMaterial : register(t7);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> Output : register(u8);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> World : register(u9);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> Normal : register(u10);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> MaterialHistory : register(u11);
Texture2D<float4> Filtered : register(t12);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> Display : register(u13);

[numthreads(8,8,1)]
void Temporal(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    Output.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;
    uint2 pixel = id.xy, size = uint2(width, height);
    float3 current = Current.Load(int3(pixel, 0)).rgb;
    float4 surface = Surface.Load(int3(pixel, 0));
    float4 material = Material.Load(int3(pixel, 0));
    float4 filtered = Filtered.Load(int3(pixel, 0));
    Output[pixel] = float4(current, 0);
    World[pixel] = 0;
    Normal[pixel] = 0;
    MaterialHistory[pixel] = material;
    Display[pixel] = filtered;
    if (surface.w <= 0) return;
    float2 jitter = PathSample(uint(Options.z), pixel.x + pixel.y * width, 0);
    float2 ndc = float2((pixel.x + jitter.x) * 2 / width - 1,
                        1 - (pixel.y + jitter.y) * 2 / height);
    float4 projected = mul(InverseViewProjection, float4(ndc, .0001, 1));
    float3 direction = SafeNormalize(projected.xyz / projected.w - CameraPosition.xyz);
    float3 world = CameraPosition.xyz + direction * surface.w;
    float3 n = SafeNormalize(surface.xyz);
    World[pixel] = float4(world, 1);
    Normal[pixel] = float4(n, 1);
    float3 history = 0;
    float count = 0, support = 0, motionPixels = 0;
    float4 clip = mul(PreviousViewProjection, float4(world, 1));
    if (Options.x > 0 && clip.w > 1e-7 && clip.z > 0 && clip.z <= clip.w)
    {
        float2 uv = float2(clip.x / clip.w * .5 + .5, .5 - clip.y / clip.w * .5);
        float2 previousPixel = uv * size - .5;
        motionPixels = length(previousPixel - float2(pixel));
        int2 base = int2(floor(previousPixel));
        float2 f = frac(previousPixel);
        // Allow one pixel of primary-ray jitter while rejecting changes in the surface plane.
        float footprint = max(.001, surface.w * 2.0 / min(width, height));
        for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
        {
            int2 q = base + int2(x, y);
            if (any(q < 0) || any(q >= int2(size))) continue;
            float4 old = Previous.Load(int3(q, 0));
            float4 oldWorld = PreviousWorld.Load(int3(q, 0));
            float4 oldNormal = PreviousNormal.Load(int3(q, 0));
            float4 oldMaterial = PreviousMaterial.Load(int3(q, 0));
            float3 delta = oldWorld.xyz - world;
            if (old.a < 1 || oldWorld.w <= 0 || oldNormal.w <= 0 ||
                dot(n, oldNormal.xyz) < .97 || length(delta) > footprint * 2 ||
                abs(dot(delta, n)) > footprint * .5 ||
                length(material.rgb - oldMaterial.rgb) > .08 ||
                abs(material.a - oldMaterial.a) > .04) continue;
            float w = (x == 0 ? 1 - f.x : f.x) * (y == 0 ? 1 - f.y : f.y);
            history += old.rgb * w;
            count += old.a * w;
            support += w;
        }
    }
    if (support > .1)
    {
        history /= support;
        count /= support;
        // Glossy reflections move differently from their receiver surface.
        float limit = lerp(2, 16, saturate((material.a - .1) / .5));
        count = min(count * saturate(support * 2), limit);
    }
    else
    {
        count = 0;
    }
    if (count > 0)
    {
        float3 mean = 0, square = 0;
        float neighbors = 0;
        for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
        {
            int2 q = int2(pixel) + int2(x, y);
            if (any(q < 0) || any(q >= int2(size))) continue;
            float4 otherSurface = Surface.Load(int3(q, 0));
            float4 otherMaterial = Material.Load(int3(q, 0));
            if (otherSurface.w <= 0 || dot(n, otherSurface.xyz) < .95 ||
                abs(otherSurface.w - surface.w) > max(.02, surface.w * .025) ||
                length(material.rgb - otherMaterial.rgb) > .08 ||
                abs(material.a - otherMaterial.a) > .04) continue;
            float3 sample = Current.Load(int3(q, 0)).rgb;
            mean += sample;
            square += sample * sample;
            neighbors += 1;
        }
        mean /= max(1, neighbors);
        float3 deviation = sqrt(max(0, square / max(1, neighbors) - mean * mean));
        history = clamp(history, max(0, mean - deviation - .02),
                        mean + deviation + .02);
    }
    float3 value = lerp(current, history, count / (count + 1));
    Output[pixel] = float4(value, min(count + 1, 16));
    // As stationary progressive samples accumulate, retire the correlated temporal preview.
    float preview = saturate(1 - max(0, Options.y - 8) / 24);
    Display[pixel] = float4(lerp(filtered.rgb, value, preview * .5 * saturate(count / 2) / (1 + .5 * motionPixels)), filtered.a);
}
