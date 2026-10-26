shader "RenderLab/BlinnPhong"
{
    use "Scene.params";
    material
    {
        [id("base_color"), color] float4 BaseColor = (1, 1, 1, 1);
        [id("specular_color"), color] float3 SpecularColor = (0.2, 0.2, 0.2);
        [id("shininess"), range(1, 256)] float Shininess = 32;
        [id("base_map"), srgb] Texture2D<float4> BaseMap = white;
        SamplerState BaseSampler = point_clamp;
    }
    pass Forward
    {
        role = Forward;
        vertex = "BlinnPhong.hlsl:VSMain";
        pixel = "BlinnPhong.hlsl:PSMain";
        state {
            depth { test = true; write = true; }
            blend { enabled = false; }
            cull = Back;
        }
    }
}
