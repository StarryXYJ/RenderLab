#include <renderlab/generated/parameters.hlsli>
#include "Core.hlsli"
#include "Alpha.hlsli"
struct Attributes { float3 positionOS : POSITION; float3 normalOS : NORMAL; float2 uv : TEXCOORD0; };
struct Varyings { float4 positionCS : SV_Position; float2 uv : TEXCOORD0; };
Varyings VSMain(Attributes input)
{
    Varyings output;
    output.positionCS = TransformObjectToHClip(input.positionOS, SceneObject.ObjectToWorld, SceneView.ViewProjection);
    output.uv = input.uv;
    return output;
}
float4 PSMain(Varyings input) : SV_Target
{
    float4 color = Material.BaseColor * BaseMap.Sample(BaseSampler, input.uv);
#if RENDERLAB_ALPHA_CLIP
    ApplyAlphaCutoff(color.a, Material.AlphaCutoff);
#endif
    return EncodeSurfaceOutput(color.rgb, color.a, Material.Surface, Material.Blend);
}
