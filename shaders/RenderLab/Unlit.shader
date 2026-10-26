shader "RenderLab/Unlit"
{
    use "Scene.params";
    material
    {
        [id("base_color"), color, hdr] float4 BaseColor = (1, 1, 1, 1);
        [id("alpha_cutoff"), range(0, 1)] float AlphaCutoff = 0.5;
        [id("base_map"), srgb] Texture2D<float4> BaseMap = white;
        SamplerState BaseSampler = point_clamp;
    }
    variant Default { ALPHA_MASK = 0; ALPHA_BLEND = 0; }
    variant Masked { ALPHA_MASK = 1; ALPHA_BLEND = 0; }
    variant Transparent { ALPHA_MASK = 0; ALPHA_BLEND = 1; }
    pass Forward
    {
        vertex = "Unlit.hlsl:VSMain";
        pixel = "Unlit.hlsl:PSMain";
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
