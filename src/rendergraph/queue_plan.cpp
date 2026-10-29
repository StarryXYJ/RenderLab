#include "rendergraph/detail/graph_impl.hpp"

#include <algorithm>

namespace renderlab::rendergraph
{
void Graph::Impl::ScheduleQueues(bool enabled)
{
    if (!enabled)
        return;
    std::vector<bool> async(passes.size()), depends(passes.size());
    for (auto& scheduled : plan.schedule)
    {
        const auto& pass = passes[scheduled.pass];
        if (!pass.asyncEligible)
            continue;
        detail::Require(pass.kind == PassKind::Compute, "Async eligibility requires Compute");
        async[scheduled.pass] = true;
        scheduled.queue = rhi::QueueType::Compute;
        plan.asyncCompute = true;
        for (const auto& access : pass.accesses)
        {
            const auto& resource = resources[access.resource];
            detail::Require(!resource.isTexture,
                            "Async feeder cannot access textures: " + pass.name);
            if (resource.imported)
                detail::Require(
                    resource.bufferDesc.memory == rhi::Memory::Upload && !access.write &&
                        (access.use == Use::Uniform || access.use == Use::ShaderRead),
                    "Async feeder imports require read-only Upload buffers: " + pass.name);
            else
            {
                detail::Require(resource.bufferDesc.memory == rhi::Memory::Device &&
                                    resource.bufferDesc.storage && access.use == Use::Storage,
                                "Async feeder outputs require transient Storage buffers: " +
                                    pass.name);
                if (std::ranges::find(plan.asyncResources, access.resource) ==
                    plan.asyncResources.end())
                    plan.asyncResources.push_back(access.resource);
            }
        }
    }
    for (const auto& edge : plan.edges)
        detail::Require(!async[edge.after] || async[edge.before],
                        "Async feeder cannot depend on Graphics work: " + passes[edge.after].name);
    for (auto& scheduled : plan.schedule)
    {
        bool dependency = false;
        for (const auto& edge : plan.edges)
            if (edge.after == scheduled.pass)
                dependency |= async[edge.before] || depends[edge.before];
        depends[scheduled.pass] = dependency;
        scheduled.waitCompute = !async[scheduled.pass] && dependency;
    }
}
} // namespace renderlab::rendergraph
