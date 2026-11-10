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
    float4 center = float4(max(0, combined.rgb - direct), combined.a);
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
        return;
    }
    float3 colors[9];
    float values[9];
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
        float3 color = max(0, Radiance.Load(int3(q, 0)).rgb -
                              DirectRadiance.Load(int3(q, 0)).rgb);
        colors[count] = color;
        values[count] = Luminance(color);
        weights[count] = x == 0 && y == 0 ? 2 : 1;
        ++count;
    }
    if (count < 3)
    {
        Output[pixel] = combined;
        return;
    }
    float sorted[9];
    [unroll] for (uint i = 0; i < 9; ++i) sorted[i] = i < count ? values[i] : 1e30;
    [unroll] for (uint i = 0; i < 9; ++i)
    [unroll] for (uint j = 0; j < 8; ++j)
    {
        float a = sorted[j], b = sorted[j + 1];
        sorted[j] = min(a, b);
        sorted[j + 1] = max(a, b);
    }
    float limit = sorted[count / 2] * 2.5 + .04;
    float3 sum = 0;
    float total = 0;
    [loop] for (uint i = 0; i < count; ++i)
    {
        sum += colors[i] * min(1, limit / max(values[i], 1e-6)) * weights[i];
        total += weights[i];
    }
    Output[pixel] = float4(direct + lerp(center.rgb, sum / total, strength), center.a);
}
