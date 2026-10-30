#include "renderer/forward.hpp"
#include "renderer/pipeline.hpp"
#include "rendergraph/graph.hpp"
#include "rendergraph/pool.hpp"
#include "rhi/commands.hpp"
#include <functional>

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
using namespace renderlab;
using namespace rendergraph;
void Require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void Fails(F function, const std::string& text)
{
    try
    {
        function();
    }
    catch (const std::exception& e)
    {
        Require(std::string(e.what()).find(text) != std::string::npos, e.what());
        return;
    }
    throw std::runtime_error("Expected diagnostic: " + text);
}
template <class T>
concept Dispatchable = requires(T& c) { c.Dispatch(1, 1, 1); };
template <class T>
concept Drawable = requires(T& c) { c.Draw(3); };
static_assert(Dispatchable<ComputeContext> && !Dispatchable<RasterContext> &&
              !Dispatchable<CopyContext>);
static_assert(!Drawable<RayTracingContext> && !Dispatchable<RayTracingContext>);
static_assert(Drawable<RasterContext> && !Drawable<ComputeContext> && !Drawable<CopyContext>);
TextureHandle Texture(Graph& graph, const std::string& name = "Test.Texture")
{
    return graph.CreateTexture(name, {8, 8, rhi::Format::Rgba8, true, true});
}
PassHandle Write(Graph& graph, TextureHandle& texture, Coverage coverage = Coverage::Full)
{
    auto pass = graph.AddComputePass("Write");
    texture =
        coverage == Coverage::Full ? pass.WriteTexture(texture) : pass.ReadWriteTexture(texture);
    return pass.Execute([](ComputeContext&) {});
}
PassHandle Read(Graph& graph, TextureHandle texture, bool root = true)
{
    auto pass = graph.AddComputePass("Read");
    pass.ReadTexture(texture);
    if (root)
        pass.SideEffect();
    return pass.Execute([](ComputeContext&) {});
}
struct FakeBuffer : rhi::Buffer
{
    using Buffer::Buffer;
    std::vector<std::byte> bytes;
    std::span<std::byte> Map() override
    {
        bytes.resize(static_cast<size_t>(desc.size));
        return bytes;
    }
};
struct FakeRuntime : rhi::Runtime
{
    std::vector<rhi::State> aliasBefore;
    std::vector<const void*> aliasPrevious;
    bool asyncAvailable = true, failBefore = false, failAfter = false, failDrain = false;
    uint32_t drains = 0;
    std::vector<rhi::RecordingRange> nativeRanges;
    bool SupportsAsyncCompute() const override
    {
        return asyncAvailable;
    }
    void EnableAsyncCompute() override
    {
        if (!asyncAvailable)
            rhi::Runtime::EnableAsyncCompute();
    }
    uint64_t completed = 0;
    std::vector<uint32_t> drawnVertices;
    struct Work : Submission
    {
    };
    std::shared_ptr<rhi::Buffer> CreateBuffer(const rhi::BufferDesc& d) override
    {
        return std::make_shared<FakeBuffer>(this, d);
    }
    std::shared_ptr<rhi::Texture> CreateTexture(const rhi::TextureDesc& d) override
    {
        return std::make_shared<rhi::Texture>(this, d);
    }
    uint64_t QueryAliasedTextureBytes(std::span<const rhi::TextureDesc> descs) override
    {
        ValidateAliasDescs(descs);
        uint64_t bytes = 0;
        for (const auto& desc : descs)
            bytes =
                std::max(bytes, uint64_t(desc.width) * desc.height * rhi::FormatBytes(desc.format));
        return bytes;
    }
    std::vector<std::shared_ptr<rhi::Texture>>
    CreateAliasedTextures(std::span<const rhi::TextureDesc> descs) override
    {
        QueryAliasedTextureBytes(descs);
        auto backing = std::make_shared<int>(0);
        std::vector<std::shared_ptr<rhi::Texture>> textures;
        for (const auto& desc : descs)
        {
            auto texture = CreateTexture(desc);
            texture->aliasBacking = backing;
            textures.push_back(std::move(texture));
        }
        return textures;
    }
    using rhi::Runtime::CreateView;
    std::shared_ptr<rhi::TextureView> CreateView(std::shared_ptr<rhi::Texture> t,
                                                 rhi::TextureViewUsage usage) override
    {
        auto& cache = usage == rhi::TextureViewUsage::Sampled ? t->sampledView : t->defaultView;
        if (auto cached = cache.lock())
            return cached;
        auto view = std::make_shared<rhi::TextureView>(t, usage);
        cache = view;
        return view;
    }
    std::shared_ptr<rhi::Sampler> CreateSampler() override
    {
        return std::make_shared<rhi::Sampler>(this);
    }
    std::shared_ptr<rhi::BindingLayout> CreateLayout(std::vector<rhi::BindingDesc> e) override
    {
        return std::make_shared<rhi::BindingLayout>(this, std::move(e));
    }
    std::shared_ptr<rhi::BindingGroup> CreateGroup(std::shared_ptr<rhi::BindingLayout> l,
                                                   std::vector<rhi::Binding> b) override
    {
        return std::make_shared<rhi::BindingGroup>(std::move(l), std::move(b));
    }
    std::shared_ptr<rhi::Pipeline> CreatePipelineNative(rhi::PipelineDesc d) override
    {
        return std::make_shared<rhi::Pipeline>(this, std::move(d));
    }
    std::unique_ptr<rhi::Swapchain> CreateSwapchain(platform::NativeSurface, uint32_t, uint32_t,
                                                    bool) override
    {
        return {};
    }
    std::shared_ptr<Submission> Execute(const rhi::CommandList& commands, uint64_t,
                                        rhi::Swapchain*) override
    {
        if (failBefore)
            throw std::runtime_error("Injected before enqueue");
        auto work = std::make_shared<Work>();
        if (failAfter)
        {
            RetainSubmitting(work);
            throw std::runtime_error("Injected after enqueue");
        }
        nativeRanges = commands.RecordingRanges();
        aliasBefore.clear();
        aliasPrevious.clear();
        for (const auto& command : commands.GetCommands())
        {
            if (const auto* draw = std::get_if<rhi::Draw>(&command))
                drawnVertices.push_back(draw->first);
            if (const auto* alias = std::get_if<rhi::TextureAlias>(&command))
            {
                aliasBefore.push_back(alias->before);
                aliasPrevious.push_back(alias->previous.get());
            }
        }
        return work;
    }
    void DrainNative() override
    {
        if (failDrain)
            throw std::runtime_error("Injected drain failure");
        ++drains;
    }
    void WaitNative(uint64_t point) override
    {
        completed = point;
    }
    uint64_t Completed() const override
    {
        return completed;
    }
    double TimestampPeriodNs() const override
    {
        return 1;
    }
};

