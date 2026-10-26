#ifndef RENDERLAB_ALPHA_INCLUDED
#define RENDERLAB_ALPHA_INCLUDED
void ApplyAlphaCutoff(float alpha, float cutoff)
{
    clip(alpha - cutoff);
}
// Surface/Blend use the stable numeric codes of the material policy interface.
float4 EncodeSurfaceOutput(float3 color, float alpha, uint surface, uint blend)
{
    if (surface == 0) return float4(color, 1);
    alpha = saturate(alpha);
    if (blend == 1) color *= alpha;
    else if (blend == 3) color = lerp(1.0.xxx, color, alpha);
    return float4(color, alpha);
}
#endif
