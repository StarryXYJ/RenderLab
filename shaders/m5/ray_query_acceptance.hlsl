RaytracingAccelerationStructure Scene : register(t0);
RWStructuredBuffer<uint> Result : register(u1);

[numthreads(1, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    RayDesc ray;
    ray.Origin = float3(0, 0, -1);
    ray.Direction = float3(0, 0, 1);
    ray.TMin = 0.001;
    ray.TMax = 10;

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(Scene, RAY_FLAG_NONE, 0xff, ray);
    while (query.Proceed())
    {
    }
    Result[0] = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 1 : 0;
}
