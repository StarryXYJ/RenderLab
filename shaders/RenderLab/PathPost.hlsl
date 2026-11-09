Texture2D<float4> Current : register(t0);
Texture2D<float4> Previous : register(t1);
#ifdef SPIRV
[[vk::image_format("rgba32f")]]
#endif
RWTexture2D<float4> Output : register(u2);
cbuffer PathParameters : register(b3)
{
    row_major float4x4 InverseViewProjection;
    float4 CameraPosition, DirectionToLight, LightColor, Environment;
    float4 Options;
};
StructuredBuffer<float2> BatchMoments : register(t4);
StructuredBuffer<float2> PreviousMoments : register(t5);
RWStructuredBuffer<float2> OutputMoments : register(u6);

[numthreads(8,8,1)]
void Accumulate(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    Output.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;
    float previousCount = min(Options.y, Options.w);
    float count = min(Environment.z, Options.w - previousCount);
    if (previousCount >= Options.w)
    {
        Output[id.xy] = Previous.Load(int3(id.xy, 0));
        return;
    }
    float4 value = Current.Load(int3(id.xy, 0));
    if (previousCount > 0)
    {
        float4 previous = Previous.Load(int3(id.xy, 0));
        value = previous + (value - previous) * (count / (previousCount + count));
    }
    Output[id.xy] = value;
}

[numthreads(8,8,1)]
void Statistics(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    Current.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;
    uint pixel = id.x + id.y * width;
    float2 batch = BatchMoments[pixel];
    float oldCount = min(Options.y, Options.w);
    float batchCount = min(Environment.z, Options.w - oldCount);
    if (oldCount >= Options.w) { OutputMoments[pixel] = PreviousMoments[pixel]; return; }
    if (oldCount == 0) { OutputMoments[pixel] = batch; return; }
    float2 old = PreviousMoments[pixel];
    float total = oldCount + batchCount;
    float delta = batch.x - old.x;
    OutputMoments[pixel] = float2(old.x + delta * batchCount / total,
        old.y + batch.y + delta * delta * oldCount * batchCount / total);
}
