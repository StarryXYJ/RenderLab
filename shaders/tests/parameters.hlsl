#include <renderlab/generated/parameters.hlsli>
#include "parameter_library.hlsli"
float4 VSMain(float3 p : POSITION) : SV_Position { return float4(p, 1); }
float4 PSMain() : SV_Target { return EvaluateParameters(Inputs); }
