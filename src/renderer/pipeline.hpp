#pragma once

#include "renderer/feature.hpp"
#include "renderer/frame.hpp"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace renderlab::renderer
{
enum class PipelineType
{
    Custom,
    Forward
};
enum class RenderStage
{
    BeforeOpaque,
    AfterOpaque,
    BeforeTransparent,
    AfterTransparent,
    AfterRendering
};
struct PipelineOptions
{
    bool copyOpaqueColor = false, copyAfterTransparentColor = false;
};
class RenderPipelineBuilder;
/** @brief Created, immutable feature composition. Build only declares a per-frame graph. */
class RenderPipeline final
{
public:
    void Build(rendergraph::Graph& graph, const RenderFrame& frame) const;

private:
    friend class RenderPipelineBuilder;
    explicit RenderPipeline(std::vector<std::shared_ptr<RenderFeature>> features)
        : features_(std::move(features))
    {
    }
    std::vector<std::shared_ptr<RenderFeature>> features_;
};
/** @brief Registration is unordered. Create validates and sorts dependencies without GPU work. */
class RenderPipelineBuilder final
{
public:
    RenderPipelineBuilder() = default;
    RenderPipelineBuilder& AddFeature(std::shared_ptr<RenderFeature> feature);
    RenderPipelineBuilder& AddFeature(std::shared_ptr<RenderFeature> feature, RenderStage stage);
    RenderPipeline Create() const;

private:
    friend RenderPipelineBuilder CreatePipeline(PipelineType, PipelineOptions);
    RenderPipelineBuilder& Append(std::shared_ptr<RenderFeature> feature, uint32_t order);
    struct Entry
    {
        std::shared_ptr<RenderFeature> feature;
        uint32_t order;
    };
    std::vector<Entry> features_;
};
/// Custom is empty; presets only pre-register feature instances and their stages.
RenderPipelineBuilder CreatePipeline(PipelineType type, PipelineOptions options = {});
} // namespace renderlab::renderer
