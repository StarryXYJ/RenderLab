#include "renderer/pipeline.hpp"

#include "renderer/deferred.hpp"
#include "renderer/forward.hpp"
#include "renderer/raytracing.hpp"
#include "rendergraph/graph.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace renderlab::renderer
{
RenderPipelineBuilder& RenderPipelineBuilder::Append(std::shared_ptr<RenderFeature> feature,
                                                     uint32_t order)
{
    if (!feature)
        throw std::invalid_argument("Empty render feature");
    features_.push_back({std::move(feature), order});
    return *this;
}
RenderPipelineBuilder& RenderPipelineBuilder::AddFeature(std::shared_ptr<RenderFeature> feature)
{
    return Append(std::move(feature), 6u);
}
RenderPipelineBuilder& RenderPipelineBuilder::AddFeature(std::shared_ptr<RenderFeature> feature,
                                                         RenderStage stage)
{
    uint32_t order = 0;
    switch (stage)
    {
    case RenderStage::BeforeOpaque:
        order = 0;
        break;
    case RenderStage::AfterOpaque:
        order = 2;
        break;
    case RenderStage::BeforeTransparent:
        order = 4;
        break;
    case RenderStage::AfterTransparent:
        order = 6;
        break;
    case RenderStage::AfterRendering:
        order = 8;
        break;
    default:
        throw std::invalid_argument("Unknown render feature stage");
    }
    return Append(std::move(feature), order);
}
void RenderPipeline::Build(rendergraph::Graph& graph, const RenderFrame& frame) const
{
    std::vector<rendergraph::PassHandle> previous;
    for (const auto& feature : features_)
    {
        const auto first = graph.PassCount();
        feature->Build(graph, frame);
        const auto end = graph.PassCount();
        for (auto i = first; i < end; ++i)
            for (const auto& predecessor : previous)
                graph.After(graph.GetPass(i), predecessor);
        // Keep all prior groups: a culled intermediate feature must not break stage ordering.
        for (auto i = first; i < end; ++i)
            previous.push_back(graph.GetPass(i));
    }
}
RenderPipeline RenderPipelineBuilder::Create() const
{
    const auto count = features_.size();
    std::map<std::string, size_t> names;
    for (size_t i = 0; i < count; ++i)
        if (!names.emplace(features_[i].feature->Name(), i).second)
            throw std::invalid_argument("Duplicate render feature: " +
                                        features_[i].feature->Name());
    std::vector<std::set<size_t>> edges(count);
    std::vector<size_t> degrees(count);
    const auto edge = [&](size_t before, size_t after)
    {
        if (edges[before].insert(after).second)
            ++degrees[after];
    };
    for (size_t i = 0; i < count; ++i)
    {
        for (const auto& dependency : features_[i].feature->Dependencies())
        {
            const auto found = names.find(dependency);
            if (found == names.end())
                throw std::invalid_argument(
                    "Missing render feature dependency: " + features_[i].feature->Name() +
                    " requires " + dependency);
            edge(found->second, i);
        }
        for (size_t j = 0; j < count; ++j)
            if (features_[j].order < features_[i].order)
                edge(j, i);
    }
    using Ready = std::pair<std::string, size_t>;
    std::priority_queue<Ready, std::vector<Ready>, std::greater<>> ready;
    for (size_t i = 0; i < count; ++i)
        if (!degrees[i])
            ready.emplace(features_[i].feature->Name(), i);
    std::vector<std::shared_ptr<RenderFeature>> sorted;
    while (!ready.empty())
    {
        const auto index = ready.top().second;
        ready.pop();
        sorted.push_back(features_[index].feature);
        for (const auto after : edges[index])
            if (--degrees[after] == 0)
                ready.emplace(features_[after].feature->Name(), after);
    }
    if (sorted.size() != count)
    {
        std::string remaining;
        for (size_t i = 0; i < count; ++i)
            if (degrees[i])
                remaining += " " + features_[i].feature->Name();
        throw std::invalid_argument("Render feature dependency/stage cycle:" + remaining);
    }
    return RenderPipeline(std::move(sorted));
}
RenderPipelineBuilder CreatePipeline(PipelineType type, PipelineOptions options)
{
    if (type != PipelineType::Custom && type != PipelineType::Forward &&
        type != PipelineType::Deferred && type != PipelineType::RayTracing)
        throw std::invalid_argument("Unknown or unimplemented pipeline type");
    if (type == PipelineType::Custom &&
        (options.copyOpaqueColor || options.copyAfterTransparentColor))
        throw std::invalid_argument("Custom pipeline requires explicit snapshot features");
    RenderPipelineBuilder pipeline;
    if (type == PipelineType::Forward)
    {
        pipeline.Append(std::make_shared<OpaqueFeature>(), 1);
        if (options.copyOpaqueColor)
            pipeline.Append(
                std::make_shared<ColorSnapshotFeature>("CameraColor", "OpaqueColor",
                                                       std::vector<std::string>{"Forward.Opaque"}),
                3);
        pipeline.Append(std::make_shared<TransparentFeature>(), 5);
        if (options.copyAfterTransparentColor)
            pipeline.Append(std::make_shared<ColorSnapshotFeature>(
                                "CameraColor", "AfterTransparentColor",
                                std::vector<std::string>{"Forward.Transparent"}),
                            7);
    }
    else if (type == PipelineType::Deferred || type == PipelineType::RayTracing)
    {
        pipeline.Append(std::make_shared<GBufferFeature>(), 1);
        if (type == PipelineType::RayTracing)
            pipeline.Append(std::make_shared<RayQueryShadowFeature>(), 2);
        pipeline.Append(std::make_shared<DeferredLightingFeature>(), 3);
        if (type == PipelineType::RayTracing)
            pipeline.Append(std::make_shared<RayReflectionFeature>(), 4);
        pipeline.Append(std::make_shared<TransparentFeature>(type == PipelineType::RayTracing
                                                                 ? "RayTracing.Reflections"
                                                                 : "Deferred.Lighting"),
                        5);
    }
    return pipeline;
}
} // namespace renderlab::renderer
