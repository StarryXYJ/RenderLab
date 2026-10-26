#include "samples/graph/graph_features.hpp"
#include "rendergraph/graph.hpp"

namespace renderlab::samples::detail
{
using namespace rendergraph;
void NoiseFeature::Build(Graph& graph, const renderer::RenderFrame&)
{
    const auto& name = output_;
    const auto width = width_, height = height_;
    const auto& programs = programs_;
    graph.CreateTexture(name, {width, height, rhi::Format::Rgba8, true, false});
    auto initialize = graph.AddComputePass(name + ".Initialize");
    auto output = initialize.WriteTexture(name);
    auto pipeline = programs.initialize;
    const auto x = (width + 7) / 8, y = (height + 7) / 8;
    initialize.Execute(
        [output, pipeline, x, y](ComputeContext& context)
        {
            context.Bind(pipeline);
            context.BindTexture(0, output);
            context.Dispatch(x, y);
        });
    if (update_)
    {
        auto pass = graph.AddComputePass(name + ".Update");
        output = pass.ReadWriteTexture(name);
        pipeline = programs.update;
        pass.Execute(
            [output, pipeline, x, y](ComputeContext& context)
            {
                context.Bind(pipeline);
                context.BindTexture(0, output);
                context.Dispatch(x, y);
            });
    }
}
void DemoSceneFeature::Build(Graph& graph, const renderer::RenderFrame&)
{
    const auto programs = programs_;
    const auto& geometry = geometry_;
    const auto vertices =
        graph.ImportBuffer("Demo.Vertices", geometry.vertices, {rhi::State::Vertex, true, 0});
    const auto indices =
        graph.ImportBuffer("Demo.Indices", geometry.indices, {rhi::State::Index, true, 0});
    const auto indirect =
        graph.ImportBuffer("Demo.Indirect", geometry.indirect, {rhi::State::Indirect, true, 0});
    for (const bool overlay : {false, true})
    {
        auto pass = graph.AddRasterPass(overlay ? "Scene.Overlay" : "Scene.Opaque");
        pass.ColorTarget(color_, overlay ? rhi::LoadOp::Load : rhi::LoadOp::Clear);
        pass.DepthTarget(depth_, overlay ? rhi::LoadOp::Load : rhi::LoadOp::Clear);
        const auto noise = pass.ReadTexture(noise_);
        pass.ReadBuffer(vertices, Use::Vertex);
        if (!overlay)
        {
            pass.ReadBuffer(indices, Use::Index);
            pass.ReadBuffer(indirect, Use::Indirect);
        }
        pass.Execute(
            [programs, noise, vertices, indices, indirect, overlay](RasterContext& context)
            {
                context.Bind(programs.pipeline);
                context.BindTexture(1, noise);
                context.BindSampler(2, programs.sampler);
                if (overlay)
                {
                    context.Geometry(vertices);
                    context.Draw(6, 12);
                }
                else
                {
                    context.Geometry(vertices, indices);
                    context.DrawIndexed(3);
                    context.Draw(6, 3);
                    context.DrawIndirect(indirect);
                }
            });
    }
}
void ReadbackFeature::Build(Graph& graph, const renderer::RenderFrame&)
{
    const auto& readback = destination_;
    const auto rowPitch = rowPitch_;
    auto buffer =
        graph.ImportBuffer(readback->desc.name, readback, {rhi::State::CopyDest, false, 0});
    auto pass = graph.AddCopyPass("Readback." + source_);
    const auto texture = pass.ReadTexture(source_);
    buffer = pass.WriteBuffer(buffer);
    pass.Execute([texture, buffer, rowPitch](CopyContext& context)
                 { context.CopyTexture(buffer, texture, rowPitch, false); });
    graph.Export(buffer, rhi::State::HostRead);
}
} // namespace renderlab::samples::detail
