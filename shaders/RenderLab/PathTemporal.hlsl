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
Texture2D<float4> SecondaryGuide : register(t14);
Texture2D<float4> Diffuse : register(t15);
Texture2D<float4> CurrentSpecular : register(t16);
Texture2D<float4> PreviousSpecular : register(t17);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> Specular : register(u18);

[numthreads(8,8,1)]
void Temporal(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    Output.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;
    uint2 pixel = id.xy, size = uint2(width, height);
    float3 current = Diffuse.Load(int3(pixel, 0)).rgb;
    float3 currentSpecular = CurrentSpecular.Load(int3(pixel, 0)).rgb;
    float4 surface = Surface.Load(int3(pixel, 0));
    float4 material = Material.Load(int3(pixel, 0));
    float4 filtered = Filtered.Load(int3(pixel, 0));
    Output[pixel] = float4(current, 0);
    Specular[pixel] = float4(currentSpecular, 0);
    World[pixel] = 0;
    Normal[pixel] = 0;
    MaterialHistory[pixel] = material;
    Display[pixel] = filtered;
    if (surface.w <= 0) return;
    uint samplePixel = PathSamplePixel(pixel.x + pixel.y * width, uint(Options.w));
    float2 jitter = PathSample(uint(Options.z), samplePixel, 0);
    float2 ndc = float2((pixel.x + jitter.x) * 2 / width - 1,
                        1 - (pixel.y + jitter.y) * 2 / height);
    float4 projected = mul(InverseViewProjection, float4(ndc, .0001, 1));
    float3 direction = SafeNormalize(projected.xyz / projected.w - CameraPosition.xyz);
    float3 world = CameraPosition.xyz + direction * surface.w;
    float3 n = SafeNormalize(surface.xyz);
    float4 secondary = SecondaryGuide.Load(int3(pixel, 0));
    float distance = secondary.w > 0 ? secondary.y : 0;
    World[pixel] = float4(world, distance > 0 ? distance : -1);
    Normal[pixel] = float4(n, 1);
    float3 history = 0, specularHistory = 0;
    float count = 0, specularCount = 0, support = 0, motionPixels = 0;
    float oldDistanceSum = 0, oldDistanceWeight = 0, parallaxConfidence = 1;
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
            float4 oldSpecular = PreviousSpecular.Load(int3(q, 0));
            float4 oldWorld = PreviousWorld.Load(int3(q, 0));
            float4 oldNormal = PreviousNormal.Load(int3(q, 0));
            float4 oldMaterial = PreviousMaterial.Load(int3(q, 0));
            float3 delta = oldWorld.xyz - world;
            if (old.a < 1 || oldNormal.w <= 0 ||
                dot(n, oldNormal.xyz) < .97 || length(delta) > footprint * 2 ||
                abs(dot(delta, n)) > footprint * .5 ||
                length(material.rgb - oldMaterial.rgb) > .08 ||
                abs(material.a - oldMaterial.a) > .04) continue;
            float w = (x == 0 ? 1 - f.x : f.x) * (y == 0 ? 1 - f.y : f.y);
            history += old.rgb * w;
            specularHistory += oldSpecular.rgb * w;
            count += old.a * w;
            specularCount += oldSpecular.a * w;
            support += w;
            if (oldWorld.w > 0 && oldWorld.w < 9999)
            {
                oldDistanceSum += oldWorld.w * w;
                oldDistanceWeight += w;
            }
        }
    }
    if (support > .1)
    {
        history /= support;
        specularHistory /= support;
        count /= support;
        specularCount /= support;
        // Glossy reflections move differently from their receiver surface.
        float limit = lerp(2, 16, saturate((material.a - .1) / .5));
        count = min(count * saturate(support * 2), 16);
        specularCount = min(specularCount * saturate(support * 2), limit);
        // A receiver and its reflected feature may move differently. This conservative
        // estimate reduces history use; it does not reproject the combined signal as specular.
        if (distance > 0 && distance < 9999 && oldDistanceWeight > .1 && material.a < .4)
        {
            distance = lerp(distance, oldDistanceSum / oldDistanceWeight, .75);
            World[pixel] = float4(world, distance);
            float3 view = SafeNormalize(CameraPosition.xyz - world);
            float4 virtualClip = mul(PreviousViewProjection, float4(world - view * distance, 1));
            if (virtualClip.w > 1e-7 && virtualClip.z > 0 && virtualClip.z <= virtualClip.w)
            {
                float2 virtualUv = float2(virtualClip.x / virtualClip.w * .5 + .5,
                                          .5 - virtualClip.y / virtualClip.w * .5);
                float2 receiverUv = float2(clip.x / clip.w * .5 + .5,
                                           .5 - clip.y / clip.w * .5);
                float disparity = length((virtualUv - receiverUv) * size);
                float glossy = saturate((.4 - material.a) / .35) * saturate(secondary.w / 2);
                parallaxConfidence = lerp(1, rcp(1 + .25 * disparity), glossy);
            }
        }
    }
    else
    {
        count = 0;
    }
    if (count > 0 || specularCount > 0)
    {
        float3 mean = 0, square = 0, specularMean = 0, specularSquare = 0;
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
            float3 sample = Diffuse.Load(int3(q, 0)).rgb;
            float3 specularSample = CurrentSpecular.Load(int3(q, 0)).rgb;
            mean += sample;
            square += sample * sample;
            specularMean += specularSample;
            specularSquare += specularSample * specularSample;
            neighbors += 1;
        }
        mean /= max(1, neighbors);
        float3 deviation = sqrt(max(0, square / max(1, neighbors) - mean * mean));
        history = clamp(history, max(0, mean - deviation - .02),
                        mean + deviation + .02);
        specularMean /= max(1, neighbors);
        float3 specularDeviation = sqrt(max(0, specularSquare / max(1, neighbors) -
                                               specularMean * specularMean));
        specularHistory = clamp(specularHistory, max(0, specularMean - specularDeviation - .02),
                                specularMean + specularDeviation + .02);
    }
    // Store the confidence for diagnostics; valid surface normals keep it strictly positive.
    Normal[pixel] = float4(n, parallaxConfidence);
    specularCount *= parallaxConfidence;
    float3 value = lerp(current, history, count / (count + 1));
    float3 specularValue = lerp(currentSpecular, specularHistory, specularCount / (specularCount + 1));
    Output[pixel] = float4(value, min(count + 1, 16));
    Specular[pixel] = float4(specularValue, min(specularCount + 1, 16));
    float preview = saturate(1 - max(0, Options.y - 8) / 24);
    float weight = preview * .75 / (1 + .2 * motionPixels);
    float3 correction = (value - current) * saturate(count / 2) +
                        (specularValue - currentSpecular) * saturate(specularCount / 2);
    Display[pixel] = float4(max(0, filtered.rgb + weight * correction), filtered.a);
}
