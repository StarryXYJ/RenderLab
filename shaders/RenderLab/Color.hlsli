#ifndef RENDERLAB_COLOR_INCLUDED
#define RENDERLAB_COLOR_INCLUDED
float3 ToneMapACES(float3 radiance, float exposure)
{
    float3 x = min(max(radiance, 0) * exp2(exposure), 1e10);
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}
#endif
