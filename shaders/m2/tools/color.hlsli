#ifndef RENDERLAB_M2_COLOR
#define RENDERLAB_M2_COLOR
float3 ApplyDetail(float3 value)
{
#if DETAIL
    return value * 0.5;
#else
    return value;
#endif
}
#endif
