#ifndef RENDERLAB_RAY_SAMPLING_INCLUDED
#define RENDERLAB_RAY_SAMPLING_INCLUDED
#include "BRDF.hlsli"

uint RaySampleHash(uint value)
{
    value ^= value >> 16; value *= 0x7feb352d;
    value ^= value >> 15; value *= 0x846ca68b;
    return value ^ (value >> 16);
}
float2 ReflectionSample(uint index, uint pixel)
{
    // Digitally shifted Sobol (0,2) sequence. Every power-of-two prefix is stratified.
    uint second = 0, direction = 0x80000000;
    for (uint bits = index; bits != 0; bits >>= 1)
    {
        if ((bits & 1) != 0) second ^= direction;
        direction ^= direction >> 1;
    }
    uint first = reversebits(index) ^ RaySampleHash(pixel ^ 0xa511e9b3);
    second ^= RaySampleHash(pixel ^ 0x63d83595);
    return float2(first >> 8, second >> 8) * (1.0 / 16777216.0);
}
// Cosine-weighted local hemisphere; Lambertian BRDF * cosine / PDF is albedo.
float3 SampleCosineHemisphere(float2 xi)
{
    float radius = sqrt(xi.x);
    float phi = 2 * RenderLabPi * xi.y;
    return float3(radius * cos(phi), radius * sin(phi), sqrt(max(0, 1 - xi.x)));
}
// A zero seed preserves the original stream; nonzero seeds select a deterministic pixel stream.
uint PathSamplePixel(uint pixel, uint seed)
{
    return seed == 0 ? pixel : pixel ^ RaySampleHash(seed);
}

// Separate dimensions for camera and path bounces; all arithmetic is deterministic uint32.
float2 PathSample(uint sampleIndex, uint pixel, uint dimension)
{
    uint seed = RaySampleHash(pixel ^ 0xa511e9b3u);
    seed = RaySampleHash(seed ^ sampleIndex ^ 0x63d83595u);
    seed = RaySampleHash(seed ^ dimension ^ 0x9e3779b9u);
    uint x = RaySampleHash(seed ^ 0x68bc21ebu);
    uint y = RaySampleHash(seed ^ 0x02e5be93u);
    return float2(x >> 8, y >> 8) * (1.0 / 16777216.0);
}
float3 SampleVisibleGGX(float3 view, float alpha, float2 xi)
{
    // Heitz 2018: sample the GGX distribution of normals visible from the view direction.
    float3 stretched = SafeNormalize(float3(alpha * view.xy, view.z));
    float lensq = dot(stretched.xy, stretched.xy);
    float3 tangent = lensq > 0 ? float3(-stretched.y, stretched.x, 0) * rsqrt(lensq) : float3(1,0,0);
    float3 bitangent = cross(stretched, tangent);
    float radius = sqrt(xi.x), phi = 2 * RenderLabPi * xi.y;
    float x = radius * cos(phi), y = radius * sin(phi);
    float blend = .5 * (1 + stretched.z);
    y = (1 - blend) * sqrt(max(0, 1 - x * x)) + blend * y;
    float3 normal = x * tangent + y * bitangent +
        sqrt(max(0, 1 - x * x - y * y)) * stretched;
    return SafeNormalize(float3(alpha * normal.xy, max(0, normal.z)));
}
float VisibleGGXWeight(float nv, float nl, float roughness)
{
    // BRDF*cos/pdf = Fresnel * G2(V,L)/G1(V). The non-Fresnel weight is bounded by one.
    float alpha = roughness * roughness;
    float root = sqrt(alpha * alpha + (1 - alpha * alpha) * nv * nv);
    return 2 * VisibilitySmithGGX(nv, nl, roughness) * nl * (nv + root);
}
#endif
