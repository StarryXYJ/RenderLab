#include "Lighting.hlsli"
#include "MaterialSampling.hlsli"
#include "ImageBasedLighting.hlsli"

struct Attributes { float3 positionOS : POSITION; float3 normalOS : NORMAL; float2 uv : TEXCOORD0; };
struct Varyings { float4 positionCS : SV_Position; float3 positionWS : TEXCOORD0; float3 normalWS : TEXCOORD1; float2 uv : TEXCOORD2; };
Varyings VSMain(Attributes input)
{
    Varyings output;
    output.positionCS = TransformObjectToHClip(input.positionOS);
    output.positionWS = TransformObjectToWorld(input.positionOS);
    output.normalWS = TransformObjectToWorldNormal(input.normalOS);
    output.uv = input.uv;
    return output;
}
float4 PSMain(Varyings input, bool frontFace : SV_IsFrontFace) : SV_Target
{
    RasterSamplingContext context;
    context.uv = input.uv;
    context.uvDx = ddx(input.uv);
    context.uvDy = ddy(input.uv);
    context.positionDx = ddx(input.positionWS);
    context.positionDy = ddy(input.positionWS);
    context.normalWS = frontFace ? input.normalWS : -input.normalWS;
    MaterialSurface surface = EvaluateMaterial(context);
    float3 viewDirWS = GetWorldSpaceViewDir(input.positionWS);
    DirectionalLight light = GetMainLight();
    float3 direct = EvaluateBRDF(surface.baseColor, surface.metallic, surface.perceptualRoughness,
        surface.normalWS, SafeNormalize(viewDirWS), light.direction) * light.color;
    return float4(direct + EvaluateImageBasedLighting(surface, viewDirWS) + surface.emission,
        surface.alpha);
}
