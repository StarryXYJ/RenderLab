#include "RaySurface.hlsli"
#include "PathBsdf.hlsli"
#include "PathHitGuide.hlsli"
#include "RectAreaLight.hlsli"
#include "ImageBasedLighting.hlsli"

RaytracingAccelerationStructure Scene : register(t0);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> Radiance : register(u1);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> SurfaceGuide : register(u2);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> MaterialGuide : register(u3);
RWStructuredBuffer<float2> BatchMoments : register(u17);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> DirectRadiance : register(u18);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> DiffuseRadiance : register(u19);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> SecondaryGuide : register(u20);
cbuffer PathParameters : register(b4)
{
    row_major float4x4 InverseViewProjection;
    float4 CameraPosition, DirectionToLight, LightColor, Environment;
    float4 Options;
    float4 RectLightHeader;
    float4 RectLightData[6];
    float4 PointLightHeader;
    float4 PointLightData[16];
};
Texture2D<float4> EnvironmentAtlas : register(t13);

RectEmitter PathRectLight(uint index, out float3 emitted)
{
    float4 center = RectLightData[index * 3];
    float4 edgeU = RectLightData[index * 3 + 1];
    float4 edgeV = RectLightData[index * 3 + 2];
    RectEmitter light;
    light.center = center.xyz;
    light.edgeU = edgeU.xyz;
    light.edgeV = edgeV.xyz;
    float3 areaNormal = cross(light.edgeU, light.edgeV);
    light.area = length(areaNormal);
    light.normal = light.area > 0 ? -areaNormal / light.area : 0;
    emitted = float3(center.w, edgeU.w, edgeV.w);
    return light;
}

struct PathPayload
{
    float3 position;
    uint hit;
    float3 normal;
    uint unlit;
    float3 albedo;
    float metallic;
    float3 emission;
    float roughness;
};

