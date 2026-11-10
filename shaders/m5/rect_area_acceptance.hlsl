#include "rect_area_sampling.hlsli"
RWStructuredBuffer<float4> Result : register(u0);
[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    RectEmitter light;
    light.center = float3(0, 0, 1);
    light.edgeU = float3(2, 0, 0);
    light.edgeV = float3(0, 2, 0);
    light.normal = float3(0, 0, -1);
    light.area = 4;
    RectEmitterSample center = SampleRectEmitter(light, 0, float2(.5, .5), .25);
    RectEmitterSample back = SampleRectEmitter(light, float3(0, 0, 2), float2(.5, .5), 1);
    RectEmitterSample onLight = SampleRectEmitter(light, light.center, float2(.5, .5), 1);
    Result[0] = float4(center.distance, center.pdfOmega, back.pdfOmega, onLight.pdfOmega);
    Result[1] = float4(RectPowerWeight(2, 3), RectPowerWeight(3, 2),
                       RectPowerWeight(0, 0), RectPowerWeight(1, 0));
    float sum = 0;
    [loop] for (uint y = 0; y < 256; ++y)
    {
        [loop] for (uint x = 0; x < 256; ++x)
        {
            RectEmitterSample s = SampleRectEmitter(light, 0, (float2(x, y) + .5) / 256, 1);
            // Unit Lambertian BRDF, receiver normal +Z; light emits unit radiance.
            sum += s.direction.z / (3.141592653589793 * s.pdfOmega);
        }
    }
    Result[2] = float4(sum / 65536, 0, 0, 0);
    RectEmitterSample corner = SampleRectEmitter(light, 0, float2(.75, .75), 1);
    Result[3] = float4(corner.position.xy, corner.pdfOmega, RectPowerWeight(1e30, 1e30));
    light.edgeV = float3(1, 2, 0);
    RectEmitterSample sheared = SampleRectEmitter(light, 0, float2(.75, .75), 1);
    float noSelection = RectEmitterPdfOmega(light, 0, light.center, 0);
    light.area = 0;
    float noArea = RectEmitterPdfOmega(light, 0, light.center, 1);
    Result[4] = float4(sheared.pdfOmega, noSelection, noArea, RectPowerWeight(1, -1));
}
