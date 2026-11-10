#ifndef RENDERLAB_RECT_AREA_SAMPLING_INCLUDED
#define RENDERLAB_RECT_AREA_SAMPLING_INCLUDED

// center + (u-.5)*edgeU + (v-.5)*edgeV; front follows the scene snapshot normal.
struct RectEmitter
{
    float3 center, edgeU, edgeV, normal;
    float area;
};
struct RectEmitterSample
{
    float3 position, direction;
    float distance, pdfOmega;
};

// PDF is with respect to solid angle and includes the probability of choosing this light.
// samplePosition must lie on the emitter. A zero PDF means the emitter is back-facing,
// degenerate, coincident or non-finite.
float RectEmitterPdfOmega(RectEmitter light, float3 shadingPoint, float3 samplePosition, float selectionPdf)
{
    float3 delta = samplePosition - shadingPoint;
    float distance2 = dot(delta, delta);
    if (!(distance2 > 1e-12) || !(light.area > 0) || !(selectionPdf > 0) ||
        !isfinite(distance2) || !isfinite(light.area) || !isfinite(selectionPdf))
        return 0;
    float3 direction = delta * rsqrt(distance2);
    float cosine = dot(light.normal, -direction);
    if (!(cosine > 0) || !isfinite(cosine)) return 0;
    float pdf = (selectionPdf / light.area) * (distance2 / cosine);
    return isfinite(pdf) && pdf > 0 ? pdf : 0;
}
RectEmitterSample SampleRectEmitter(RectEmitter light, float3 shadingPoint, float2 xi, float selectionPdf)
{
    RectEmitterSample result = (RectEmitterSample)0;
    result.position = light.center + (xi.x - .5) * light.edgeU + (xi.y - .5) * light.edgeV;
    float3 delta = result.position - shadingPoint;
    float distance2 = dot(delta, delta);
    if (!(distance2 > 1e-12) || !isfinite(distance2)) return result;
    result.distance = sqrt(distance2);
    result.direction = delta / result.distance;
    result.pdfOmega = RectEmitterPdfOmega(light, shadingPoint, result.position, selectionPdf);
    return result;
}
// Intersect the finite parallelogram from either side. The caller decides whether its
// front face emits; both faces can block another light. direction must be normalized.
bool IntersectRectEmitter(RectEmitter light, float3 origin, float3 direction,
                          float tMin, float tMax, out float distance)
{
    distance = 0;
    float denominator = dot(light.normal, direction);
    if (!isfinite(denominator) || denominator == 0) return false;
    float t = dot(light.normal, light.center - origin) / denominator;
    if (!isfinite(t) || t < tMin || t > tMax) return false;
    float3 delta = origin + t * direction - light.center;
    float uu = dot(light.edgeU, light.edgeU), uv = dot(light.edgeU, light.edgeV);
    float vv = dot(light.edgeV, light.edgeV), determinant = uu * vv - uv * uv;
    if (!(determinant > 0) || !isfinite(determinant)) return false;
    float du = dot(delta, light.edgeU), dv = dot(delta, light.edgeV);
    float u = (du * vv - dv * uv) / determinant;
    float v = (dv * uu - du * uv) / determinant;
    if (!isfinite(u) || !isfinite(v) || abs(u) > .5 || abs(v) > .5) return false;
    distance = t;
    return true;
}
// Power heuristic with one sample from each technique; PDFs must use the same measure.
float RectPowerWeight(float selectedPdf, float otherPdf)
{
    if (!(selectedPdf > 0) || !isfinite(selectedPdf)) return 0;
    if (!isfinite(otherPdf) || otherPdf < 0) return 0;
    if (otherPdf == 0) return 1;
    float scale = max(selectedPdf, otherPdf);
    float a = selectedPdf / scale, b = otherPdf / scale;
    return a * a / (a * a + b * b);
}
#endif
