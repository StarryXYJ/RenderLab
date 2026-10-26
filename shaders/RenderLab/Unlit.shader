shader "RenderLab/Unlit"
{
    use "Scene.params";
    material
    {
        [id("base_color"), color, hdr] float4 BaseColor = (1, 1, 1, 1);
        [id("base_map"), srgb] Texture2D<float4> BaseMap = white;
        SamplerState BaseSampler = point_clamp;
    }
    variant Default {}
    pass Forward
    {
        vertex = "Unlit.hlsl:VSMain";
        pixel = "Unlit.hlsl:PSMain";
        depth = true;
        blend = false;
        cull = true;
    }
}
