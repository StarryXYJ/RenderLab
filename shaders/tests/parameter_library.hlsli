#include <renderlab/generated/types.hlsli>
float4 EvaluateParameters(AbiData data)
{
    return float4(data.Basis[0][1], data.Weights[1], data.Enabled, data.Choice);
}
