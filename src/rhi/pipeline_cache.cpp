#include "rhi/runtime.hpp"

#include "rhi/detail/validation.hpp"

#include <algorithm>
#include <utility>

namespace renderlab::rhi
{
std::shared_ptr<Pipeline> Runtime::CreatePipeline(PipelineDesc desc)
{
    detail::Require(std::this_thread::get_id() == ownerThread_,
                    "Prepare pipelines on runtime owner thread");
    Validate(desc);
    std::erase_if(pipelines_, [](const auto& entry) { return entry.expired(); });
    for (const auto& entry : pipelines_)
    {
        auto pipeline = entry.lock();
        if (!pipeline)
            continue;
        const auto& key = pipeline->desc;
        if (key.layout == desc.layout && key.vertex == desc.vertex && key.pixel == desc.pixel &&
            key.compute == desc.compute && key.color == desc.color && key.depth == desc.depth &&
            key.depthWrite == desc.depthWrite && key.blend == desc.blend && key.cull == desc.cull &&
            key.frontCounterClockwise == desc.frontCounterClockwise &&
            key.vertexStride == desc.vertexStride && key.attributes == desc.attributes)
            return pipeline;
    }
    auto pipeline = CreatePipelineNative(std::move(desc));
    // Eviction drops only the lookup reference; command packets retain native lifetimes.
    if (pipelines_.size() == 256)
        pipelines_.erase(pipelines_.begin());
    pipelines_.push_back(pipeline);
    return pipeline;
}
} // namespace renderlab::rhi
