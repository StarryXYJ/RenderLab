Texture2D<float4> Radiance : register(t0);
Texture2D<float4> SurfaceGuide : register(t1);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> Output : register(u2);
cbuffer PathParameters : register(b3)
{
    row_major float4x4 InverseViewProjection;
    float4 CameraPosition, DirectionToLight, LightColor, Environment;
    float4 Options;
};
Texture2D<float4> MaterialGuide : register(t4);
StructuredBuffer<float2> Moments : register(t5);
Texture2D<float4> DirectRadiance : register(t6);
Texture2D<float4> DiffuseRadiance : register(t7);
#ifdef PATH_COMPONENT_OUTPUTS
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> FilteredDiffuse : register(u8);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> FilteredSpecular : register(u9);
#endif

float Luminance(float3 color) { return dot(color, float3(.2126, .7152, .0722)); }

[numthreads(8,8,1)]
void Denoise(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    Output.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;
    int2 pixel = int2(id.xy), size = int2(width, height);
    float4 combined = Radiance.Load(int3(pixel, 0));
    float3 direct = DirectRadiance.Load(int3(pixel, 0)).rgb;
    float3 centerDiffuse = max(0, DiffuseRadiance.Load(int3(pixel, 0)).rgb);
    float3 centerSpecular = max(0, combined.rgb - direct - centerDiffuse);
    float4 guide = SurfaceGuide.Load(int3(pixel, 0));
    float4 material = MaterialGuide.Load(int3(pixel, 0));
    float samples = min(Options.y + Environment.z, Options.w);
    float2 moments = Moments[id.x + id.y * width];
    float stderr = samples > 1 ? sqrt(max(0, moments.y) / (samples - 1) / samples) : 1e10;
    // Fade by measured uncertainty, not a fixed sample-count cutoff.
    float strength = saturate(stderr / max(.03 * abs(moments.x), .002)) * min(1, 512.0 / samples);
    if (guide.w <= 0 || strength <= 0)
    {
        Output[pixel] = combined;
#ifdef PATH_COMPONENT_OUTPUTS
        FilteredDiffuse[pixel] = float4(centerDiffuse, 1);
        FilteredSpecular[pixel] = float4(combined.rgb - direct - centerDiffuse, 1);
#endif
        return;
    }
    float3 diffuseColors[9], specularColors[9];
    float diffuseValues[9], specularValues[9];
    float weights[9];
    uint count = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
    [unroll] for (int x = -1; x <= 1; ++x)
    {
        int2 q = pixel + int2(x,y);
        if (any(q < 0) || any(q >= size)) continue;
        float4 g = SurfaceGuide.Load(int3(q, 0));
        float4 m = MaterialGuide.Load(int3(q, 0));
        if (g.w <= 0 || dot(guide.xyz, g.xyz) < .95 ||
            abs(guide.w - g.w) > max(.02, guide.w * .025) ||
            length(material.rgb - m.rgb) > .12 || abs(material.a - m.a) > .1) continue;
        float3 diffuse = max(0, DiffuseRadiance.Load(int3(q, 0)).rgb);
        float3 specular = max(0, Radiance.Load(int3(q, 0)).rgb -
                                  DirectRadiance.Load(int3(q, 0)).rgb - diffuse);
        diffuseColors[count] = diffuse;
        specularColors[count] = specular;
        diffuseValues[count] = Luminance(diffuse);
        specularValues[count] = Luminance(specular);
        weights[count] = x == 0 && y == 0 ? 2 : 1;
        ++count;
    }
    if (count < 2)
    {
        Output[pixel] = combined;
#ifdef PATH_COMPONENT_OUTPUTS
        FilteredDiffuse[pixel] = float4(centerDiffuse, 1);
        FilteredSpecular[pixel] = float4(combined.rgb - direct - centerDiffuse, 1);
#endif
        return;
    }
    float sortedDiffuse[9], sortedSpecular[9];
    [unroll] for (uint i = 0; i < 9; ++i)
    {
        sortedDiffuse[i] = i < count ? diffuseValues[i] : 1e30;
        sortedSpecular[i] = i < count ? specularValues[i] : 1e30;
    }
    [unroll] for (uint i = 0; i < 9; ++i)
    [unroll] for (uint j = 0; j < 8; ++j)
    {
        float a = sortedDiffuse[j], b = sortedDiffuse[j + 1];
        sortedDiffuse[j] = min(a, b); sortedDiffuse[j + 1] = max(a, b);
        a = sortedSpecular[j]; b = sortedSpecular[j + 1];
        sortedSpecular[j] = min(a, b); sortedSpecular[j + 1] = max(a, b);
    }
    // With two matching samples, use the lower value to keep an isolated outlier
    // from defining its own limit. Larger neighborhoods retain the prior median.
    uint median = count == 2 ? 0 : count / 2;
    float diffuseLimit = count == 2 ? sortedDiffuse[median] * 4 + .08
                                    : sortedDiffuse[median] * 2.5 + .04;
    float specularLimit = sortedSpecular[median] * 2.5 + .04;
    float3 diffuseSum = 0, specularSum = 0;
    float total = 0;
    [loop] for (uint i = 0; i < count; ++i)
    {
        diffuseSum += diffuseColors[i] * min(1, diffuseLimit / max(diffuseValues[i], 1e-6)) * weights[i];
        specularSum += specularColors[i] * min(1, specularLimit / max(specularValues[i], 1e-6)) * weights[i];
        total += weights[i];
    }
    // Do not blur a normal value when the only neighbor is the brighter sample.
    float3 diffuseResult = count == 2 && Luminance(centerDiffuse) <= diffuseLimit
                               ? centerDiffuse
                               : lerp(centerDiffuse, diffuseSum / total, strength);
    // Two points cannot distinguish a narrow reflection from an outlier reliably.
    float3 specularResult = count < 3 ? centerSpecular
                                      : lerp(centerSpecular, specularSum / total, strength);
    Output[pixel] = float4(direct + diffuseResult + specularResult, combined.a);
#ifdef PATH_COMPONENT_OUTPUTS
    FilteredDiffuse[pixel] = float4(diffuseResult, 1);
    FilteredSpecular[pixel] = float4(specularResult, 1);
#endif
}
