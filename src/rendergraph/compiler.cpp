#include "rendergraph/detail/graph_impl.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <queue>
#include <set>

namespace renderlab::rendergraph
{
using detail::Require;
const Plan& Graph::Compile(CompileOptions options)
{
    const auto started = std::chrono::steady_clock::now();
    auto& g = *impl_;
    g.Building();
    g.profiling = options.profile;
    Require(options.recordingThreads >= 1 && options.recordingThreads <= 16,
            "Recording threads must be in [1, 16]");
    g.recordingThreads = options.recordingThreads;
    Require(g.active == UINT32_MAX, "Seal the active pass with Execute before Compile");
    try
    {
        auto& plan = g.plan;
        plan = {};
        plan.recordingThreads = g.recordingThreads;
        for (auto& pass : g.passes)
        {
            Require(pass.kind != PassKind::Raster || !pass.colors.empty(),
                    "Raster pass needs a color attachment: " + pass.name + " (" + pass.source +
                        ")");
            plan.passes.push_back({pass.name, pass.source, "unused output", pass.kind, false});
            for (const auto& a : pass.accesses)
                plan.passes.back().accesses.push_back(
                    {a.resource, a.input, a.output, a.use, a.read, a.write, a.bufferRange});
        }
        for (auto& r : g.resources)
            plan.resources.push_back(
                {r.Name(), r.imported, UINT32_MAX, 0, detail::EstimatedBytes(r)});
        std::function<void(uint32_t, const std::string&)> retain;
        const auto requireInput =
            [&](uint32_t resource, uint32_t version, const std::string& consumer)
        {
            auto& r = g.resources[resource];
            if (version == 0)
                Require(r.initial.initialized,
                        "Read before initialization: " + r.Name() + " in " + consumer);
            else
                retain(r.versions[version].writer, "consumed by " + consumer);
        };
        retain = [&](uint32_t pass, const std::string& reason)
        {
            if (plan.passes[pass].live)
                return;
            plan.passes[pass].live = true;
            plan.passes[pass].reason = reason;
            for (auto& a : g.passes[pass].accesses)
                if (a.read)
                    requireInput(a.resource, a.input,
                                 g.passes[pass].name + " (" + g.passes[pass].source + ")");
        };
        for (uint32_t p = 0; p < g.passes.size(); ++p)
            if (!options.cull || g.passes[p].sideEffect)
                retain(p, options.cull ? "external side effect" : "culling disabled");
        for (uint32_t r = 0; r < g.resources.size(); ++r)
            if (g.resources[r].exported)
                requireInput(r, static_cast<uint32_t>(g.resources[r].versions.size() - 1),
                             "Export");
        std::set<std::pair<uint32_t, uint32_t>> added;
        const auto edge = [&](uint32_t before, uint32_t after, std::string reason)
        {
            if (before == after || !plan.passes[before].live || !plan.passes[after].live)
                return;
            if (added.emplace(before, after).second)
                plan.edges.push_back({before, after, std::move(reason)});
            else
                for (auto& existing : plan.edges)
                    if (existing.before == before && existing.after == after)
                        existing.reason += "; " + reason;
        };
        for (uint32_t r = 0; r < g.resources.size(); ++r)
        {
            const auto& resource = g.resources[r];
            uint32_t previous = UINT32_MAX;
            for (uint32_t v = 1; v < resource.versions.size(); ++v)
            {
                const auto writer = resource.versions[v].writer;
                if (!plan.passes[writer].live)
                    continue;
                if (previous != UINT32_MAX)
                    edge(previous, writer, "WAW " + resource.Name());
                previous = writer;
            }
            for (uint32_t p = 0; p < g.passes.size(); ++p)
                if (plan.passes[p].live)
                    for (const auto& access : g.passes[p].accesses)
                        if (access.resource == r && access.read)
                        {
                            if (access.input)
                                edge(resource.versions[access.input].writer, p,
                                     "RAW " + resource.Name() + " v" +
                                         std::to_string(access.input));
                            for (uint32_t v = access.input + 1; v < resource.versions.size(); ++v)
                            {
                                const auto writer = resource.versions[v].writer;
                                if (plan.passes[writer].live)
                                {
                                    edge(p, writer,
                                         "WAR " + resource.Name() + " v" +
                                             std::to_string(access.input));
                                    break;
                                }
                            }
                        }
        }
        for (const auto& e : g.order)
            edge(e.before, e.after, e.reason);
        std::vector<uint32_t> degrees(g.passes.size());
        for (auto& e : plan.edges)
            ++degrees[e.after];
        std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<>> ready;
        for (uint32_t p = 0; p < g.passes.size(); ++p)
            if (plan.passes[p].live && !degrees[p])
                ready.push(p);
        while (!ready.empty())
        {
            const auto p = ready.top();
            ready.pop();
            plan.schedule.push_back({p, {}});
            for (auto& e : plan.edges)
                if (e.before == p && --degrees[e.after] == 0)
                    ready.push(e.after);
        }
        std::string cycle;
        for (uint32_t p = 0; p < g.passes.size(); ++p)
            if (plan.passes[p].live && degrees[p])
                cycle += " " + g.passes[p].name + " (" + g.passes[p].source + ")";
        Require(cycle.empty(), "Dependency/order cycle:" + cycle);
        for (uint32_t i = 0; i < plan.schedule.size(); ++i)
            for (auto& a : g.passes[plan.schedule[i].pass].accesses)
            {
                auto& r = plan.resources[a.resource];
                r.first = std::min(r.first, i);
                r.last = i;
            }
        for (uint32_t r = 0; r < g.resources.size(); ++r)
            if (g.resources[r].exported)
            {
                auto& info = plan.resources[r];
                if (info.first == UINT32_MAX)
                    info.first = 0;
                info.last = static_cast<uint32_t>(plan.schedule.size());
            }
        g.Barriers();
        g.phase = detail::Phase::Compiled;
        plan.compileNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
        return plan;
    }
    catch (...)
    {
        Poison();
        throw;
    }
}
void Graph::Impl::Barriers()
{
    std::vector<rhi::State> states;
    std::vector<bool> written(resources.size());
    for (auto& r : resources)
        states.push_back(r.start);
    for (auto& scheduled : plan.schedule)
    {
        scheduled.barriers.clear();
        for (auto& a : passes[scheduled.pass].accesses)
        {
            auto& before = states[a.resource];
            const auto after = StateFor(a.use);
            if (before != after || written[a.resource] || a.write)
                scheduled.barriers.push_back({a.resource, before, after});
            before = after;
            written[a.resource] = a.write;
        }
    }
    plan.finalBarriers.clear();
    for (uint32_t r = 0; r < resources.size(); ++r)
    {
        auto& resource = resources[r];
        if (resource.exported)
        {
            if (states[r] != *resource.exported || written[r])
                plan.finalBarriers.push_back({r, states[r], *resource.exported});
            states[r] = *resource.exported;
        }
        resource.final = states[r];
        plan.resources[r].initial = resource.start;
        plan.resources[r].final = resource.final;
        plan.resources[r].versions = static_cast<uint32_t>(resource.versions.size());
    }
}
} // namespace renderlab::rendergraph
