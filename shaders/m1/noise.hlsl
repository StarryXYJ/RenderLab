#ifdef SPIRV
[[vk::binding(0, 0)]] [[vk::image_format("rgba8")]]
#endif
RWTexture2D<float4> Output : register(u0);

uint Hash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352d;
    value ^= value >> 15;
    value *= 0x846ca68b;
    return value ^ (value >> 16);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    Output.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
    {
        return;
    }
#ifdef UPDATE
    uint3 bytes = uint3(round(Output[id.xy].rgb * 255));
    bytes ^= uint3(0x5a, 0xa5, 0x3c);
#else
    uint value = Hash(id.x + id.y * width + 0x12345678);
    uint3 bytes = uint3(value, value >> 8, value >> 16) & 255;
#endif
    Output[id.xy] = float4(float3(bytes) / 255, 1);
}
