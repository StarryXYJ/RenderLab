#include "../RenderLab/RaySampling.hlsli"
RWStructuredBuffer<float4> Result : register(u0);
[numthreads(1,1,1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint scenario = id.x;
    float nv = scenario / 2 == 0 ? 1 : scenario / 2 == 1 ? .1 : .001;
    float roughness = (scenario & 1) != 0 ? .8 : .045;
    float3 view = float3(sqrt(1 - nv * nv), 0, nv);
    float total = 0, maximum = 0, invalid = 0;
    uint quadrants[4] = {0,0,0,0};
    [loop] for (uint i = 0; i < 4096; ++i)
    {
        float2 xi = ReflectionSample(i, scenario);
        quadrants[(xi.x >= .5 ? 1 : 0) + (xi.y >= .5 ? 2 : 0)]++;
        float3 normal = SampleVisibleGGX(view, roughness * roughness, xi);
        float nl = reflect(-view, normal).z;
        float weight = nl > 0 ? VisibleGGXWeight(nv, nl, roughness) : 0;
        invalid += !isfinite(weight) || weight < 0 || weight > 1.0001 ||
            dot(view, normal) <= 0 || abs(dot(normal, normal) - 1) > 1e-4 ? 1 : 0;
        total += weight;
        maximum = max(maximum, weight);
    }
    float discrepancy = 0;
    [unroll] for (uint q = 0; q < 4; ++q) discrepancy += abs(int(quadrants[q]) - 1024);
    Result[scenario] = float4(total / 4096, maximum, invalid, discrepancy);
}
