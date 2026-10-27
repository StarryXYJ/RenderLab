#include "ImageBasedLighting.hlsli"
Texture2D<float4> Current : register(t0);
Texture2D<float4> Previous : register(t1);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> Output : register(u2);
cbuffer ReflectionParameters : register(b3)
{
    row_major float4x4 InverseViewProjection;
    float4 CameraPosition, DirectionToLight, LightColor, Environment;
    float4 Options;
};
Texture2D<float4> CameraColor : register(t4);
Texture2D<float4> NormalRoughness : register(t5);
Texture2D<float4> AlbedoMetallic : register(t6);
Texture2D<float> Depth : register(t7);
Texture2D<float4> EnvironmentAtlas : register(t8);
Texture2D<float4> BrdfLut : register(t9);
Texture2D<float4> EmissionOcclusion : register(t10);
[numthreads(8,8,1)]
void Accumulate(uint3 id : SV_DispatchThreadID)
{
    uint width, height; Output.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;
    // Stop at the exact float count limit rather than silently switching to a noisy EMA.
    if (Options.y >= Options.w)
    {
        Output[id.xy] = Previous.Load(int3(id.xy,0));
        return;
    }
    float4 value = Current.Load(int3(id.xy,0));
    // Version mismatch sets count=0. The previous texture is never sampled in that case.
    if (Options.y > 0)
    {
        float4 previous = Previous.Load(int3(id.xy,0));
        value = previous + (value - previous) / (Options.y + 1);
    }
    Output[id.xy] = value;
}
[numthreads(8,8,1)]
void Combine(uint3 id : SV_DispatchThreadID)
{
    uint width, height; Output.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;
    float4 color = CameraColor.Load(int3(id.xy,0));
    float4 nr = NormalRoughness.Load(int3(id.xy,0));
    float depth = Depth.Load(int3(id.xy,0));
    if (depth > 1e-7 && nr.w >= 0)
    {
        float2 ndc = float2((id.x+.5)*2/width-1,1-(id.y+.5)*2/height);
        float4 world = mul(InverseViewProjection,float4(ndc,depth,1));
        float3 v = SafeNormalize(CameraPosition.xyz-world.xyz/world.w), n = SafeNormalize(nr.xyz);
        float4 albedo = AlbedoMetallic.Load(int3(id.xy,0));
        float roughness = clamp(nr.w,.045,1), level = roughness*5;
        uint low = uint(floor(level));
        float3 direction = reflect(-v,n);
        float3 environment = lerp(EnvironmentTile(direction,low,EnvironmentAtlas,Environment),
            EnvironmentTile(direction,min(low+1,5),EnvironmentAtlas,Environment),frac(level));
        float2 brdf = EnvironmentBrdf(saturate(dot(n,v)),roughness,BrdfLut);
        float3 environmentSpecular = environment*(lerp(.04.xxx,albedo.rgb,albedo.a)*brdf.x+brdf.y);
        // Rough surfaces retain the stable prefiltered IBL baseline; replace its specular term.
        float blend = saturate(1-roughness*roughness);
        color.rgb = max(0, color.rgb + blend * EmissionOcclusion.Load(int3(id.xy,0)).a * (Current.Load(int3(id.xy,0)).rgb-environmentSpecular));
    }
    Output[id.xy] = color;
}
