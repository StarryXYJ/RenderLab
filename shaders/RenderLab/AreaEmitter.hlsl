// Raster-only visualisation of the same single-sided parallelograms used for lighting.
#include <renderlab/generated/types.hlsli>
ConstantBuffer<SceneViewData> SceneView : register(b0);
ConstantBuffer<SceneAreaLightingData> SceneAreaLighting : register(b1);
struct Attributes { float3 vertex : POSITION; };
struct Varyings { float4 position : SV_Position; nointerpolation float3 radiance : COLOR0; };
Varyings VSMain(Attributes input)
{
    bool second = input.vertex.z > .5;
    float4 center = second ? SceneAreaLighting.Center1 : SceneAreaLighting.Center0;
    float4 u = second ? SceneAreaLighting.EdgeU1 : SceneAreaLighting.EdgeU0;
    float4 v = second ? SceneAreaLighting.EdgeV1 : SceneAreaLighting.EdgeV0;
    float3 position = center.xyz + input.vertex.x * u.xyz + input.vertex.y * v.xyz;
    float3 normal = -cross(u.xyz, v.xyz);
    Varyings output;
    output.position = mul(SceneView.ViewProjection, float4(position, 1));
    output.radiance = dot(normal, SceneView.CameraPosition.xyz - center.xyz) > 0
        ? float3(center.w, u.w, v.w) : 0;
    return output;
}
float4 PSForward(Varyings input) : SV_Target { return float4(input.radiance, 1); }
struct GBufferOutput
{
    float4 albedoMetallic : SV_Target0;
    float4 normalRoughness : SV_Target1;
    float4 emissionOcclusion : SV_Target2;
};
GBufferOutput PSGBuffer(Varyings input)
{
    GBufferOutput output;
    output.albedoMetallic = float4(input.radiance, 0);
    output.normalRoughness = float4(0, 0, 1, -1);
    output.emissionOcclusion = float4(0, 0, 0, 1);
    return output;
}
