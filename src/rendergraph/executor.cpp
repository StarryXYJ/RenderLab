#include "rendergraph/detail/graph_impl.hpp"
#include <chrono>

namespace renderlab::rendergraph
{
using detail::Require;
void Graph::Prepare(Pool& pool)
{
    const auto started = std::chrono::steady_clock::now();
    auto& g = *impl_;
    Require(g.phase == detail::Phase::Compiled, "Prepare requires a compiled graph");
    g.pool = &pool;
    g.phase = detail::Phase::Preparing;
    try
    {
        for (uint32_t i = 0; i < g.resources.size(); ++i)
        {
            auto& r = g.resources[i];
            if (g.plan.resources[i].first == UINT32_MAX)
                continue;
            if (r.imported)
            {
                const auto owner = r.isTexture ? r.texture->owner : r.buffer->owner;
                Require(owner == &pool.Device(),
                        "Imported object belongs to another runtime: " + r.Name());
                Require(r.initial.completion <= pool.Device().LastSubmitted(),
                        "Import completion was not submitted to this queue");
            }
            else
            {
                r.lease = r.isTexture ? pool.Acquire(r.textureDesc) : pool.Acquire(r.bufferDesc);
                r.texture = r.lease->texture;
                r.buffer = r.lease->buffer;
                r.start = r.lease->state;
            }
            if (r.isTexture)
                r.view = pool.Device().CreateView(r.texture);
        }
        g.Barriers();
        for (auto& scheduled : g.plan.schedule)
        {
            g.active = scheduled.pass;
            PrepareContext context(*this, g.active);
            g.passes[g.active].callbacks->Prepare(context);
        }
        g.active = UINT32_MAX;
        g.phase = detail::Phase::Prepared;
        g.plan.prepareNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::steady_clock::now() - started)
                               .count();
    }
    catch (...)
    {
        Poison();
        throw;
    }
}
void Graph::Record()
{
    const auto started = std::chrono::steady_clock::now();
    auto& g = *impl_;
    Require(g.phase == detail::Phase::Prepared, "Record requires prepared graph");
    g.commands = g.pool->Device().CreateCommands();
    g.phase = detail::Phase::Recording;
    const auto barrier = [&](const Barrier& b)
    {
        const auto& r = g.resources[b.resource];
        if (r.isTexture)
            g.commands->Barrier(r.texture, b.before, b.after);
        else
            g.commands->Barrier(r.buffer, b.before, b.after);
    };
    try
    {
        for (const auto& scheduled : g.plan.schedule)
        {
            for (const auto& b : scheduled.barriers)
                barrier(b);
            auto& pass = g.passes[scheduled.pass];
            g.commands->BeginMarker(pass.name);
            if (pass.color)
            {
                auto& color = g.resources[pass.color->resource];
                g.commands->Begin(color.view,
                                  pass.depth ? g.resources[pass.depth->resource].view : nullptr,
                                  pass.color->clear, pass.color->load,
                                  pass.depth ? pass.depth->load : rhi::LoadOp::Clear);
            }
            g.active = scheduled.pass;
            ExecuteContext context(*this, g.active, *g.commands);
            pass.callbacks->Execute(context);
            if (pass.color)
                g.commands->End();
            g.commands->EndMarker();
        }
        for (const auto& b : g.plan.finalBarriers)
            barrier(b);
        g.active = UINT32_MAX;
        g.phase = detail::Phase::Recorded;
        g.plan.recordNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                              std::chrono::steady_clock::now() - started)
                              .count();
    }
    catch (...)
    {
        g.commands.reset();
        Poison();
        throw;
    }
}
Submission Graph::Submit(rhi::Swapchain* presentation)
{
    auto& g = *impl_;
    if (g.phase == detail::Phase::Prepared)
        Record();
    Require(g.phase == detail::Phase::Recorded, "Graph submits once, after preparation/recording");
    // Allocate the result before native submission; publication below cannot lose completion
    // ownership.
    Submission result;
    for (uint32_t i = 0; i < g.resources.size(); ++i)
        if (g.resources[i].exported)
        {
            auto& r = g.resources[i];
            result.exports.push_back({i, r.texture, r.buffer, r.final});
        }
    try
    {
        result.completion = g.pool->Device().Submit(std::move(g.commands), presentation);
    }
    catch (...)
    {
        Poison();
        throw;
    }
    for (uint32_t i = 0; i < g.resources.size(); ++i)
        if (g.plan.resources[i].first != UINT32_MAX && g.resources[i].lease)
        {
            auto& r = g.resources[i];
            r.lease->state = r.final;
            r.lease->completion = result.completion;
        }
    g.phase = detail::Phase::Submitted;
    return result;
}
} // namespace renderlab::rendergraph
