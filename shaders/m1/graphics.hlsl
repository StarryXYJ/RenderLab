#ifdef SPIRV
#define BINDING(slot) [[vk::binding(slot, 0)]]
#else
#define BINDING(slot)
#endif

BINDING(1) Texture2D<float4> Image : register(t1);
BINDING(2) SamplerState PointClamp : register(s2);

struct VertexInput
{
    float3 position : POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};
struct Varyings
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

Varyings VSMain(VertexInput input)
{
    Varyings output;
    // Column-vector convention; translation in the final column, deliberately non-symmetric.
    const float4x4 transform = float4x4(
        1, 0, 0, 0.03125,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1);
    output.position = mul(transform, float4(input.position, 1));
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

float4 PSMain(Varyings input) : SV_Target0
{
    if (input.color.a == 0)
    {
        return Image.SampleLevel(PointClamp, input.uv, 0);
    }
    // The sRGB attachment encodes after linear-space blending.
    return input.color;
}
