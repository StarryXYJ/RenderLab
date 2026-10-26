shader "Test/Parameters"
{
    use "parameters.params";
    pass Draw
    {
        parameters { [binding(BrdfLut)] Texture2D<float4> RenamedLut; }
        vertex = "parameters.hlsl:VSMain";
        pixel = "parameters.hlsl:PSMain";
    }
}
