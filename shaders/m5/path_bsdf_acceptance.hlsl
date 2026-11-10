#include "../RenderLab/PathBsdf.hlsli"
RWStructuredBuffer<float4> Result : register(u0);
[numthreads(1,1,1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint scenario = id.x;
    float nv = scenario / 2 == 0 ? 1 : scenario / 2 == 1 ? .5 : .1;
    float roughness = (scenario & 1) != 0 ? .8 : .25;
    float metallic = scenario % 3 == 0 ? 1 : 0;
    float3 n = float3(0, 0, 1);
    float3 v = float3(sqrt(1 - nv * nv), 0, nv);
    float3 sum = 0, diffuseSum = 0, specularSum = 0;
    float invalid = 0;
    [loop] for (uint i = 0; i < 65536; ++i)
    {
        float3 direction = SamplePathBsdf(n, v, roughness, PathSample(i, scenario, 1),
                                          PathSample(i, scenario, 5).x);
        float pdf = PathBsdfPdf(n, v, direction, roughness);
        if (pdf > 0)
        {
            float3 value = EvaluateBRDF(float3(.7, .4, .2), metallic, roughness, n, v,
                                         direction) / pdf;
            float3 diffuse, specular;
            EvaluateBRDFComponents(float3(.7, .4, .2), metallic, roughness, n, v,
                                   direction, diffuse, specular);
            float3 fraction = PathDiffuseFraction(diffuse, specular);
            float3 splitDiffuse = value * fraction;
            float3 splitSpecular = value - splitDiffuse;
            diffuseSum += splitDiffuse;
            specularSum += splitSpecular;
            invalid += any(abs((diffuse + specular) / pdf - value) >
                           1e-4 * max(1, abs(value))) ? 1 : 0;
            invalid += !all(isfinite(value)) || any(value < 0) ? 1 : 0;
            sum += value;
        }
    }
    Result[scenario] = float4(sum / 65536, invalid);
    Result[scenario + 6] = float4(diffuseSum / 65536, 0);
    Result[scenario + 12] = float4(specularSum / 65536, 0);
}
