shader "RenderLab/DeferredLighting"
{
    use "View.params";
    use "Lighting.params";
    use "Shadow.params";
    pass DeferredLighting
    {
        parameters
        {
            float4x4 InverseViewProjection;
            uint RayTracedShadows;
            Texture2D<float4> GBufferAlbedoMetallic;
            Texture2D<float4> GBufferNormalRoughness;
            Texture2D<float4> GBufferEmissionOcclusion;
            Texture2D<float4> CameraDepth;
            Texture2D<float4> EnvironmentAtlas;
            Texture2D<float4> BrdfLut;
            Texture2D<float4> MainLightShadow;
            Texture2D<float4> ShadowVisibility;
        }
        vertex = "DeferredLighting.hlsl:VSMain";
        pixel = "DeferredLighting.hlsl:PSMain";
    }
}
