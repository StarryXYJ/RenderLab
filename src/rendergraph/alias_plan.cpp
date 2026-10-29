#include "rendergraph/detail/graph_impl.hpp"

#include <algorithm>

namespace renderlab::rendergraph
{
void Graph::Impl::PlanAliases(bool enabled)
{
    plan.memoryAlias = enabled;
    if (!enabled)
        return;
    std::vector<uint32_t> candidates;
    for (uint32_t i = 0; i < resources.size(); ++i)
    {
        const auto& resource = resources[i];
        const auto& lifetime = plan.resources[i];
        if (!resource.isTexture || resource.imported || resource.exported ||
            lifetime.first == UINT32_MAX)
            continue;
        // Positions are meaningful here only because every access runs on the same ordered queue.
        bool graphicsOnly = true;
        for (const auto& scheduled : plan.schedule)
            for (const auto& access : passes[scheduled.pass].accesses)
                if (access.resource == i && scheduled.queue != rhi::QueueType::Graphics)
                    graphicsOnly = false;
        if (!graphicsOnly)
            continue;
        const auto& first = passes[plan.schedule[lifetime.first].pass];
        bool clear = false;
        for (const auto& color : first.colors)
            if (color.resource == i)
                clear = color.load == rhi::LoadOp::Clear;
        if (first.depth && first.depth->resource == i)
            clear = first.depth->load == rhi::LoadOp::Clear && !first.depthReadOnly &&
                    (!rhi::HasStencil(resource.textureDesc.format) ||
                     (first.stencilLoad == rhi::LoadOp::Clear && !first.stencilReadOnly));
        if (first.kind == PassKind::Raster && clear)
            candidates.push_back(i);
    }
    std::ranges::stable_sort(candidates, [&](auto a, auto b)
                             { return plan.resources[a].first < plan.resources[b].first; });
    std::vector<std::vector<uint32_t>> groups;
    for (const auto candidate : candidates)
    {
        const auto& desc = resources[candidate].textureDesc;
        const auto compatible = [&](const auto& group)
        {
            const auto last = group.back();
            const auto& previous = resources[last].textureDesc;
            return group.size() < 16 &&
                   plan.resources[last].last < plan.resources[candidate].first &&
                   previous.format == desc.format && previous.storage == desc.storage &&
                   previous.attachment == desc.attachment;
        };
        const auto found = std::ranges::find_if(groups, compatible);
        if (found != groups.end())
            found->push_back(candidate);
        else
            groups.push_back({candidate});
    }
    for (auto& group : groups)
        if (group.size() > 1)
        {
            const auto index = static_cast<uint32_t>(plan.aliasGroups.size());
            for (const auto resource : group)
                plan.resources[resource].aliasGroup = index;
            plan.aliasGroups.push_back(std::move(group));
        }
}
} // namespace renderlab::rendergraph