float3 TracePath(uint2 pixel, uint2 size, uint sampleIndex, out float4 surfaceGuide,
                 out float4 materialGuide, out float3 primary, out float3 indirectDiffuse,
                 out float secondaryDistance, out uint secondarySpecular)
{
    uint pixelIndex = PathSamplePixel(pixel.x + pixel.y * size.x, uint(Environment.w));
    float2 jitter = PathSample(sampleIndex, pixelIndex, 0);
    float2 ndc = float2((pixel.x + jitter.x) * 2 / size.x - 1,
                        1 - (pixel.y + jitter.y) * 2 / size.y);
    float4 world = mul(InverseViewProjection, float4(ndc, .0001, 1));
    RayDesc ray;
    ray.Origin = CameraPosition.xyz;
    ray.Direction = SafeNormalize(world.xyz / world.w - ray.Origin);
    ray.TMin = .001;
    ray.TMax = 10000;
    float3 throughput = 1;
    float3 result = 0;
    primary = 0;
    indirectDiffuse = 0;
    secondaryDistance = 0;
    secondarySpecular = 0;
    float3 diffuseFraction = 0;
    float previousBsdfPdf = 0;
    float3 previousSurface = 0;
    const uint rectLightCount = min((uint)RectLightHeader.x, 2u);
    const bool rectSampling = RectLightHeader.y > 0;
    surfaceGuide = 0;
    materialGuide = 0;
    [loop] for (uint bounce = 0; bounce < 4; ++bounce)
    {
        PathPayload payload = (PathPayload)0;
        TraceRay(Scene, RAY_FLAG_NONE, 0x02, 0, 1, 0, ray, payload);
        // Analytic emitters have stable identities distinct from emissive scene meshes.
        float nearestDistance = payload.hit != 0 ? length(payload.position - ray.Origin) : ray.TMax;
        int nearestLight = -1;
        [loop] for (uint emitter = 0; emitter < rectLightCount; ++emitter)
        {
            float3 ignored;
            RectEmitter light = PathRectLight(emitter, ignored);
            float distance;
            if (IntersectRectEmitter(light, ray.Origin, ray.Direction, ray.TMin, nearestDistance, distance))
            {
                nearestDistance = distance;
                nearestLight = (int)emitter;
            }
        }
        if (bounce == 1)
            secondaryDistance = nearestLight >= 0 || payload.hit != 0 ? nearestDistance : ray.TMax;
        if (nearestLight >= 0)
        {
            float3 emitted;
            RectEmitter light = PathRectLight((uint)nearestLight, emitted);
            if (dot(light.normal, -ray.Direction) > 0)
            {
                float weight = 1;
                if (bounce > 0 && rectSampling)
                {
                    float3 hitPosition = ray.Origin + ray.Direction * nearestDistance;
                    float lightPdf = RectEmitterPdfOmega(light, previousSurface, hitPosition,
                                                         1.0 / rectLightCount);
                    weight = RectPowerWeight(previousBsdfPdf, lightPdf);
                }
                float3 value = throughput * emitted * weight;
                result += value;
                if (bounce == 0) primary += value;
                else indirectDiffuse += value * diffuseFraction;
            }
            break;
        }
        if (bounce == 0 && payload.hit != 0)
        {
            // Negative distance bypasses filtering for Unlit surfaces.
            surfaceGuide = float4(payload.normal,
                length(payload.position - CameraPosition.xyz) * (payload.unlit != 0 ? -1 : 1));
            materialGuide = float4(payload.albedo, payload.roughness);
        }
        if (payload.hit == 0)
        {
            float3 environment = throughput * EnvironmentTile(ray.Direction, 0, EnvironmentAtlas, Environment);
            result += environment;
            if (bounce == 0) primary += environment;
            else indirectDiffuse += environment * diffuseFraction;
            break;
        }
        if (payload.unlit != 0)
        {
            float3 unlit = throughput * payload.albedo;
            result += unlit;
            if (bounce == 0) primary += unlit;
            else indirectDiffuse += unlit * diffuseFraction;
            break;
        }
        float3 emission = throughput * payload.emission;
        result += emission;
        if (bounce == 0) primary += emission;
        else indirectDiffuse += emission * diffuseFraction;
        float3 lightDirection = SafeNormalize(DirectionToLight.xyz);
        float nl = saturate(dot(payload.normal, lightDirection));
        if (nl > 0 && LightColor.w > 0)
        {
            bool visible = true;
            if (Options.z > 0)
            {
                RayDesc shadow;
                shadow.Origin = payload.position + payload.normal * .002;
                shadow.Direction = lightDirection;
                shadow.TMin = .001;
                shadow.TMax = 10000;
                RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
                query.TraceRayInline(Scene, RAY_FLAG_NONE, 0x01, shadow);
                while (query.Proceed()) {}
                visible = query.CommittedStatus() != COMMITTED_TRIANGLE_HIT;
                [loop] for (uint emitter = 0; emitter < rectLightCount && visible; ++emitter)
                {
                    float3 ignored;
                    RectEmitter blocker = PathRectLight(emitter, ignored);
                    float distance;
                    if (IntersectRectEmitter(blocker, shadow.Origin, shadow.Direction,
                                             shadow.TMin, shadow.TMax, distance)) visible = false;
                }
            }
            if (visible)
            {
                float3 direct = throughput * EvaluateBRDF(payload.albedo, payload.metallic,
                    payload.roughness, payload.normal, -ray.Direction, lightDirection) *
                    LightColor.rgb * LightColor.w;
                result += direct;
                if (bounce == 0) primary += direct;
                else indirectDiffuse += direct * diffuseFraction;
            }
        }
        // Delta point emitters are connected explicitly; a BSDF sample cannot hit a point.
        if (bounce < 3)
        {
            [loop] for (uint pointIndex = 0; pointIndex < min((uint)PointLightHeader.x, 8u); ++pointIndex)
            {
                float4 pr = PointLightData[2 * pointIndex];
                float4 ci = PointLightData[2 * pointIndex + 1];
                float3 delta = pr.xyz - payload.position;
                float distance2 = dot(delta, delta);
                float q = distance2 / (pr.w * pr.w);
                float falloff = saturate(1 - q * q);
                float3 direction = delta * rsqrt(max(distance2, 1e-8));
                if (falloff <= 0 || dot(payload.normal, direction) <= 0) continue;
                float3 origin = payload.position + payload.normal * .002;
                float3 shadowDelta = pr.xyz - origin;
                float shadowDistance = length(shadowDelta);
                bool visible = shadowDistance > .002;
                if (visible)
                {
                    RayDesc shadow;
                    shadow.Origin = origin;
                    shadow.Direction = shadowDelta / shadowDistance;
                    shadow.TMin = .001;
                    shadow.TMax = shadowDistance - .001;
                    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
                    query.TraceRayInline(Scene, RAY_FLAG_NONE, 0x01, shadow);
                    while (query.Proceed()) {}
                    visible = query.CommittedStatus() != COMMITTED_TRIANGLE_HIT;
                    [loop] for (uint emitter = 0; emitter < rectLightCount && visible; ++emitter)
                    {
                        float3 ignored;
                        RectEmitter blocker = PathRectLight(emitter, ignored);
                        float blockerDistance;
                        if (IntersectRectEmitter(blocker, shadow.Origin, shadow.Direction,
                                                 shadow.TMin, shadow.TMax, blockerDistance))
                            visible = false;
                    }
                }
                if (visible)
                {
                    float3 direct = throughput * EvaluateBRDF(payload.albedo, payload.metallic,
                        payload.roughness, payload.normal, -ray.Direction, direction) * ci.rgb * ci.a *
                        (falloff * falloff / max(distance2, 1e-4));
                    result += direct;
                    if (bounce == 0) primary += direct;
                    else indirectDiffuse += direct * diffuseFraction;
                }
            }
        }
        // A light connection consumes the next path segment, matching the four-segment limit.
        if (rectLightCount > 0 && rectSampling && bounce < 3)
        {
            float2 lightXi = PathSample(sampleIndex, pixelIndex, 16 + 2 * bounce);
            float choose = PathSample(sampleIndex, pixelIndex, 17 + 2 * bounce).x;
            uint lightIndex = min((uint)(choose * rectLightCount), rectLightCount - 1);
            float3 emitted;
            RectEmitter light = PathRectLight(lightIndex, emitted);
            RectEmitterSample sample = SampleRectEmitter(light, payload.position, lightXi,
                                                          1.0 / rectLightCount);
            if (sample.pdfOmega > 0 && dot(payload.normal, sample.direction) > 0)
            {
                float3 shadowOrigin = payload.position + payload.normal * .002;
                float3 shadowDelta = sample.position - shadowOrigin;
                float shadowDistance = length(shadowDelta);
                bool visible = shadowDistance > .002;
                if (visible)
                {
                    RayDesc shadow;
                    shadow.Origin = shadowOrigin;
                    shadow.Direction = shadowDelta / shadowDistance;
                    shadow.TMin = .001;
                    shadow.TMax = shadowDistance - .001;
                    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
                    query.TraceRayInline(Scene, RAY_FLAG_NONE, 0x01, shadow);
                    while (query.Proceed()) {}
                    visible = query.CommittedStatus() != COMMITTED_TRIANGLE_HIT;
                    [loop] for (uint other = 0; other < rectLightCount && visible; ++other)
                    {
                        if (other == lightIndex) continue;
                        float3 ignored;
                        RectEmitter blocker = PathRectLight(other, ignored);
                        float distance;
                        if (IntersectRectEmitter(blocker, shadow.Origin, shadow.Direction,
                                                 shadow.TMin, shadow.TMax, distance)) visible = false;
                    }
                }
                if (visible)
                {
                    float bsdfPdf = PathBsdfPdf(payload.normal, -ray.Direction,
                                                sample.direction, payload.roughness);
                    float weight = RectPowerWeight(sample.pdfOmega, bsdfPdf);
                    float3 direct = throughput * EvaluateBRDF(payload.albedo, payload.metallic,
                        payload.roughness, payload.normal, -ray.Direction, sample.direction) *
                        emitted * (weight / sample.pdfOmega);
                    result += direct;
                    if (bounce == 0) primary += direct;
                    else indirectDiffuse += direct * diffuseFraction;
                }
            }
        }
        float2 xi = PathSample(sampleIndex, pixelIndex, bounce + 1);
        float choice = PathSample(sampleIndex, pixelIndex, bounce + 5).x;
        float3 direction = SamplePathBsdf(payload.normal, -ray.Direction, payload.roughness,
                                          xi, choice);
        float pdf = PathBsdfPdf(payload.normal, -ray.Direction, direction, payload.roughness);
        if (pdf <= 0) break;
        if (bounce == 0)
        {
            secondarySpecular = choice >= .5 ? 1 : 0;
            float3 diffuse, specular;
            EvaluateBRDFComponents(payload.albedo, payload.metallic, payload.roughness,
                                   payload.normal, -ray.Direction, direction, diffuse, specular);
            diffuseFraction = PathDiffuseFraction(diffuse, specular);
        }
        throughput *= EvaluateBRDF(payload.albedo, payload.metallic, payload.roughness,
                                   payload.normal, -ray.Direction, direction) / pdf;
        previousBsdfPdf = pdf;
        previousSurface = payload.position;
        ray.Origin = payload.position + payload.normal * .002;
        ray.Direction = direction;
    }
    return result;
}

