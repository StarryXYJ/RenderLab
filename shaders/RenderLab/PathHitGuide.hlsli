#ifndef RENDERLAB_PATH_HIT_GUIDE_INCLUDED
#define RENDERLAB_PATH_HIT_GUIDE_INCLUDED

// Distances describe the first ray after the primary surface; 0 denotes no sample,
// while the finite ray limit represents an environment miss. No PDF/BRDF weighting.
struct PathHitGuideAccumulator
{
    float diffuseReciprocal, specularReciprocal;
    uint diffuseCount, specularCount;
};
void AddPathHitGuide(inout PathHitGuideAccumulator guide, float distance, bool specular)
{
    if (!isfinite(distance) || distance <= 0) return;
    float reciprocal = rcp(max(distance, 1e-6));
    if (specular)
    {
        guide.specularReciprocal += reciprocal;
        ++guide.specularCount;
    }
    else
    {
        guide.diffuseReciprocal += reciprocal;
        ++guide.diffuseCount;
    }
}
float4 ResolvePathHitGuide(PathHitGuideAccumulator guide)
{
    return float4(guide.diffuseCount > 0 ? guide.diffuseCount / guide.diffuseReciprocal : 0,
                  guide.specularCount > 0 ? guide.specularCount / guide.specularReciprocal : 0,
                  guide.diffuseCount, guide.specularCount);
}
#endif
