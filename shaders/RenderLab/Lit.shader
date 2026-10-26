shader "RenderLab/Lit"
{
    use "Scene.params";
    material
    {
        [id("surface"), semantic(Surface)] SurfaceType Surface = Opaque;
        [id("blend")] BlendMode Blend = Alpha;
        [id("cull"), semantic(Cull)] CullMode Cull = Back;
        [id("cast_shadows")] bool CastShadows = true;
        [id("alpha_clip"), semantic(AlphaClip), keyword("RENDERLAB_ALPHA_CLIP")] bool AlphaClip = false;
        [id("base_color"), semantic(BaseColor), color] float4 BaseColor = (1, 1, 1, 1);
        [id("metallic"), semantic(Metallic), range(0, 1)] float Metallic = 0;
        [id("roughness"), semantic(Roughness), range(0, 1)] float Roughness = 0.5;
        [id("normal_scale"), semantic(NormalScale), range(0, 2)] float NormalScale = 1;
        [id("occlusion_strength"), semantic(OcclusionStrength), range(0, 1)] float OcclusionStrength = 1;
        [id("alpha_cutoff"), semantic(AlphaCutoff), range(0, 1)] float AlphaCutoff = 0.5;
        [id("emission"), semantic(Emission), color, hdr] float3 Emission = (0, 0, 0);
        [id("base_map"), semantic(BaseMap), srgb] Texture2D<float4> BaseMap = white;
        [id("metallic_roughness_map"), semantic(MetallicRoughnessMap)] Texture2D<float4> MetallicRoughnessMap = white;
        [id("normal_map"), semantic(NormalMap)] Texture2D<float4> NormalMap = normal;
        [id("occlusion_map"), semantic(OcclusionMap)] Texture2D<float4> OcclusionMap = white;
        [id("emission_map"), semantic(EmissionMap), srgb] Texture2D<float4> EmissionMap = white;
        [id("base_sampler")] SamplerState BaseSampler = point_clamp;
    }
    render { surface = $Surface; blend = $Blend; alphaClip = $AlphaClip; }
    pass Forward
    {
        role = Forward;
        features = { AlphaClip };
        parameters
        {
            [binding(EnvironmentAtlas)] Texture2D<float4> EnvironmentAtlas;
            [binding(BrdfLut)] Texture2D<float4> BrdfLut;
            [binding(MainLightShadow)] Texture2D<float4> MainLightShadow;
        }
        vertex = "Lit.hlsl:VSMain";
        pixel = "Lit.hlsl:PSMain";
        state { preset = SurfaceForward; cull = $Cull; }
    }
    pass Shadow
    {
        role = ShadowCaster;
        enabled = OpaqueShadow;
        castShadows = $CastShadows;
        features = { AlphaClip };
        vertex = "Shadow.hlsl:VSMain";
        pixel = "Shadow.hlsl:PSMain";
        state { preset = ShadowCaster; cull = $Cull; }
    }
}