[shader("raygeneration")]
void RayGeneration()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 size = DispatchRaysDimensions().xy;
    uint pixelIndex = pixel.x + pixel.y * size.x;
    uint count = min(uint(Environment.z), uint(Options.w) - min(uint(Options.y), uint(Options.w)));
    float3 sum = 0, directSum = 0, diffuseSum = 0;
    float mean = 0, m2 = 0;
    float4 guide = 0, material = 0;
    PathHitGuideAccumulator hitGuide = (PathHitGuideAccumulator)0;
    [loop] for (uint i = 0; i < count; ++i)
    {
        float4 sampleGuide, sampleMaterial;
        float3 sampleDirect, sampleDiffuse;
        float secondaryDistance;
        uint secondarySpecular;
        float3 sample = TracePath(pixel, size, uint(Options.x) + i, sampleGuide, sampleMaterial,
                                  sampleDirect, sampleDiffuse, secondaryDistance, secondarySpecular);
#ifdef PATH_SAMPLE_NORMALIZE
        // Compress each complete path with one shared scale so components still sum to total.
        float sampleScale = 1.0 / (1.0 + dot(max(sample, 0), float3(.2126, .7152, .0722)));
        sample *= sampleScale;
        sampleDirect *= sampleScale;
        sampleDiffuse *= sampleScale;
#endif
        directSum += sampleDirect;
        diffuseSum += sampleDiffuse;
        if (i == 0) { guide = sampleGuide; material = sampleMaterial; }
        // Avoid mixing different primary surfaces within a jittered batch.
        if (guide.w > 0 && sampleGuide.w > 0 && dot(guide.xyz, sampleGuide.xyz) >= .99 &&
            abs(guide.w - sampleGuide.w) <= max(.02, guide.w * .01) &&
            length(material.rgb - sampleMaterial.rgb) <= .05 &&
            abs(material.a - sampleMaterial.a) <= .03)
            AddPathHitGuide(hitGuide, secondaryDistance, secondarySpecular != 0);
        sum += sample;
        float lum = dot(sample, float3(.2126, .7152, .0722));
        float delta = lum - mean;
        mean += delta / (i + 1);
        m2 += delta * (lum - mean);
    }
    SurfaceGuide[pixel] = guide;
    MaterialGuide[pixel] = material;
    Radiance[pixel] = float4(count > 0 ? sum / count : 0, 1);
    BatchMoments[pixelIndex] = float2(mean, m2);
    DirectRadiance[pixel] = float4(count > 0 ? directSum / count : 0, 1);
    DiffuseRadiance[pixel] = float4(count > 0 ? diffuseSum / count : 0, 1);
    SecondaryGuide[pixel] = ResolvePathHitGuide(hitGuide);
}

[shader("miss")]
void Miss(inout PathPayload payload) { payload.hit = 0; }

[shader("closesthit")]
void ClosestHit(inout PathPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    bool unlit;
    MaterialSurface surface = EvaluateRaySurface(InstanceID(), PrimitiveIndex(),
                                                  attributes.barycentrics, unlit);
    payload.hit = 1;
    payload.position = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
    payload.normal = surface.normalWS;
    payload.unlit = unlit ? 1 : 0;
    payload.albedo = surface.baseColor;
    payload.metallic = surface.metallic;
    payload.roughness = surface.perceptualRoughness;
    payload.emission = surface.emission;
}
