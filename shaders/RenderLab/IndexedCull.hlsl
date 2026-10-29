#include <renderlab/generated/types.hlsli>
struct IndexedInstance
{
    row_major float4x4 world, normal, previousWorld;
    float4 minimum, maximum;
    uint4 indices, reserved;
};
struct Batch { uint offset, count, reserved0, reserved1; };
struct Arguments
{
    uint indexCount, instanceCount, firstIndex;
    int vertexOffset;
    uint firstInstance;
};
StructuredBuffer<IndexedInstance> Objects : register(t0);
StructuredBuffer<uint> Candidates : register(t1);
StructuredBuffer<Batch> Batches : register(t2);
RWStructuredBuffer<uint> Visible : register(u3);
RWStructuredBuffer<Arguments> Draws : register(u4);
RWStructuredBuffer<uint> Counts : register(u5);
ConstantBuffer<SceneViewData> SceneView : register(b6);

[numthreads(64, 1, 1)]
void Clear(uint id : SV_DispatchThreadID)
{
    uint count, stride;
    Counts.GetDimensions(count, stride);
    if (id < count) Counts[id] = 0;
}
bool InFrustum(IndexedInstance object)
{
    bool outside[6] = {true, true, true, true, true, true};
    for (uint corner = 0; corner < 8; ++corner)
    {
        float3 position = float3((corner & 1) ? object.maximum.x : object.minimum.x,
            (corner & 2) ? object.maximum.y : object.minimum.y,
            (corner & 4) ? object.maximum.z : object.minimum.z);
        float4 p = mul(SceneView.ViewProjection, float4(position, 1));
        outside[0] = outside[0] && p.x < -p.w;
        outside[1] = outside[1] && p.x > p.w;
        outside[2] = outside[2] && p.y < -p.w;
        outside[3] = outside[3] && p.y > p.w;
        outside[4] = outside[4] && p.z < 0;
        outside[5] = outside[5] && p.z > p.w;
    }
    return !(outside[0] || outside[1] || outside[2] || outside[3] || outside[4] || outside[5]);
}
groupshared uint prefix[64], running;
[numthreads(64, 1, 1)]
void Cull(uint batchIndex : SV_GroupID, uint lane : SV_GroupIndex)
{
    Batch batch = Batches[batchIndex];
    if (lane == 0) running = Counts[batchIndex];
    GroupMemoryBarrierWithGroupSync();
    // Stable compaction in one group per pipeline batch. All lanes visit every chunk.
    for (uint chunk = 0; chunk < batch.count; chunk += 64)
    {
        uint candidate = chunk + lane;
        uint objectIndex = 0;
        bool visible = false;
        if (candidate < batch.count)
        {
            objectIndex = Candidates[batch.offset + candidate];
            visible = InFrustum(Objects[objectIndex]);
        }
        prefix[lane] = visible ? 1 : 0;
        GroupMemoryBarrierWithGroupSync();
        if (lane == 0)
            for (uint i = 1; i < 64; ++i) prefix[i] += prefix[i - 1];
        GroupMemoryBarrierWithGroupSync();
        if (visible)
        {
            uint slot = batch.offset + running + prefix[lane] - 1;
            // Capacity is the full candidate count of this batch, validated on the CPU.
            IndexedInstance object = Objects[objectIndex];
            Visible[slot] = objectIndex;
            Arguments draw;
            draw.indexCount = object.indices.z;
            draw.instanceCount = 1;
            draw.firstIndex = object.indices.x;
            draw.vertexOffset = int(object.indices.w);
            draw.firstInstance = slot;
            Draws[slot] = draw;
        }
        GroupMemoryBarrierWithGroupSync();
        if (lane == 0) running += prefix[63];
        GroupMemoryBarrierWithGroupSync();
    }
    if (lane == 0) Counts[batchIndex] = running;
}
