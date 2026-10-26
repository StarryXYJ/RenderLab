shader "RenderLab/ToneMapping"
{
    variant Default {}
    pass Display
    {
        parameters
        {
            float Exposure = 0;
            Texture2D<float4> HdrColor;
        }
        vertex = "ToneMapping.hlsl:VSMain";
        pixel = "ToneMapping.hlsl:PSMain";
        depth = false;
        cull = false;
    }
}
