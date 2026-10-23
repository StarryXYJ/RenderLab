#pragma once

#include "rendergraph/graph.hpp"
#include "samples/detail/demo_geometry.hpp"

namespace renderlab::samples::detail
{
struct NoisePrograms
{
    std::shared_ptr<rhi::BindingLayout> layout;
    std::shared_ptr<rhi::Pipeline> initialize, update;
};
struct RasterPrograms
{
    std::shared_ptr<rhi::BindingLayout> layout;
    std::shared_ptr<rhi::Pipeline> pipeline;
    std::shared_ptr<rhi::Sampler> sampler;
};
struct SceneOutput
{
    rendergraph::TextureHandle color, depth;
};
rendergraph::TextureHandle AddNoiseFeature(rendergraph::Graph& graph, const NoisePrograms& programs,
                                           uint32_t width, uint32_t height, bool update);
SceneOutput AddSceneFeature(rendergraph::Graph& graph, const RasterPrograms& programs,
                            rendergraph::TextureHandle noise, rendergraph::TextureHandle color,
                            rendergraph::TextureHandle depth, const DemoGeometry& geometry);
void AddReadbackFeature(rendergraph::Graph& graph, rendergraph::TextureHandle texture,
                        const std::shared_ptr<rhi::Buffer>& readback, uint32_t rowPitch);
} // namespace renderlab::samples::detail
