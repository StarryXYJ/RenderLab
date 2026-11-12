#ifndef RENDERLAB_AREA_VISIBILITY_FILTER_INCLUDED
#define RENDERLAB_AREA_VISIBILITY_FILTER_INCLUDED
// Filter area-light visibility without crossing depth or normal discontinuities.
float2 FilterAreaVisibility(Texture2D<float4> visibility, Texture2D<float4> depths,
                            Texture2D<float4> normals, int2 pixel, int2 dimensions,
                            float depth, float3 normal, float depthLimit)
{
    float2 sum = 0;
    float total = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
    [unroll] for (int x = -1; x <= 1; ++x)
    {
        int2 neighbor = pixel + int2(x, y);
        if (any(neighbor < 0) || any(neighbor >= dimensions)) continue;
        float otherDepth = depths.Load(int3(neighbor, 0)).x;
        float4 otherNormal = normals.Load(int3(neighbor, 0));
        if (otherDepth <= 1e-7 || otherNormal.w < 0 ||
            abs(otherDepth - depth) > depthLimit || dot(otherNormal.xyz, normal) < .95) continue;
        float weight = (x == 0 ? 2 : 1) * (y == 0 ? 2 : 1);
        sum += visibility.Load(int3(neighbor, 0)).yz * weight;
        total += weight;
    }
    return total > 0 ? sum / total : visibility.Load(int3(pixel, 0)).yz;
}
#endif
