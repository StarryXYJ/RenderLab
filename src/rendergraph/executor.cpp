#include "rendergraph/detail/graph_impl.hpp"
#include <chrono>
#include <cstring>

namespace renderlab::rendergraph
{
using detail::Require;
void Graph::Prepare(Pool& pool)
{
    const auto started = std::chrono::steady_clock::now();
    auto& g = *impl_;
    Require(g.phase == detail::Phase::Compiled, "Prepare requires a compiled graph");
    pool.Device();
    g.pool = &pool;
    g.phase = detail::Phase::Preparing;
    try
    {
        if (g.profiling && !g.plan.schedule.empty())
        {
            Require(g.plan.schedule.size() <= rhi::MaxTimestampSlots / 2,
                    "GPU profiling supports at most 32 live passes");
            g.timestamps = pool.Device().CreateBuffer(
                {g.plan.schedule.size() * 16, rhi::Memory::Readback, "Graph.Timestamps"});
        }
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
        uint32_t timestamp = 0;
        for (const auto& scheduled : g.plan.schedule)
        {
            for (const auto& b : scheduled.barriers)
                barrier(b);
            auto& pass = g.passes[scheduled.pass];
            g.commands->BeginMarker(pass.name);
            if (g.timestamps)
                g.commands->WriteTimestamp(timestamp++);
            if (pass.color)
            {
                auto& color = g.resources[pass.color->resource];
                g.commands->Begin(color.view,
                                  pass.depth ? g.resources[pass.depth->resource].view : nullptr,
                                  pass.color->clear, pass.color->load,
                                  pass.depth ? pass.depth->load : rhi::LoadOp::Clear,
                                  pass.stencilLoad, pass.clearStencil);
            }
            g.active = scheduled.pass;
            if (pass.kind == PassKind::Raster)
            {
                RasterContext context(*this, g.active, *g.commands);
                std::get<std::function<void(RasterContext&)>>(pass.callback)(context);
            }
            else if (pass.kind == PassKind::Compute)
            {
                ComputeContext context(*this, g.active, *g.commands);
                std::get<std::function<void(ComputeContext&)>>(pass.callback)(context);
            }
            else
            {
                CopyContext context(*this, g.active, *g.commands);
                std::get<std::function<void(CopyContext&)>>(pass.callback)(context);
            }
            if (pass.color)
                g.commands->End();
            if (g.timestamps)
                g.commands->WriteTimestamp(timestamp++);
            g.commands->EndMarker();
        }
        for (const auto& b : g.plan.finalBarriers)
            barrier(b);
        if (g.timestamps)
        {
            g.commands->ReadTimestamps(g.timestamps, timestamp);
            g.commands->Barrier(g.timestamps, rhi::State::CopyDest, rhi::State::HostRead);
        }
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
    Require(g.phase == detail::Phase::Recorded, "Graph submits once, after preparation/recording");
    // Allocate the result before native submission; publication below cannot lose completion
    // ownership.
    Submission result;
    if (g.timestamps)
    {
        result.profile.ticks = g.timestamps;
        result.profile.periodNs = g.pool->Device().TimestampPeriodNs();
        for (const auto& scheduled : g.plan.schedule)
            result.profile.names.push_back(g.passes[scheduled.pass].name);
    }
    for (uint32_t i = 0; i < g.resources.size(); ++i)
        if (g.resources[i].exported)
        {
            auto& r = g.resources[i];
            result.exports.push_back({i, r.texture, r.buffer, r.final});
        }
    try
    {
        result.completion = g.pool->Device().Submit(std::move(g.commands), presentation);
        result.profile.completion = result.completion;
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
Submission Graph::Execute(Pool& pool, rhi::Swapchain* presentation)
{
    Require(impl_->phase == detail::Phase::Compiled, "Graph executes once, after Compile");
    try
    {
        Prepare(pool);
        Record();
        return Submit(presentation);
    }
    catch (...)
    {
        Poison();
        throw;
    }
}
std::vector<Submission::GpuProfile::Timing>
Submission::GpuProfile::Read(const rhi::Runtime& runtime) const
{
    if (!ticks)
        return {};
    Require(ticks->owner == &runtime && completion && runtime.Completed() >= completion,
            "GPU profile requires its owning runtime and a completed submission");
    const auto bytes = ticks->Map();
    Require(names.size() <= rhi::MaxTimestampSlots / 2 && bytes.size() >= names.size() * 16,
            "GPU profile readback is truncated or exceeds capacity");
    std::vector<Timing> timings;
    for (size_t i = 0; i < names.size(); ++i)
    {
        uint64_t begin, end;
        std::memcpy(&begin, bytes.data() + i * 16, 8);
        std::memcpy(&end, bytes.data() + i * 16 + 8, 8);
        timings.push_back({names[i], double(end - begin) * periodNs});
    }
    return timings;
}
} // namespace renderlab::rendergraph
