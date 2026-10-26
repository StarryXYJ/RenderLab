#include "renderer/raytracing.hpp"

#include "renderer/frame.hpp"
#include "rendergraph/graph.hpp"

#include <stdexcept>

namespace renderlab::renderer
{
void RayQueryShadowFeature::Build(rendergraph::Graph& graph, const RenderFrame& frame)
{
    using namespace rendergraph;
    const auto& input = frame.rayTracing;
    if (!input.scene || !input.shadowPipeline)
        throw std::invalid_argument(
            "Ray Tracing requires a prepared scene AS and Ray Query program");
    const auto camera = graph.GetTextureDesc(graph.GetTexture("CameraColor"));
    graph.CreateTexture("RayTracing.ShadowVisibility",
                        {camera.width, camera.height, rhi::Format::Rgba8, true, false});
    auto pass = graph.AddComputePass("RayTracing.Shadows");
    const auto scene = pass.ReadBuffer(input.storage, Use::AccelerationStructureRead);
    const auto depth = pass.ReadTexture("CameraDepth");
    const auto normal = pass.ReadTexture("GBuffer.NormalRoughness");
    const auto parameters =
        pass.ReadBuffer(input.parameters.buffer, Use::Uniform, input.parameters.range);
    const auto output = pass.WriteTexture("RayTracing.ShadowVisibility");
    pass.Execute(
        [=](ComputeContext& context)
        {
            context.Bind(input.shadowPipeline);
            context.BindAccelerationStructure(0, input.scene, scene);
            context.BindTexture(1, depth);
            context.BindTexture(2, normal);
            context.BindBuffer(3, parameters, input.parameters.range.offset,
                               input.parameters.range.size);
            context.BindTexture(4, output);
            context.Dispatch((camera.width + 7) / 8, (camera.height + 7) / 8);
        });
}
} // namespace renderlab::renderer
