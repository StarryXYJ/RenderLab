#ifndef RENDERLAB_MATH_INCLUDED
#define RENDERLAB_MATH_INCLUDED
float3 SafeNormalize(float3 v) { return v * rsqrt(max(dot(v, v), 1e-12)); }
#endif
