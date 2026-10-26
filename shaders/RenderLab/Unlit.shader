shader "RenderLab/Unlit"
{
    use "Scene.params";
    material
    {
        [id("surface")] SurfaceType Surface = Opaque;
        [id("blend")] BlendMode Blend = Alpha;
        [id("cull")] CullMode Cull = Back;
        [id("cast_shadows")] bool CastShadows = true;
        [id("alpha_clip"), keyword("RENDERLAB_ALPHA_CLIP")] bool AlphaClip = false;
        [id("base_color"), color, hdr] float4 BaseColor = (1, 1, 1, 1);
        [id("alpha_cutoff"), range(0, 1)] float AlphaCutoff = 0.5;
        [id("base_map"), srgb] Texture2D<float4> BaseMap = white;
        SamplerState BaseSampler = point_clamp;
    }
    render { surface = $Surface; blend = $Blend; alphaClip = $AlphaClip; }
    pass Forward
    {
        role = Forward;
        features = { AlphaClip };
        vertex = "Unlit.hlsl:VSMain";
        pixel = "Unlit.hlsl:PSMain";
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
