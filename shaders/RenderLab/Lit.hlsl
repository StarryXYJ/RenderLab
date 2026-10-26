#include "LitSurface.hlsli"
#include "Lighting.hlsli"
#include "ImageBasedLighting.hlsli"
#include "Shadows.hlsli"

float4 PSMain(Varyings input, bool frontFace : SV_IsFrontFace) : SV_Target
{
    MaterialSurface surface = EvaluateLitSurface(input, frontFace);
    float3 shadowCoordinate = ProjectMainLightShadow(SceneShadow, input.positionWS,
                                                    SafeNormalize(surface.normalWS));
    RasterShadowSamplingContext shadowSampling = MakeShadowSamplingContext(
        shadowCoordinate, ddx(shadowCoordinate), ddy(shadowCoordinate));
    float visibility = MainLightVisibility(SceneShadow, MainLightShadow, shadowSampling);
    float3 viewDirWS = GetWorldSpaceViewDir(input.positionWS, SceneView.CameraPosition.xyz);
    DirectionalLight light = GetMainLight(SceneLighting);
    float3 direct = EvaluateBRDF(surface.baseColor, surface.metallic,
                                 surface.perceptualRoughness, surface.normalWS,
                                 SafeNormalize(viewDirWS), light.direction) * light.color * visibility;
    float3 indirect = EvaluateImageBasedLighting(surface, viewDirWS,
                                                 SceneLighting.EnvironmentParameters,
                                                 EnvironmentAtlas, BrdfLut);
    return EncodeSurfaceOutput(direct + indirect + surface.emission, surface.alpha,
                               Material.Surface, Material.Blend);
}
