// Fixed graph synchronization probe; authored Shader assets use the M2 declaration compiler.
cbuffer Parameters : register(b0, space0)
{
    uint Scale;
};
StructuredBuffer<uint4> Input : register(t1, space0);
RWStructuredBuffer<uint4> Output : register(u2, space0);
[numthreads(16, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    Output[id.x] = Input[id.x] * Scale + uint4(1, 2, 3, 4);
}
