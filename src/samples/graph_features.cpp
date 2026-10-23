#include "samples/detail/graph_features.hpp"

namespace renderlab::samples::detail
{
using namespace rendergraph;
namespace
{
struct NoiseData
{
    TextureHandle output;
    std::shared_ptr<rhi::BindingLayout> layout;
    std::shared_ptr<rhi::Pipeline> pipeline;
    std::shared_ptr<rhi::BindingGroup> bindings;
    uint32_t x = 0, y = 0;
};
TextureHandle AddNoisePass(Graph& graph, const NoisePrograms& programs, TextureHandle output,
                           uint32_t width, uint32_t height, bool update)
{
    graph.AddPass<NoiseData>(
        update ? "Noise.Update" : "Noise.Initialize", PassKind::Compute,
        [&](PassBuilder& b, NoiseData& d)
        {
            output = b.Write(output, Use::Storage, update ? Coverage::Preserve : Coverage::Full);
            d.output = output;
            d.layout = programs.layout;
            d.pipeline = update ? programs.update : programs.initialize;
            d.x = (width + 7) / 8;
            d.y = (height + 7) / 8;
        },
        [](const NoiseData& d, ExecuteContext& c)
        {
            c.Bind(d.pipeline);
            c.Bind(d.bindings);
            c.Dispatch(d.x, d.y);
        },
        [](NoiseData& d, PrepareContext& c)
        { d.bindings = c.Device().CreateGroup(d.layout, {{0, c.View(d.output)}}); });
    return output;
}
struct DrawData
{
    TextureHandle noise;
    BufferHandle vertices, indices, indirect;
    RasterPrograms programs;
    std::shared_ptr<rhi::BindingGroup> bindings;
    bool overlay = false;
};
struct CopyData
{
    TextureHandle texture;
    BufferHandle buffer;
    uint32_t pitch = 0;
};
} // namespace
TextureHandle AddNoiseFeature(Graph& graph, const NoisePrograms& programs, uint32_t width,
                              uint32_t height, bool update)
{
    auto output = graph.Create(
        rhi::TextureDesc{width, height, rhi::Format::Rgba8, true, false, "Feature.Noise"});
    output = AddNoisePass(graph, programs, output, width, height, false);
    if (update)
        output = AddNoisePass(graph, programs, output, width, height, true);
    return output;
}
SceneOutput AddSceneFeature(Graph& graph, const RasterPrograms& programs, TextureHandle noise,
                            TextureHandle color, TextureHandle depth, const DemoGeometry& geometry)
{
    const auto vertices = graph.Import(geometry.vertices, {rhi::State::Vertex, true, 0});
    const auto indices = graph.Import(geometry.indices, {rhi::State::Index, true, 0});
    const auto indirect = graph.Import(geometry.indirect, {rhi::State::Indirect, true, 0});
    // Two raster passes explicitly exercise Clear followed by Load on color and reversed-Z depth.
    for (const bool overlay : {false, true})
        graph.AddPass<DrawData>(
            overlay ? "Scene.Overlay" : "Scene.Opaque", PassKind::Raster,
            [&](PassBuilder& b, DrawData& d)
            {
                color = b.Color(color, overlay ? rhi::LoadOp::Load : rhi::LoadOp::Clear);
                depth = b.Depth(depth, overlay ? rhi::LoadOp::Load : rhi::LoadOp::Clear);
                b.Read(noise, Use::Sampled);
                b.Read(vertices, Use::Vertex);
                if (!overlay)
                {
                    b.Read(indices, Use::Index);
                    b.Read(indirect, Use::Indirect);
                }
                d.noise = noise;
                d.vertices = vertices;
                d.indices = indices;
                d.indirect = indirect;
                d.programs = programs;
                d.overlay = overlay;
            },
            [](const DrawData& d, ExecuteContext& c)
            {
                c.Bind(d.programs.pipeline);
                c.Bind(d.bindings);
                if (d.overlay)
                {
                    c.Geometry(d.vertices);
                    c.Draw(6, 12);
                }
                else
                {
                    c.Geometry(d.vertices, d.indices);
                    c.DrawIndexed(3);
                    c.Draw(6, 3);
                    c.DrawIndirect(d.indirect);
                }
            },
            [](DrawData& d, PrepareContext& c)
            {
                d.bindings = c.Device().CreateGroup(
                    d.programs.layout, {{1, c.View(d.noise)}, {2, {}, d.programs.sampler}});
            });
    return {color, depth};
}
void AddReadbackFeature(Graph& graph, TextureHandle texture,
                        const std::shared_ptr<rhi::Buffer>& readback, uint32_t rowPitch)
{
    auto buffer = graph.Import(readback, {rhi::State::CopyDest, false, 0});
    graph.AddPass<CopyData>(
        "Readback", PassKind::Copy,
        [&](PassBuilder& b, CopyData& d)
        {
            b.Read(texture, Use::CopySource);
            buffer = b.Write(buffer, Use::CopyDestination);
            d.texture = texture;
            d.buffer = buffer;
            d.pitch = rowPitch;
        },
        [](const CopyData& d, ExecuteContext& c)
        { c.CopyTexture(d.buffer, d.texture, d.pitch, false); });
    graph.Export(buffer, rhi::State::HostRead);
}
} // namespace renderlab::samples::detail
