shader "RenderLab/ToneMapping"
{
    pass Display
    {
        parameters
        {
            float Exposure = 0;
            uint RawView = 0;
            float2 ViewportSize = (1, 1);
            Texture2D<float4> HdrColor;
        }
        vertex = "ToneMapping.hlsl:VSMain";
        pixel = "ToneMapping.hlsl:PSMain";
        state {
            depth { test = false; write = false; }
            cull = None;
        }
    }
}
