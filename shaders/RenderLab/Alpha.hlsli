#ifndef RENDERLAB_ALPHA_INCLUDED
#define RENDERLAB_ALPHA_INCLUDED
void ApplyAlphaCutoff(float alpha)
{
#if ALPHA_MASK
    clip(alpha - Material.AlphaCutoff);
#endif
}
float SurfaceAlpha(float alpha)
{
#if ALPHA_BLEND
    return saturate(alpha);
#else
    return 1;
#endif
}
#endif
