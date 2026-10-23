shader "Demo/Surface"
{
    use "view.params";

    material
    {
        [id("base_color"), label("基础色"), color, group("表面")]
        float4 BaseColor = (0.25, 0.5, 0.75, 1);
        [id("roughness"), range(0, 1), tooltip("用于验收参数元数据")]
        float Roughness = 0.5;
        [id("unused"), label("未使用参数仍保留")]
        float Unused = 0.125;
        int Mode = 2;
        bool Enabled = true;
        uint Seed = 7;
        float3 Direction = (1, 2, 3);
        float Weights[2] = (0.2, 0.8);
        float3x2 Basis = (1, 2, 3, 4, 5, 6);
        [id("base_map"), srgb]
        Texture2D<float4> BaseMap = white;
        SamplerState BaseSampler = point_clamp;
    }

    variant Default { DETAIL = 0; }
    variant Detail { DETAIL = 1; }

    pass Forward
    {
        vertex = "surface.hlsl:VSMain";
        pixel = "surface.hlsl:PSMain";
        depth = false;
        cull = false;
    }
    pass Alternate
    {
        vertex = "surface.hlsl:VSMain";
        pixel = "surface.hlsl:PSAlternate";
        depth = false;
        cull = false;
    }
    pass Probe
    {
        parameters
        {
            uint Count = 1;
            StructuredBuffer<float4> Input;
            RWStructuredBuffer<float4> Results;
            [format(rgba8_unorm)] RWTexture2D<float4> Output;
        }
        compute = "probe.hlsl:CSMain";
    }
}
