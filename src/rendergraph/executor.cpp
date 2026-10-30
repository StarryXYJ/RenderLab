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
        if (g.plan.asyncCompute)
        {
            Require(pool.Device().SupportsAsyncCompute(), "Async compute queue is unavailable");
            pool.Device().EnableAsyncCompute();
        }
        if (g.profiling && !g.plan.schedule.empty())
        {
            Require(g.plan.schedule.size() <= rhi::MaxTimestampSlots / 2,
                    "GPU profiling supports at most 32 live passes");
            g.timestampLease = pool.Acquire(rhi::BufferDesc{
                rhi::MaxTimestampSlots * 8, rhi::Memory::Readback, "Graph.Timestamps"});
            g.timestamps = g.timestampLease->buffer;
        }
        for (const auto& members : g.plan.aliasGroups)
        {
            std::vector<rhi::TextureDesc> descs;
            for (const auto member : members)
                descs.push_back(g.resources[member].textureDesc);
            auto group = pool.AcquireAliases(descs);
            g.plan.aliasBackingBytes += group->bytes;
            for (size_t i = 0; i < members.size(); ++i)
                g.resources[members[i]].lease = group->members[i];
            g.aliasLeases.push_back(std::move(group));
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
                if (!r.lease)
                    r.lease =
                        r.isTexture ? pool.Acquire(r.textureDesc) : pool.Acquire(r.bufferDesc);
                r.texture = r.lease->texture;
                r.buffer = r.lease->buffer;
                r.start = r.lease->state;
            }
            if (r.isTexture)
            {
                r.view = pool.Device().CreateView(r.texture);
                if (r.lease)
                    r.lease->view = r.view;
            }
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
    g.commands->SetRecordingThreads(g.recordingThreads);
    g.phase = detail::Phase::Recording;
    uint32_t position = 0;
    std::vector<std::shared_ptr<rhi::Texture>> activeAliases;
    for (const auto& group : g.aliasLeases)
        activeAliases.push_back(
            group->active == UINT32_MAX ? nullptr : group->members[group->active]->texture);
    const auto barrier = [&](const Barrier& b)
    {
        const auto& r = g.resources[b.resource];
        const auto& info = g.plan.resources[b.resource];
        if (info.aliasGroup != UINT32_MAX && info.first == position)
        {
            auto& previous = activeAliases[info.aliasGroup];
            g.commands->ActivateAlias(previous, r.texture, b.before, b.after);
            previous = r.texture;
        }
        else if (r.isTexture)
            g.commands->Barrier(r.texture, b.before, b.after);
        else
            g.commands->Barrier(r.buffer, b.before, b.after);
    };
    try
    {
        if (g.plan.asyncCompute)
        {
            g.commands->BeginMarker("Graph.AsyncPrologue");
            for (const auto& b : g.plan.prologueBarriers)
                barrier(b);
            g.commands->EndMarker();
            g.commands->RecordingBoundary();
        }
        uint32_t timestamp = 0;
        for (const auto& scheduled : g.plan.schedule)
        {
            g.plan.passes[scheduled.pass].firstCommand = g.commands->GetCommands().size();
            g.commands->SelectQueue(scheduled.queue, scheduled.waitCompute);
            for (const auto& b : scheduled.barriers)
                barrier(b);
            auto& pass = g.passes[scheduled.pass];
            g.commands->BeginMarker(pass.name);
            if (g.timestamps)
                g.commands->WriteTimestamp(timestamp++);
            if (!pass.colors.empty())
            {
                std::vector<std::shared_ptr<rhi::TextureView>> colors;
                colors.reserve(pass.colors.size());
                for (const auto& attachment : pass.colors)
                    colors.push_back(g.resources[attachment.resource].view);
                const auto& color = pass.colors.front();
                g.commands->Begin(std::move(colors),
                                  pass.depth ? g.resources[pass.depth->resource].view : nullptr,
                                  color.clear, color.load,
                                  pass.depth ? pass.depth->load : rhi::LoadOp::Clear,
                                  pass.stencilLoad, pass.clearStencil);
            }
            g.active = scheduled.pass;
            if (pass.kind == PassKind::Raster)
            {
                RasterContext context(*this, g.active, *g.commands);
                std::get<std::function<void(RasterContext&)>>(pass.callback)(context);
            }
            else if (pass.kind == PassKind::RayTracing)
            {
                RayTracingContext context(*this, g.active, *g.commands);
                std::get<std::function<void(RayTracingContext&)>>(pass.callback)(context);
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
            if (!pass.colors.empty())
                g.commands->End();
            if (g.timestamps)
                g.commands->WriteTimestamp(timestamp++);
            g.commands->EndMarker();
            g.commands->RecordingBoundary();
            g.plan.passes[scheduled.pass].lastCommand = g.commands->GetCommands().size();
            ++position;
        }
        g.commands->SelectQueue(rhi::QueueType::Graphics, g.plan.asyncCompute);
        for (const auto& b : g.plan.finalBarriers)
            barrier(b);
        if (g.timestamps)
        {
            g.commands->Barrier(g.timestamps, g.timestampLease->state, rhi::State::CopyDest);
            g.commands->ReadTimestamps(g.timestamps, timestamp);
            g.commands->Barrier(g.timestamps, rhi::State::CopyDest, rhi::State::HostRead);
        }
        g.active = UINT32_MAX;
        g.phase = detail::Phase::Recorded;
        g.plan.recordingChunks = static_cast<uint32_t>(g.commands->RecordingRanges().size());
        const auto ranges = g.commands->RecordingRanges();
        for (auto& pass : g.plan.passes)
            if (pass.live)
            {
                pass.commands = CountCommands(
                    std::span(g.commands->GetCommands())
                        .subspan(static_cast<size_t>(pass.firstCommand),
                                 static_cast<size_t>(pass.lastCommand - pass.firstCommand)));
                for (uint32_t index = 0; index < ranges.size(); ++index)
                    if (pass.firstCommand >= ranges[index].first &&
                        pass.lastCommand <= ranges[index].last)
                        pass.recordingChunk = index;
            }
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
        const auto graphicsClock = g.pool->Device().TimestampClock(rhi::QueueType::Graphics);
        const auto computeClock = g.plan.asyncCompute
                                      ? g.pool->Device().TimestampClock(rhi::QueueType::Compute)
                                      : graphicsClock;
        for (const auto& scheduled : g.plan.schedule)
        {
            result.profile.names.push_back(g.passes[scheduled.pass].name);
            result.profile.clocks.push_back(
                scheduled.queue == rhi::QueueType::Compute ? computeClock : graphicsClock);
        }
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
    for (auto& group : g.aliasLeases)
        group->active = static_cast<uint32_t>(group->members.size() - 1);
    if (g.timestampLease)
    {
        g.timestampLease->state = rhi::State::HostRead;
        g.timestampLease->completion = result.completion;
    }
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
    Require(clocks.empty() || clocks.size() == names.size(), "GPU profile clock metadata mismatch");
    std::vector<Timing> timings;
    uint64_t referenceTick = 0;
    if (!names.empty())
        std::memcpy(&referenceTick, bytes.data(), 8);
    for (size_t i = 0; i < names.size(); ++i)
    {
        uint64_t begin, end;
        std::memcpy(&begin, bytes.data() + i * 16, 8);
        std::memcpy(&end, bytes.data() + i * 16 + 8, 8);
        const auto clock = clocks.empty() ? rhi::QueueClock{0, 0, periodNs} : clocks[i];
        const auto position = [&](uint64_t tick)
        {
            // Subtract shared device ticks before conversion to preserve small intervals.
            const auto origin =
                clock.gpuTick == 0 && clock.originNs == 0 ? referenceTick : clock.gpuTick;
            const double difference =
                tick >= origin ? double(tick - origin) : -double(origin - tick);
            return clock.originNs + difference * clock.periodNs;
        };
        Require(end >= begin, "GPU timestamp pair is reversed");
        timings.push_back({names[i], double(end - begin) * clock.periodNs, position(begin),
                           position(end), clock.queue});
    }
    return timings;
}
} // namespace renderlab::rendergraph
