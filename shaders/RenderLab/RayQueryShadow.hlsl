RaytracingAccelerationStructure Scene : register(t0);
Texture2D<float> CameraDepth : register(t1);
Texture2D<float4> NormalRoughness : register(t2);
cbuffer RayQueryParameters : register(b3)
{
    row_major float4x4 InverseViewProjection;
    float4 DirectionToLight;
    // Normal bias, ray minimum distance, ray maximum distance, enabled.
    float4 ShadowOptions;
};
#ifdef SPIRV
[[vk::image_format("rgba8")]]
#endif
RWTexture2D<float4> Visibility : register(u4);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    Visibility.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
        return;
    float visibility = 1;
    const float depth = CameraDepth.Load(int3(id.xy, 0));
    if (depth > 1e-7 && ShadowOptions.w > 0)
    {
        const float2 ndc = float2((id.x + 0.5) * 2 / width - 1,
                                  1 - (id.y + 0.5) * 2 / height);
        const float4 world = mul(InverseViewProjection, float4(ndc, depth, 1));
        const float3 normal = normalize(NormalRoughness.Load(int3(id.xy, 0)).xyz);
        RayDesc ray;
        ray.Origin = world.xyz / world.w + normal * ShadowOptions.x;
        ray.Direction = normalize(DirectionToLight.xyz);
        ray.TMin = ShadowOptions.y;
        ray.TMax = ShadowOptions.z;
        RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
        query.TraceRayInline(Scene, RAY_FLAG_NONE, 0x01, ray);
        while (query.Proceed()) {}
        visibility = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0 : 1;
    }
    Visibility[id.xy] = float4(visibility, visibility, visibility, 1);
}
