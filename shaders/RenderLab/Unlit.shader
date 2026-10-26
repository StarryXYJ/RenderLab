shader "RenderLab/Unlit"
{
    use "View.params";
    use "Object.params";
    use "Shadow.params";
    material
    {
        [id("surface"), semantic(Surface)] SurfaceType Surface = Opaque;
        [id("blend")] BlendMode Blend = Alpha;
        [id("cull"), semantic(Cull)] CullMode Cull = Back;
        [id("cast_shadows")] bool CastShadows = true;
        [id("alpha_clip"), semantic(AlphaClip), keyword("RENDERLAB_ALPHA_CLIP")] bool AlphaClip = false;
        [id("base_color"), semantic(BaseColor), color, hdr] float4 BaseColor = (1, 1, 1, 1);
        [id("alpha_cutoff"), semantic(AlphaCutoff), range(0, 1)] float AlphaCutoff = 0.5;
        [id("base_map"), semantic(BaseMap), srgb] Texture2D<float4> BaseMap = white;
        [id("base_sampler")] SamplerState BaseSampler = point_clamp;
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
    pass GBuffer
    {
        role = GBuffer;
        features = { AlphaClip };
        vertex = "Unlit.hlsl:VSMain";
        pixel = "Unlit.hlsl:PSGBuffer";
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
