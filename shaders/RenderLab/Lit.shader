shader "RenderLab/Lit"
{
    use "Scene.params";
    material
    {
        [id("base_color"), color] float4 BaseColor = (1, 1, 1, 1);
        [id("metallic"), range(0, 1)] float Metallic = 0;
        [id("roughness"), range(0, 1)] float Roughness = 0.5;
        [id("normal_scale"), range(0, 2)] float NormalScale = 1;
        [id("occlusion_strength"), range(0, 1)] float OcclusionStrength = 1;
        [id("alpha_cutoff"), range(0, 1)] float AlphaCutoff = 0.5;
        [id("emission"), color, hdr] float3 Emission = (0, 0, 0);
        [id("base_map"), srgb] Texture2D<float4> BaseMap = white;
        Texture2D<float4> MetallicRoughnessMap = white;
        Texture2D<float4> NormalMap = normal;
        Texture2D<float4> OcclusionMap = white;
        [srgb] Texture2D<float4> EmissionMap = white;
        SamplerState BaseSampler = point_clamp;
    }
    variant Default { ALPHA_MASK = 0; ALPHA_BLEND = 0; }
    variant Masked { ALPHA_MASK = 1; ALPHA_BLEND = 0; }
    variant Transparent { ALPHA_MASK = 0; ALPHA_BLEND = 1; }
    pass Forward
    {
        parameters
        {
            Texture2D<float4> EnvironmentAtlas;
            Texture2D<float4> BrdfLut;
            Texture2D<float4> MainLightShadow;
        }
        vertex = "Lit.hlsl:VSMain";
        pixel = "Lit.hlsl:PSMain";
        depth = true;
        blend = false;
        cull = true;
    }
    pass Shadow
    {
        vertex = "Shadow.hlsl:VSMain";
        pixel = "Shadow.hlsl:PSMain";
        depth = true;
        blend = false;
        cull = true;
    }
}
