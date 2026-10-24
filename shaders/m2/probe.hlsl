#include <renderlab/generated/parameters.hlsli>

[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= ProbeParameters.Count) return;
    Results[0] = Material.BaseColor;
    Results[1] = float4(Material.Roughness, float(Material.Mode), float(Material.Enabled), float(Material.Seed));
    Results[2] = float4(Material.Direction, Material.Weights[1]);
    Results[3] = float4(Material.Basis[0][0], Material.Basis[0][1], Material.Basis[2][0], Material.Basis[2][1]);
    Results[4] = Input[0] + View.CameraPosition + Draw.Offset;
    Output[id.xy] = Material.BaseColor;
}