std::shared_ptr<rhi::Pipeline> Compute(FakeRuntime& runtime, rhi::BindingKind kind)
{
    rhi::PipelineDesc desc;
    desc.layout = runtime.CreateLayout({{0, kind}});
    desc.compute = std::make_shared<rhi::ShaderModule>(&runtime, rhi::ShaderStage::Compute,
                                                       std::vector<std::byte>{}, "CSMain");
    return std::make_shared<rhi::Pipeline>(&runtime, std::move(desc));
}
void AliasContracts()
{
    const std::array<rhi::TextureDesc, 2> descs = {
        rhi::TextureDesc{8, 8, rhi::Format::Rgba8, false, true, "A"},
        rhi::TextureDesc{4, 4, rhi::Format::Rgba8, false, true, "B"}};
    const auto clear =
        [](Graph& graph, TextureHandle& texture, rhi::LoadOp load = rhi::LoadOp::Clear)
    {
        auto pass = graph.AddRasterPass("Clear");
        texture = pass.ColorTarget(texture, load);
        pass.SideEffect();
        return pass.Execute([](RasterContext&) {});
    };
    const auto build = [&](bool cancel = false)
    {
        auto graph = std::make_unique<Graph>();
        for (const auto& desc : descs)
        {
            auto texture = graph->CreateTexture(desc.name, desc);
            clear(*graph, texture);
        }
        if (cancel)
        {
            auto pass = graph->AddComputePass("Cancel");
            pass.SideEffect();
            pass.Execute([](ComputeContext&)
                         { throw std::runtime_error("Alias recording cancelled"); });
        }
        graph->Compile({.memoryAlias = true});
        return graph;
    };
    FakeRuntime runtime;
    Pool pool(runtime, 4, 1024);
    {
        auto graph = build();
        Require(graph->GetPlan().aliasGroups.size() == 1, "Disjoint graphics clears must alias");
        graph->Execute(pool);
        Require(runtime.aliasBefore == std::vector(2, rhi::State::Undefined) &&
                    runtime.aliasPrevious[0] == nullptr && runtime.aliasPrevious[1],
                "Initial alias activation is incomplete");
        runtime.WaitIdle();
    }
    {
        auto graph = build(true);
        Fails([&] { graph->Execute(pool); }, "Alias recording cancelled");
    }
    {
        auto graph = build();
        graph->Execute(pool);
        Require(runtime.aliasBefore == std::vector(2, rhi::State::ColorAttachment) &&
                    runtime.aliasPrevious[0] && runtime.aliasPrevious[1],
                "Alias cancellation lost previous native state/active member");
        runtime.WaitIdle();
    }
    const auto allocationCount = pool.Stats().allocations;
    {
        auto graph = build();
        runtime.failBefore = true;
        Fails([&] { graph->Execute(pool); }, "Injected before");
        runtime.failBefore = false;
        auto next = build();
        runtime.Wait(next->Execute(pool).completion);
    }
    Require(pool.Stats().allocations == allocationCount,
            "Failed/cancelled alias lease was retained");
    {
        auto pin = pool.AcquireAliases(descs);
        Require(pin->active == 1, "Successful submission did not publish final active alias");
        auto view = runtime.CreateView(pin->members[0]->texture);
        pin->members[0]->view = view;
        pin.reset();
        pool.Trim();
        Require(pool.Stats().cached == 2, "An external view must pin its entire alias group");
    }
    pool.Trim();
    Require(pool.Stats().cached == 0, "Retired alias groups did not trim");
    {
        // Group leases exist before physical resources/views are attached to graph resources.
        Pool limited(runtime, 2, 256);
        // Force failure after the group is reserved, in the ordinary resource preparation loop.
        Graph failing;
        auto extra = failing.CreateTexture("Extra", {8, 8, rhi::Format::R32Float, false, true});
        clear(failing, extra);
        for (const auto& desc : descs)
        {
            auto texture = failing.CreateTexture(desc.name, desc);
            clear(failing, texture);
        }
        failing.Compile({.memoryAlias = true});
        Fails([&] { failing.Execute(limited); }, "pool is full");
        auto recovered = limited.AcquireAliases(descs);
        Require(limited.Stats().allocations == 2 && recovered->active == UINT32_MAX,
                "Failed preparation retained an alias lease or published active state");
    }
    {
        Pool exact(runtime, 2, 256);
        auto group = exact.AcquireAliases(descs);
        Fails([&] { exact.Acquire(descs[1]); }, "pool is full");
        group.reset();
        auto ordinary = exact.Acquire(descs[1]);
        Require(exact.Stats().cached == 1 && exact.Stats().estimatedBytes == 64,
                "Budget eviction must remove an entire alias group");
        Pool tooSmall(runtime, 1, 256);
        Fails([&] { tooSmall.AcquireAliases(descs); }, "limit");
    }
    {
        FakeRuntime failed;
        Pool failedPool(failed, 2, 1024);
        failed.failAfter = true;
        {
            auto graph = build();
            Fails([&] { graph->Execute(failedPool); }, "Injected after");
        }
        failedPool.Trim();
        Require(failedPool.Stats().cached == 2 && failed.PendingCount() == 1 &&
                    failed.LastSubmitted() == 0,
                "Partial alias submission must retain the entire group without publication");
        failed.WaitIdle();
        failedPool.Trim();
        Require(failedPool.Stats().cached == 0, "Drained failed alias group did not retire");
    }
    // Simultaneous attachments, later reads, exports, imports and non-clear first uses exclude
    // alias.
    for (uint32_t mode = 0; mode < 6; ++mode)
    {
        Graph graph;
        auto a = graph.CreateTexture("A", descs[0]);
        auto b = graph.CreateTexture(
            "B", mode == 5 ? rhi::TextureDesc{8, 8, rhi::Format::R32Float, false, true} : descs[1]);
        if (mode == 3)
            a = graph.ImportTexture("External", runtime.CreateTexture(descs[0]), {});
        if (mode == 4)
        {
            Fails([&] { clear(graph, a, rhi::LoadOp::Discard); }, "Use Clear or Load");
            continue;
        }
        if (mode == 0)
        {
            auto pass = graph.AddRasterPass("Simultaneous");
            a = pass.ColorTarget(a);
            // Same dimensions are required for simultaneous attachments.
            b = graph.CreateTexture("SameSize", descs[0]);
            b = pass.ColorTarget(b);
            pass.SideEffect();
            pass.Execute([](RasterContext&) {});
        }
        else
        {
            clear(graph, a);
            clear(graph, b);
            if (mode == 1)
                Read(graph, a);
            if (mode == 2)
                graph.Export(a, rhi::State::Sampled);
        }
        Require(graph.Compile({.memoryAlias = true}).aliasGroups.empty(),
                "Unsafe/incompatible alias candidate was grouped");
    }
    {
        Graph graph;
        auto a = graph.CreateTexture("A", descs[0]);
        auto b = graph.CreateTexture("B", descs[1]);
        clear(graph, a);
        clear(graph, b);
        Require(graph.Compile().aliasGroups.empty(), "Alias default must remain off");
    }
    {
        Graph graph;
        for (uint32_t i = 0; i < 18; ++i)
        {
            auto texture = graph.CreateTexture("Bounded." + std::to_string(i), descs[0]);
            clear(graph, texture);
        }
        const auto& plan = graph.Compile({.memoryAlias = true});
        Require(plan.aliasGroups.size() == 2 && plan.aliasGroups[0].size() == 16 &&
                    plan.aliasGroups[1].size() == 2,
                "Alias group member limit was not respected");
    }
    {
        Graph graph;
        std::array<TextureHandle, 2> depths;
        for (uint32_t i = 0; i < 2; ++i)
        {
            auto color = graph.CreateTexture("Color." + std::to_string(i), descs[0]);
            auto depth = graph.CreateTexture("Depth." + std::to_string(i),
                                             {8, 8, rhi::Format::D32S8, false, true});
            auto pass = graph.AddRasterPass("Stencil initialization");
            pass.ColorTarget(color);
            depths[i] = pass.DepthStencilTarget(depth, {}, {});
            pass.SideEffect();
            pass.Execute([](RasterContext&) {});
        }
        const auto& plan = graph.Compile({.memoryAlias = true});
        Require(plan.resources[depths[0].resource].aliasGroup != UINT32_MAX &&
                    plan.resources[depths[0].resource].aliasGroup ==
                        plan.resources[depths[1].resource].aliasGroup,
                "Full depth/stencil initialization should permit compatible aliases");
    }
}
void Dependencies()
{
    {
        Graph g;
        auto t = Texture(g);
        Read(g, t);
        Fails([&] { g.Compile(); }, "initialization");
    }
    {
        Graph g;
        auto t = Texture(g);
        auto old = Write(g, t);
        Write(g, t);
        g.Export(t, rhi::State::Sampled);
        auto& p = g.Compile();
        Require(!p.passes[old.index].live && p.schedule.size() == 1,
                "Full overwrites cull old content");
    }
    {
        Graph g;
        auto t = Texture(g);
        Write(g, t);
        Write(g, t, Coverage::Preserve);
        g.Export(t, rhi::State::Sampled);
        Require(g.Compile().schedule.size() == 2, "ReadWrite retains old producer");
    }
    {
        Graph g;
        auto t = Texture(g);
        Write(g, t);
        auto a = Read(g, t), b = Read(g, t);
        auto& p = g.Compile();
        Require(p.schedule.size() == 3 &&
                    std::ranges::none_of(p.edges,
                                         [&](auto& e)
                                         {
                                             return (e.before == a.index && e.after == b.index) ||
                                                    (e.before == b.index && e.after == a.index);
                                         }),
                "Read-only consumers share");
    }
    {
        Graph g;
        auto t = Texture(g);
        Write(g, t);
        auto old = t;
        auto writer = Write(g, t), reader = Read(g, old);
        g.Export(t, rhi::State::Sampled);
        auto& p = g.Compile();
        Require(p.schedule[1].pass == reader.index && p.schedule[2].pass == writer.index,
                "WAR moves old reader before physical overwrite");
    }
    {
        Graph g;
        auto t = Texture(g);
        Write(g, t);
        auto reader = Read(g, t);
        auto writer = Write(g, t);
        g.Export(t, rhi::State::Sampled);
        g.After(reader, writer);
        Fails([&] { g.Compile(); }, "cycle");
    }
    {
        Graph a, b;
        auto t = Texture(a);
        Fails([&] { Read(b, t); }, "Foreign");
    }
    {
        Graph g;
        auto t = Texture(g), stale = t;
        Write(g, t);
        Fails([&] { Write(g, stale); }, "Stale");
    }
    {
        Graph g;
        auto t = Texture(g);
        Write(g, t);
        auto dead = Read(g, t, false), live = Read(g, t);
        g.After(live, dead);
        Require(!g.Compile().passes[dead.index].live, "Ordering does not root dead work");
    }
}
void NamesAndBuilders()
{
    Graph g;
    auto first = Texture(g, "Color");
    Fails([&] { Texture(g, "Color"); }, "duplicate");
    g.CreateBuffer("Buffer", {16});
    Fails([&] { g.GetTexture("Buffer"); }, "type");
    Fails([&] { g.GetBuffer("Color"); }, "type");
    Fails([&] { g.GetTexture("Missing"); }, "Unknown");
    auto pass = g.AddComputePass("Producer");
    auto latest = pass.WriteTexture("Color");
    Fails([&] { g.AddCopyPass("Unsealed"); }, "Seal");
    Fails([&] { g.Compile(); }, "Seal");
    pass.Execute([](ComputeContext&) {});
    Require(g.GetTexture("Color") == latest && first.version + 1 == latest.version,
            "Names resolve current logical version");
    Fails([&] { pass.ReadTexture(latest); }, "sealed");
    Fails([&] { g.SetTexture("Alias", first); }, "latest");
    g.SetTexture("Alias", latest);
    auto next = g.AddComputePass("Update");
    auto updated = next.ReadWriteTexture("Alias");
    next.Execute([](ComputeContext&) {});
    Require(g.GetTexture("Color") == updated, "Aliases follow physical resource versions");
    auto replacement = Texture(g, "Replacement");
    Write(g, replacement);
    g.SetTexture("Alias", replacement);
    Require(g.GetTexture("Alias") == replacement && g.GetTexture("Color") == updated,
            "Set changes a logical output without copying data");
    Fails([&] { g.SetBuffer("Color", g.GetBuffer("Buffer")); }, "type");
    Fails([&] { g.Export(first, rhi::State::Sampled); }, "latest");
    g.Export("Alias", rhi::State::Sampled);
    Fails([&] { Write(g, replacement); }, "Export");
}
void AccessValidation()
{
    {
        Graph g;
        auto t = Texture(g);
        auto p = g.AddComputePass("Range");
        Fails([&] { p.ReadTexture(t, Use::Sampled, {1, 1, 0, 1}); }, "mip/layer");
        Fails([&] { p.ReadTexture(t, Use::Sampled, {0, 1, 0, 1, Aspect::Depth}); }, "aspect");
    }
    {
        Graph g;
        auto t = Texture(g);
        auto p = g.AddCopyPass("Wrong");
        Fails([&] { p.ReadTexture(t, Use::Sampled); }, "Copy pass");
    }
    {
        Graph g;
        auto t = Texture(g);
        auto p = g.AddComputePass("Conflict");
        p.ReadTexture(t);
        Fails([&] { p.WriteTexture(t); }, "Conflicting");
    }
    {
        Graph g;
        auto b = g.CreateBuffer("Buffer", {64, rhi::Memory::Device, {}, true});
        auto p = g.AddComputePass("Partial");
        Fails([&] { p.WriteBuffer(b, Use::Storage, {16, 16}); }, "Partial");
        Fails([&] { p.ReadBuffer(b, Use::ShaderRead, {60, 8}); }, "range");
    }
    for (const auto load : {rhi::LoadOp::Clear, rhi::LoadOp::Load})
    {
        Graph g;
        auto t = Texture(g);
        auto p = g.AddRasterPass("Attachment");
        t = p.ColorTarget(t, load);
        p.Execute([](RasterContext&) {});
        g.Export(t, rhi::State::Sampled);
        if (load == rhi::LoadOp::Load)
            Fails([&] { g.Compile(); }, "initialization");
        else
            Require(g.Compile().schedule.size() == 1, "Clear initializes attachment");
    }
    {
        Graph g;
        auto t = Texture(g);
        auto p = g.AddRasterPass("Discard");
        Fails([&] { p.ColorTarget(t, rhi::LoadOp::Discard); }, "Clear or Load");
    }
}
void Lifecycle()
{
    FakeRuntime runtime;
    Pool pool(runtime, 4, 4096);
    bool rejected = false;
    std::jthread thread(
        [&]
        {
            try
            {
                pool.Stats();
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    thread.join();
    Require(rejected, "Pool rejects foreign thread");
    auto imported = runtime.CreateTexture({8, 8, rhi::Format::Rgba8, true, true, "Import"});
    for (bool cull : {true, false})
    {
        Graph g;
        auto t = g.ImportTexture("Import", imported, {rhi::State::Sampled, true, 0});
        Read(g, t, false);
        Require(g.Compile({cull}).schedule.size() == (cull ? 0u : 1u), "Import is not a root");
    }
    {
        Graph g;
        auto t = Texture(g);
        auto pass = g.AddComputePass("Cancelled");
        t = pass.WriteTexture(t);
        pass.Execute([](ComputeContext&) { throw std::runtime_error("cancel recording"); });
        g.Export(t, rhi::State::Sampled);
        g.Compile({.recordingThreads = 4});
        Fails([&] { g.Execute(pool); }, "cancel recording");
        Require(runtime.LastSubmitted() == 0 && pool.Stats().allocations == 1,
                "Cancelled packets never publish completion");
    }
    {
        Graph g;
        auto t = Texture(g);
        Write(g, t);
        g.Export(t, rhi::State::Sampled);
        g.Compile();
        auto result = g.Execute(pool);
        Require(pool.Stats().reuses == 1 &&
                    g.GetPlan().resources[0].initial == rhi::State::Undefined,
                "Cancellation preserves actual pool state and releases lease");
        Fails([&] { g.Execute(pool); }, "once");
        result.exports.clear();
    }
    {
        Graph g;
        auto t = Texture(g);
        Write(g, t);
        g.Export(t, rhi::State::Sampled);
        g.Compile();
        g.Execute(pool);
        Require(pool.Stats().allocations == 2, "In-flight allocation is not reused");
    }
    runtime.WaitIdle();
    {
        Graph g;
        auto t = Texture(g);
        Write(g, t);
        g.Export(t, rhi::State::Sampled);
        g.Compile();
        g.Execute(pool);
        Require(pool.Stats().reuses >= 2 && g.GetPlan().resources[0].initial == rhi::State::Sampled,
                "Completed allocation reuses published final state");
    }
    runtime.WaitIdle();
    {
        Pool small(runtime, 1, 1024);
        auto a = small.Acquire(rhi::TextureDesc{8, 8});
        Fails([&] { small.Acquire(rhi::TextureDesc{8, 8}); }, "pool is full");
        a.reset();
        auto b = small.Acquire(rhi::TextureDesc{16, 16});
        Require(small.Stats().cached == 1, "Bounded pool evicts idle incompatible resource");
    }
    {
        Graph g;
        auto t = g.ImportTexture("Future", imported,
                                 {rhi::State::Sampled, true, runtime.LastSubmitted() + 1});
        Read(g, t);
        g.Compile();
        Fails([&] { g.Execute(pool); }, "not submitted");
    }
    {
        FakeRuntime foreign;
        Graph g;
        auto t = g.ImportTexture("Foreign", imported, {rhi::State::Sampled, true, 0});
        Read(g, t);
        g.Compile();
        Pool other(foreign);
        Fails([&] { g.Execute(other); }, "another runtime");
    }
    {
        Graph g;
        auto b = runtime.CreateBuffer({16, rhi::Memory::Upload, "Upload"});
        Fails([&] { g.ImportBuffer("Upload", b, {rhi::State::CopyDest, false, 0}); },
              "import/export state");
    }
    pool.Trim();
    Require(pool.Stats().cached == 0, "Trim releases idle completed allocations");
}
void RecordingValidation()
{
    FakeRuntime runtime;
    Pool pool(runtime);
    auto pipeline = Compute(runtime, rhi::BindingKind::StorageTexture);
    for (bool declare : {false, true})
    {
        Graph g;
        auto old = Texture(g);
        auto p = g.AddComputePass("UndeclaredVersion");
        if (declare)
            p.WriteTexture(old);
        p.SideEffect();
        p.Execute(
            [pipeline, old](ComputeContext& c)
            {
                c.Bind(pipeline);
                c.BindTexture(0, old);
            });
        g.Compile();
        Fails([&] { g.Execute(pool); }, "Undeclared");
    }
    {
        Graph g;
        auto physical = runtime.CreateBuffer({128, rhi::Memory::Device, "Input"});
        auto input = g.ImportBuffer("Input", physical, {rhi::State::CopySource, true, 0});
        auto output = g.CreateBuffer("Output", {128});
        auto p = g.AddCopyPass("Bounded");
        p.ReadBuffer(input, Use::CopySource, {0, 16});
        output = p.WriteBuffer(output);
        p.SideEffect();
        p.Execute([input, output](CopyContext& c) { c.Copy(input, output, 32); });
        g.Compile();
        Fails([&] { g.Execute(pool); }, "declared buffer range");
    }
    {
        Graph g;
        auto b = runtime.CreateBuffer({256, rhi::Memory::Upload, "Constants"});
        auto input = g.ImportBuffer("Constants", b, {rhi::State::HostWrite, true, 0});
        auto p = g.AddComputePass("BoundedBinding");
        p.ReadBuffer(input, Use::Uniform, {0, 16});
        p.SideEffect();
        auto cp = Compute(runtime, rhi::BindingKind::ConstantBuffer);
        p.Execute(
            [cp, input](ComputeContext& c)
            {
                c.Bind(cp);
                c.BindBuffer(0, input, 0, 256);
            });
        g.Compile();
        Fails([&] { g.Execute(pool); }, "declared buffer range");
    }
    {
        Graph g;
        auto count = std::make_shared<int>(7);
        std::weak_ptr<int> weak = count;
        auto p = g.AddComputePass("OwnedCapture");
        p.SideEffect();
        p.Execute([count](ComputeContext&)
                  { Require(*count == 7, "Captured value survived caller"); });
        count.reset();
        Require(!weak.expired(), "Graph owns callback");
        g.Compile();
        g.Execute(pool);
    }
    runtime.WaitIdle();
}
class TestFeature final : public renderer::RenderFeature
{
public:
    using BuildFunction = std::function<void(Graph&, const renderer::RenderFrame&)>;
    TestFeature(std::string name, BuildFunction build, std::vector<std::string> dependencies = {})
        : RenderFeature(std::move(name), std::move(dependencies)), build_(std::move(build))
    {
    }
    void Build(Graph& graph, const renderer::RenderFrame& frame) override
    {
        build_(graph, frame);
    }

private:
    BuildFunction build_;
};
void FeatureCreationContracts()
{
    using namespace renderer;
    int builds = 0;
    std::vector<std::string> sequence;
    const auto feature = [&](std::string name, std::vector<std::string> dependencies = {})
    {
        return std::make_shared<TestFeature>(
            name,
            [&, name](Graph&, const RenderFrame&)
            {
                ++builds;
                sequence.push_back(name);
            },
            std::move(dependencies));
    };
    auto a = feature("A");
    auto b = feature("B", {"A"});
    auto c = feature("C", {"B"});
    std::vector<int> permutation{0, 1, 2};
    const std::vector<std::shared_ptr<RenderFeature>> features{a, b, c};
    do
    {
        auto builder = CreatePipeline(PipelineType::Custom);
        for (const auto index : permutation)
            builder.AddFeature(features[index]);
        const auto oldBuilds = builds;
        auto pipeline = builder.Create();
        Require(builds == oldBuilds, "Create must not build a graph");
        builder.AddFeature(feature("Later"));
        Graph graph;
        sequence.clear();
        pipeline.Build(graph, {});
        Require(sequence == std::vector<std::string>({"A", "B", "C"}),
                "Dependency order ignores registration and created composition is frozen");
    } while (std::next_permutation(permutation.begin(), permutation.end()));
    auto missing = CreatePipeline(PipelineType::Custom);
    missing.AddFeature(b);
    Fails([&] { missing.Create(); }, "Missing");
    auto duplicate = CreatePipeline(PipelineType::Custom);
    duplicate.AddFeature(a).AddFeature(a);
    Fails([&] { duplicate.Create(); }, "Duplicate");
    auto cycle = CreatePipeline(PipelineType::Custom);
    cycle.AddFeature(feature("X", {"Y"})).AddFeature(feature("Y", {"X"}));
    Fails([&] { cycle.Create(); }, "cycle");
    auto self = CreatePipeline(PipelineType::Custom);
    self.AddFeature(feature("Self", {"Self"}));
    Fails([&] { self.Create(); }, "cycle");
    auto conflict = CreatePipeline(PipelineType::Forward);
    conflict.AddFeature(feature("Early", {"Forward.Transparent"}), RenderStage::BeforeOpaque);
    Fails([&] { conflict.Create(); }, "cycle");
    Fails([&] { CreatePipeline(static_cast<PipelineType>(100)); }, "unimplemented");
    // Presets do not change stage/dependency semantics.
    auto custom = CreatePipeline(PipelineType::Custom);
    custom.AddFeature(b, RenderStage::AfterTransparent).AddFeature(a, RenderStage::BeforeOpaque);
    Graph graph;
    sequence.clear();
    custom.Create().Build(graph, {});
    Require(sequence == std::vector<std::string>({"A", "B"}), "Custom supports the same stages");
}
void ForwardContracts()
{
    using namespace renderer;
    FakeRuntime runtime;
    Pool pool(runtime);
    Graph graph;
    RenderFrame frame;
    Texture(graph, "CameraColor");
    graph.CreateTexture("CameraDepth", {8, 8, rhi::Format::D32, false, true});
    auto vertices = runtime.CreateBuffer({256, rhi::Memory::Device, "Vertices"});
    rhi::PipelineDesc desc;
    desc.layout = runtime.CreateLayout({});
    desc.depthStencilFormat = rhi::Format::D32;
    desc.depth = {true, true};
    desc.depth.writeEnabled = false;
    desc.blend.enabled = true;
    desc.vertexStride = 4;
    DrawPacket draw;
    draw.pipeline = std::make_shared<rhi::Pipeline>(&runtime, desc);
    graph.ImportBuffer("Vertices", vertices, {rhi::State::Vertex, true});
    draw.vertices = "Vertices";
    draw.count = 3;
    for (uint32_t i = 1; i <= 3; ++i)
    {
        draw.first = i;
        draw.distance = i == 1 ? 1.f : 5.f;
        frame.transparent.push_back(draw);
    }
    auto builder = CreatePipeline(PipelineType::Forward);
    std::vector<int> stages;
    builder
        .AddFeature(std::make_shared<TestFeature>("After", [&](Graph&, const RenderFrame&)
                                                  { stages.push_back(3); }),
                    RenderStage::AfterTransparent)
        .AddFeature(std::make_shared<TestFeature>("Before",
                                                  [&](Graph& g, const RenderFrame&)
                                                  {
                                                      stages.push_back(1);
                                                      auto p = g.AddComputePass("Before");
                                                      p.SideEffect();
                                                      p.Execute([](ComputeContext&) {});
                                                  }),
                    RenderStage::BeforeOpaque)
        .AddFeature(std::make_shared<TestFeature>("Dead",
                                                  [&](Graph& g, const RenderFrame&)
                                                  {
                                                      stages.push_back(2);
                                                      auto p = g.AddComputePass("Dead");
                                                      p.Execute([](ComputeContext&) {});
                                                  }),
                    RenderStage::AfterOpaque);
    Fails([&] { builder.AddFeature({}); }, "Empty");
    auto nothing = std::make_shared<TestFeature>("Nothing", [](Graph&, const RenderFrame&) {});
    Fails([&] { builder.AddFeature(nothing, static_cast<RenderStage>(100)); }, "Unknown");
    auto pipeline = builder.Create();
    pipeline.Build(graph, frame);
    frame.transparent.clear();
    Require(stages == std::vector<int>({1, 2, 3}), "Features build in stage order");
    graph.Export("CameraColor", rhi::State::Sampled);
    const auto& plan = graph.Compile();
    Require(plan.schedule.size() == 3 && !plan.passes[2].live,
            "Empty opaque initializes; transparent retains depth; dead feature is culled");
    Require(std::ranges::any_of(plan.edges,
                                [](const auto& e) { return e.before == 0 && e.after == 3; }),
            "Stage order survives an intervening culled feature");
    graph.Execute(pool);
    Require(runtime.drawnVertices == std::vector<uint32_t>({2, 3, 1}),
            "Transparent order is stable far-to-near and owns input copies");
    {
        Graph invalid;
        auto color = Texture(invalid, "Color");
        auto depth = invalid.CreateTexture("Depth", {8, 8, rhi::Format::D32, false, true});
        auto initialize = invalid.AddRasterPass("Initialize");
        color = initialize.ColorTarget(color);
        depth = initialize.DepthTarget(depth);
        initialize.Execute([](RasterContext&) {});
        auto readonly = invalid.AddRasterPass("ReadOnlyDepth");
        color = readonly.ColorTarget(color, rhi::LoadOp::Load);
        readonly.DepthTarget(depth, rhi::LoadOp::Load, AttachmentAccess::ReadOnly);
        auto writingDesc = desc;
        writingDesc.depth.writeEnabled = true;
        auto writing = std::make_shared<rhi::Pipeline>(&runtime, writingDesc);
        readonly.Execute([writing](RasterContext& c) { c.Bind(writing); });
        invalid.Export(color, rhi::State::Sampled);
        invalid.Compile();
        Fails([&] { invalid.Execute(pool); }, "depth writes disabled");
    }
    runtime.WaitIdle();
}

void StencilContracts()
{
    FakeRuntime runtime;
    Pool pool(runtime);
    {
        Graph graph;
        auto depth = graph.CreateTexture("Depth", {8, 8, rhi::Format::D32S8, false, true});
        auto color = Texture(graph, "Color");
        auto pass = graph.AddRasterPass("PartialClear");
        pass.ColorTarget(color);
        pass.DepthStencilTarget(depth, {rhi::LoadOp::Clear},
                                {rhi::LoadOp::Load, AttachmentAccess::ReadOnly});
        pass.SideEffect();
        pass.Execute([](RasterContext&) {});
        Fails([&] { graph.Compile(); }, "initialization");
    }
    {
        Graph graph;
        auto depth = graph.CreateTexture("Depth", {8, 8, rhi::Format::D32, false, true});
        auto pass = graph.AddRasterPass("InvalidStencil");
        Fails([&] { pass.DepthStencilTarget(depth, {}, {}); }, "stencil format");
    }
    {
        Graph graph;
        auto color = Texture(graph, "Color");
        auto depth = graph.CreateTexture("Depth", {8, 8, rhi::Format::D32S8, false, true});
        auto initialize = graph.AddRasterPass("Mask");
        color = initialize.ColorTarget(color);
        depth = initialize.DepthTarget(depth);
        initialize.Execute([](RasterContext&) {});
        auto read = graph.AddRasterPass("ReadOnlyStencil");
        color = read.ColorTarget(color, rhi::LoadOp::Load);
        read.DepthStencilTarget(depth, {rhi::LoadOp::Clear},
                                {rhi::LoadOp::Load, AttachmentAccess::ReadOnly});
        rhi::PipelineDesc desc;
        desc.layout = runtime.CreateLayout({});
        desc.depthStencilFormat = rhi::Format::D32S8;
        desc.stencil.enabled = true;
        desc.stencil.back.fail = rhi::StencilOp::Replace;
        auto writing = std::make_shared<rhi::Pipeline>(&runtime, desc);
        read.Execute([writing](RasterContext& context) { context.Bind(writing); });
        graph.Export(color, rhi::State::Sampled);
        graph.Compile();
        Require(graph.GetPlan().passes[0].live,
                "Stencil Load must retain the mask producer when depth is cleared");
        Fails([&] { graph.Execute(pool); }, "stencil writes disabled");
    }
    runtime.WaitIdle();
}
void ProfilingContracts()
{
    FakeRuntime runtime;
    {
        struct BudgetRuntime : FakeRuntime
        {
            void Charge(uint64_t bytes)
            {
                ledger_->Add(bytes);
            }
            void Release(uint64_t bytes)
            {
                ledger_->Remove(bytes);
            }
            void Reserve(uint64_t bytes)
            {
                ledger_->ReserveBlock(bytes);
            }
            void ReleaseBlock(uint64_t bytes)
            {
                ledger_->ReleaseBlock(bytes);
            }
        } budget;
        Fails([&] { budget.SetAllocationBudget(0); }, "nonzero");
        budget.SetAllocationBudget(1024);
        budget.Charge(512);
        Fails([&] { budget.SetAllocationBudget(511); }, "current native usage");
        Fails([&] { budget.Charge(513); }, "requested=513 bytes, used=512 bytes, limit=1024 bytes");
        Require(budget.GetMemoryStats().used == 512 && budget.GetMemoryStats().highWater == 512,
                "Rejected allocation must not change ledger accounting");
        budget.SetAllocationBudget(2048);
        budget.Charge(1024);
        Require(budget.GetMemoryStats().used == 1536 && budget.GetMemoryStats().budget == 2048,
                "Configured budget must cover tracked allocations");
        std::thread worker([&]
                           { Fails([&] { budget.SetAllocationBudget(4096); }, "owner thread"); });
        worker.join();
        budget.Release(1024);
        budget.Release(512);
        budget.Reserve(1024);
        Require(budget.GetMemoryStats().used == 0 && budget.GetMemoryStats().reserved == 1024,
                "Idle backing must be visible independently of resource allocations");
        Fails([&] { budget.SetAllocationBudget(1023); }, "reserved backing");
        budget.SetAllocationBudget(1024);
        Fails([&] { budget.Charge(1); }, "reserved=1024 bytes");
        budget.ReleaseBlock(1024);
    }
    Pool pool(runtime);
    {
        Pool views(runtime, 1, 1024);
        auto lease = views.Acquire(rhi::TextureDesc{8, 8});
        auto* identity = lease->texture.get();
        lease->view = runtime.CreateView(lease->texture);
        auto retained = lease->view;
        std::weak_ptr<rhi::Texture> weak = lease->texture;
        lease.reset();
        Fails([&] { views.Acquire(rhi::TextureDesc{8, 8}); }, "pool is full");
        views.Trim();
        Require(views.Stats().cached == 1, "A retained View must prevent pool trim/reuse");
        retained.reset();
        lease = views.Acquire(rhi::TextureDesc{8, 8});
        Require(lease->texture.get() == identity &&
                    lease->view == lease->texture->defaultView.lock(),
                "Pool must reuse a texture with its internal default View");
        lease.reset();
        views.Trim();
        Require(weak.expired() && views.Stats().cached == 0, "Default View must not form a cycle");
    }
    {
        Pool hdr(runtime, 2, 1024);
        Fails([&] { hdr.Acquire(rhi::TextureDesc{16, 16, rhi::Format::Rgba32Float}); },
              "byte limit");
        auto lease = hdr.Acquire(rhi::TextureDesc{8, 8, rhi::Format::Rgba32Float});
        Require(hdr.Stats().estimatedBytes == 1024,
                "RGBA32F pool budget must account for 16 bytes per texel");
        Fails([&] { hdr.Acquire(rhi::TextureDesc{1, 1}); }, "pool is full");
        Graph estimate;
        auto texture = estimate.CreateTexture("HDR", {8, 8, rhi::Format::Rgba32Float, false, true});
        auto pass = estimate.AddRasterPass("HDR.Clear");
        texture = pass.ColorTarget(texture);
        pass.Execute([](RasterContext&) {});
        estimate.Export(texture, rhi::State::Sampled);
        Require(estimate.Compile().resources.front().estimatedBytes == 1024,
                "Graph resource diagnostics must use the actual HDR texel size");
    }
    Graph graph;
    for (int i = 0; i < 33; ++i)
    {
        auto pass = graph.AddComputePass("Profile." + std::to_string(i));
        pass.SideEffect();
        pass.Execute([](ComputeContext&) {});
    }
    graph.Compile({.profile = true});
    const auto submission = graph.Execute(pool);
    Require(!submission.profile.ticks &&
                graph.GetPlan().profilingReason.find("32 live passes") != std::string::npos,
            "Over-budget profiling must preserve rendering and explain the missing timestamps");
    Submission::GpuProfile profile;
    Require(profile.Read(runtime).empty(), "Disabled profile must not access readback memory");
    profile.ticks = runtime.CreateBuffer({16, rhi::Memory::Readback, "Profile"});
    profile.names = {"test"};
    profile.completion = runtime.LastSubmitted() + 1;
    Fails([&] { profile.Read(runtime); }, "completed submission");
}
void QueueContracts()
{
    {
        FakeRuntime runtime;
        runtime.completed = 1;
        Submission::GpuProfile profile;
        profile.ticks = runtime.CreateBuffer({32, rhi::Memory::Readback, "LargeClock"});
        profile.completion = 1;
        profile.names = {"Compute", "Graphics"};
        profile.clocks = {{0, 0, 1, rhi::QueueType::Compute}, {0, 0, 1, rhi::QueueType::Graphics}};
        const uint64_t anchor = (uint64_t{1} << 62) + 1000;
        const std::array<uint64_t, 4> ticks{anchor, anchor + 125, anchor - 25, anchor + 25};
        std::memcpy(profile.ticks->Map().data(), ticks.data(), sizeof(ticks));
        const auto times = profile.Read(runtime);
        Require(times[0].startNanoseconds == 0 && times[0].endNanoseconds == 125 &&
                    times[1].startNanoseconds == -25 && times[1].endNanoseconds == 25 &&
                    times[1].nanoseconds == 50,
                "Large shared GPU clock lost interval precision or cross-queue origin");
    }
    {
        FakeRuntime runtime;
        runtime.asyncAvailable = false;
        Pool pool(runtime);
        Graph graph;
        auto pass = graph.AddComputePass("Unavailable");
        pass.AllowAsyncCompute();
        pass.SideEffect();
        pass.Execute([](ComputeContext&) {});
        graph.Compile({.asyncCompute = true});
        Fails([&] { graph.Execute(pool); }, "unavailable");
        Require(pool.Stats().cached == 0 && runtime.LastSubmitted() == 0,
                "Unavailable async queue allocated or submitted work");
    }
    for (const bool enabled : {false, true})
    {
        FakeRuntime runtime;
        Pool pool(runtime);
        auto upload = runtime.CreateBuffer({16, rhi::Memory::Upload, "Upload"});
        for (uint32_t frame = 0; frame < 2; ++frame)
        {
            Graph graph;
            auto input = graph.ImportBuffer("Upload", upload, {rhi::State::HostWrite, true});
            auto output = graph.CreateBuffer("Output", {16, rhi::Memory::Device, "", true});
            auto produce = graph.AddComputePass("Produce");
            produce.AllowAsyncCompute();
            produce.ReadBuffer(input, Use::ShaderRead);
            output = produce.WriteBuffer(output);
            produce.Execute([](ComputeContext&) {});
            auto independent = graph.AddComputePass("SharedUpload");
            independent.ReadBuffer(input, Use::Uniform);
            independent.SideEffect();
            independent.Execute([](ComputeContext&) {});
            auto consume = graph.AddCopyPass("Consume");
            consume.ReadBuffer(output);
            consume.SideEffect();
            consume.Execute([](CopyContext&) {});
            graph.Export(output, rhi::State::ShaderRead);
            const auto& plan = graph.Compile({.recordingThreads = 1, .asyncCompute = enabled});
            Require(plan.asyncCompute == enabled && !plan.schedule[1].waitCompute &&
                        plan.schedule[2].waitCompute == enabled,
                    "Queue plan serialized independent reads or missed a consumer wait");
            graph.Execute(pool);
            if (enabled)
            {
                Require(runtime.nativeRanges.size() == 4 &&
                            runtime.nativeRanges[0].queue == rhi::QueueType::Graphics &&
                            runtime.nativeRanges[1].queue == rhi::QueueType::Compute &&
                            !runtime.nativeRanges[2].waitCompute &&
                            runtime.nativeRanges[3].waitCompute,
                        "Queue packet ranges lost prologue, independent work or consumer wait");
                Require(frame == 0 || !plan.prologueBarriers.empty(),
                        "Cached buffer state was not handed to the Compute queue");
            }
            runtime.WaitIdle();
        }
    }
    for (const bool texture : {false, true})
    {
        FakeRuntime runtime;
        Graph graph;
        auto buffer = runtime.CreateBuffer({16, rhi::Memory::Device, "Device"});
        auto imported = graph.ImportBuffer("Device", buffer, {rhi::State::ShaderRead, true});
        auto image = Texture(graph);
        auto pass = graph.AddComputePass("Unsupported");
        pass.AllowAsyncCompute();
        if (texture)
            pass.WriteTexture(image);
        else
            pass.ReadBuffer(imported, Use::ShaderRead);
        pass.SideEffect();
        pass.Execute([](ComputeContext&) {});
        Fails([&] { graph.Compile({.asyncCompute = true}); }, texture ? "textures" : "Upload");
    }
    {
        Graph graph;
        auto buffer = graph.CreateBuffer("Late", {16, rhi::Memory::Device, "", true});
        auto graphics = graph.AddCopyPass("GraphicsProducer");
        buffer = graphics.WriteBuffer(buffer);
        graphics.Execute([](CopyContext&) {});
        auto async = graph.AddComputePass("LateAsync");
        async.AllowAsyncCompute();
        buffer = async.ReadWriteBuffer(buffer);
        async.Execute([](ComputeContext&) {});
        graph.Export(buffer, rhi::State::ShaderRead);
        Fails([&] { graph.Compile({.asyncCompute = true}); }, "depend on Graphics");
    }
    for (const bool started : {false, true})
    {
        FakeRuntime runtime;
        runtime.failBefore = !started;
        runtime.failAfter = started;
        auto buffer = runtime.CreateBuffer({16, rhi::Memory::Upload, "Retained"});
        std::weak_ptr<rhi::Buffer> weak = buffer;
        auto commands = runtime.CreateCommands();
        commands->Barrier(buffer, rhi::State::HostWrite, rhi::State::UniformRead);
        buffer.reset();
        Fails([&] { runtime.Submit(std::move(commands)); },
              started ? "after enqueue" : "before enqueue");
        Require(runtime.LastSubmitted() == 0 && runtime.Completed() == 0 &&
                    runtime.PendingCount() == size_t(started) && weak.expired() != started,
                "Failed submission lost native uses or invented a completion");
        if (started)
        {
            runtime.Collect();
            Fails([&] { runtime.Submit(runtime.CreateCommands()); }, "poisoned");
            runtime.failDrain = true;
            Fails([&] { runtime.WaitIdle(); }, "drain failure");
            Require(!weak.expired(), "Failed drain released potentially in-flight resources");
            runtime.failDrain = false;
            runtime.WaitIdle();
            Require(weak.expired() && runtime.drains == 1 && runtime.LastSubmitted() == 0,
                    "Actual queue drain did not retire failed work safely");
        }
    }
}
void AccelerationStructureContracts()
{
    FakeRuntime runtime;
    rhi::AccelerationStructureDesc unsupported;
    unsupported.name = "Unsupported.TLAS";
    unsupported.topLevel = true;
    Fails([&] { runtime.QueryAccelerationStructureSizes(unsupported); },
          "does not support acceleration structures");
    const rhi::BufferDesc storage{256, rhi::Memory::Device, "AS", false, true, false, true};
    {
        Graph graph;
        auto target = graph.CreateBuffer("AS", storage);
        auto pass = graph.AddComputePass("UpdateBeforeBuild");
        pass.ReadWriteBuffer(target, Use::AccelerationStructureBuildWrite);
        pass.SideEffect();
        pass.Execute([](ComputeContext&) {});
        Fails([&] { graph.Compile(); }, "Read before initialization");
    }
    {
        Graph graph;
        auto target = graph.CreateBuffer("AS", storage);
        auto build = graph.AddComputePass("Build");
        target = build.WriteBuffer(target, Use::AccelerationStructureBuildWrite);
        build.Execute([](ComputeContext&) {});
        auto update = graph.AddComputePass("Update");
        target = update.ReadWriteBuffer(target, Use::AccelerationStructureBuildWrite);
        update.Execute([](ComputeContext&) {});
        auto trace = graph.AddComputePass("Trace");
        trace.ReadBuffer(target, Use::AccelerationStructureRead);
        trace.SideEffect();
        trace.Execute([](ComputeContext&) {});
        Require(graph.Compile().schedule.size() == 3,
                "A preserving AS update must retain the original build producer");
    }
    {
        Graph graph;
        auto target = graph.CreateBuffer("AS", storage);
        auto pass = graph.AddComputePass("InvalidRead");
        Fails([&] { pass.ReadBuffer(target, Use::ShaderRead); },
              "AS storage cannot be used as an ordinary buffer");
    }
}
void ShaderTableContracts()
{
    FakeRuntime runtime;
    Pool pool(runtime, 8, 1024 * 1024);
    rhi::PipelineDesc desc;
    desc.layout = runtime.CreateLayout({});
    desc.rayGeneration = runtime.CreateShader(rhi::ShaderStage::RayGeneration,
                                              std::vector<std::byte>(4), "RayGeneration");
    auto pipeline = std::make_shared<rhi::Pipeline>(&runtime, desc);
    auto buffer =
        runtime.CreateBuffer({256, rhi::Memory::Upload, "Table", false, true, false, false, true});
    auto table = std::make_shared<rhi::ShaderTable>(pipeline, buffer, 0, 64);
    Fails([&] { runtime.CreateShaderTable(pipeline); }, "requires a ray tracing pipeline");
    {
        Graph graph;
        auto records = graph.ImportBuffer("Table", buffer, {rhi::State::HostWrite, true});
        auto pass = graph.AddRayTracingPass("PartialTable");
        records = pass.ReadBuffer(records, Use::ShaderTableRead, {0, 64});
        pass.SideEffect();
        pass.Execute(
            [=](RayTracingContext& context)
            {
                context.Bind(pipeline);
                context.TraceRays(table, records, 1, 1);
            });
        graph.Compile();
        Fails([&] { graph.Execute(pool); }, "exceeds declared buffer range");
    }
    {
        Graph graph;
        auto records = graph.ImportBuffer("Table", buffer, {rhi::State::HostWrite, true});
        auto pass = graph.AddRayTracingPass("FullTable");
        records = pass.ReadBuffer(records, Use::ShaderTableRead);
        pass.SideEffect();
        pass.Execute(
            [=](RayTracingContext& context)
            {
                context.Bind(pipeline);
                context.TraceRays(table, records, 1, 1);
            });
        graph.Compile();
        runtime.Wait(graph.Execute(pool).completion);
    }
    auto commands = runtime.CreateCommands();
    commands->Bind(pipeline);
    commands->Bind(runtime.CreateGroup(desc.layout, {}));
    Fails([&] { commands->Trace(table, UINT32_MAX, UINT32_MAX, UINT32_MAX); }, "Invalid TraceRays");
}
} // namespace
int main()
{
    try
    {
        Dependencies();
        {
            const std::vector<rhi::Command> commands{
                rhi::Draw{3, 0, false, 5},
                rhi::Draw{6, 0, true, 7},
                rhi::SetStencilReference{1},
                rhi::DrawIndirect{nullptr, 0, 128, true, nullptr, 0},
                rhi::Dispatch{2, 3, 4},
                rhi::TraceRays{nullptr, 8, 4, 1},
                rhi::BuildAccelerationStructure{nullptr, nullptr, false}};
            const auto counts = CountCommands(commands);
            Require(
                counts.direct == 2 && counts.indexed == 1 && counts.indirect == 1 &&
                    counts.batches == 2 && counts.directInstances == 12 &&
                    counts.indirectInstancesUnknown && counts.indirectCapacity == 128 &&
                    counts.dispatch == 1 && counts.threadGroups == 24 && counts.traceRays == 1 &&
                    counts.rays == 32 && counts.asBuilds == 1,
                "Actual command counters conflated draw calls, batches, instances or dispatches");
        }
        {
            FakeRuntime runtime;
            Pool pool(runtime);
            Graph graph;
            auto target =
                graph.CreateTexture("Diagnostic.Target", {17, 11, rhi::Format::Rgba8, false, true});
            auto pass = graph.AddRasterPass("Diagnostic.Pass");
            target = pass.ColorTarget(target);
            pass.Execute([](RasterContext&) {});
            graph.Export(target, rhi::State::CopySource);
            const auto& plan = graph.Compile();
            graph.Execute(pool);
            const auto& resource = plan.resources.front();
            Require(resource.texture && resource.exported && resource.width == 17 &&
                        resource.height == 11,
                    "Plan diagnostics lost resource descriptors or export roots");
            Require(plan.passes.front().recordingChunk == 0 &&
                        plan.passes.front().lastCommand > plan.passes.front().firstCommand &&
                        plan.passes.front().accesses.front().textureRange.aspect == Aspect::Color,
                    "Plan diagnostics lost actual recording ranges or texture aspects");
            Require(plan.ToJson().find("Diagnostic.Target") != std::string::npos &&
                        plan.ToJson().find("textureRange") != std::string::npos,
                    "Plan JSON lost diagnostic metadata");
        }
        AliasContracts();
        NamesAndBuilders();
        AccessValidation();
        Lifecycle();
        RecordingValidation();
        FeatureCreationContracts();
        ForwardContracts();
        StencilContracts();
        ProfilingContracts();
        {
            FakeRuntime runtime;
            Pool pool(runtime);
            Graph graph;
            Fails([&] { graph.Compile({.recordingThreads = 0}); }, "Recording threads");
            Fails([&] { graph.Compile({.recordingThreads = 17}); }, "Recording threads");
            const auto owner = std::this_thread::get_id();
            uint32_t callbacks = 0;
            for (uint32_t i = 0; i < 16; ++i)
            {
                auto pass = graph.AddComputePass("Parallel." + std::to_string(i));
                pass.SideEffect();
                pass.Execute(
                    [&](ComputeContext&)
                    {
                        Require(std::this_thread::get_id() == owner,
                                "Graph callbacks left the owner thread");
                        ++callbacks;
                    });
            }
            const auto& plan = graph.Compile({.recordingThreads = 4});
            graph.Execute(pool);
            Require(callbacks == 16 && plan.recordingChunks == 4,
                    "Graph failed to freeze and partition all live passes");
            Require(plan.ToJson().find("parallel-native") != std::string::npos,
                    "Graph dump misreported parallel recording");
            runtime.WaitIdle();
        }
        AccelerationStructureContracts();
        QueueContracts();
        ShaderTableContracts();
        std::cout << "Graph contracts passed: names, typed passes, versions, culling, hazards, "
                     "bindings and lifetime\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
