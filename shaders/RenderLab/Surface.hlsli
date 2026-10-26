#ifndef RENDERLAB_SURFACE_INCLUDED
#define RENDERLAB_SURFACE_INCLUDED
// Linear values, independent of raster derivatives, resources and lighting.
struct MaterialSurface
{
    float3 baseColor;
    float alpha;
    float3 normalWS;
    float metallic;
    float perceptualRoughness;
    float occlusion;
    float3 emission;
};
#endif
