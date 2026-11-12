#include <renderlab/generated/parameters.hlsli>
#include "Core.hlsli"
#include "Lighting.hlsli"
#include "AreaLighting.hlsli"
#include "AreaShadow.hlsli"
#include "AreaVisibilityFilter.hlsli"
#include "ImageBasedLighting.hlsli"
#include "Shadows.hlsli"

struct Attributes
{
    float2 positionOS : POSITION;
};

struct Varyings
{
    float4 positionCS : SV_Position;
};

Varyings VSMain(Attributes input)
{
    Varyings output;
    output.positionCS = float4(input.positionOS, 0, 1);
    return output;
}

float4 PSMain(Varyings input) : SV_Target
{
    uint width, height;
    GBufferAlbedoMetallic.GetDimensions(width, height);
    const int2 pixel = int2(input.positionCS.xy);
    const float depth = CameraDepth.Load(int3(pixel, 0)).x;
    clip(depth - 1e-7);

    const float2 ndc = float2((float(pixel.x) + 0.5) * 2 / width - 1,
                              1 - (float(pixel.y) + 0.5) * 2 / height);
    const float4 worldH = mul(DeferredLightingParameters.InverseViewProjection,
                              float4(ndc, depth, 1));
    const float3 positionWS = worldH.xyz / worldH.w;
    const float4 albedoMetallic = GBufferAlbedoMetallic.Load(int3(pixel, 0));
    const float4 normalRoughness = GBufferNormalRoughness.Load(int3(pixel, 0));
    const float4 emissionOcclusion = GBufferEmissionOcclusion.Load(int3(pixel, 0));
    if (normalRoughness.a < 0)
        return float4(albedoMetallic.rgb, 1);

    MaterialSurface surface;
    surface.baseColor = max(albedoMetallic.rgb, 0);
    surface.alpha = 1;
    surface.normalWS = SafeNormalize(normalRoughness.xyz);
    surface.metallic = saturate(albedoMetallic.a);
    surface.perceptualRoughness = clamp(normalRoughness.a, 0.045, 1);
    surface.occlusion = saturate(emissionOcclusion.a);
    surface.emission = emissionOcclusion.rgb;

    const float3 viewDirWS = GetWorldSpaceViewDir(positionWS, SceneView.CameraPosition.xyz);
    const DirectionalLight light = GetMainLight(SceneLighting);
    const float3 shadowCoordinate = ProjectMainLightShadow(SceneShadow, positionWS,
                                                           surface.normalWS);
    const RasterShadowSamplingContext shadowSampling = MakeShadowSamplingContext(
        shadowCoordinate, ddx(shadowCoordinate), ddy(shadowCoordinate));
    const float visibility = DeferredLightingParameters.RayTracedShadows != 0
        ? ShadowVisibility.Load(int3(pixel, 0)).x
        : MainLightVisibility(SceneShadow, MainLightShadow, shadowSampling);
    float3 direct = EvaluateBRDF(surface.baseColor, surface.metallic,
        surface.perceptualRoughness, surface.normalWS, SafeNormalize(viewDirWS), light.direction)
        * light.color * visibility;
    float2 areaVisibility = 1;
    if (SceneAreaLighting.Header.x > 0)
        areaVisibility = DeferredLightingParameters.RayTracedShadows != 0
            ? FilterAreaVisibility(ShadowVisibility, CameraDepth, GBufferNormalRoughness,
                                   pixel, int2(width, height), depth, surface.normalWS,
                                   max(.00005, min(depth * .04,
                                       2 * max(abs(ddx(depth)), abs(ddy(depth))))))
            : AreaShadowVisibility(SceneAreaShadow, AreaShadow0, AreaShadow1, positionWS, surface.normalWS);
    direct += EvaluateAreaLights(SceneAreaLighting, positionWS, surface.normalWS,
                                 SafeNormalize(viewDirWS), surface.baseColor, surface.metallic,
                                 surface.perceptualRoughness, LtcMatrix, LtcAmplitude, areaVisibility);
    const float3 indirect = EvaluateImageBasedLighting(surface, viewDirWS,
        SceneLighting.EnvironmentParameters, EnvironmentAtlas, BrdfLut);
    return float4(direct + indirect + surface.emission, 1);
}
