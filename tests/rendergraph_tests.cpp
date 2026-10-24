#include "rendergraph/graph.hpp"
#include "rendergraph/pool.hpp"
#include "rhi/commands.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <thread>

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
    throw std::runtime_error("Expected graph diagnostic: " + text);
}
struct Data
{
    TextureHandle texture;
    BufferHandle buffer;
};
auto Empty = [](const Data&, ExecuteContext&) {};
PassHandle Write(Graph& graph, TextureHandle& texture, Coverage coverage = Coverage::Full)
{
    return graph.AddPass<Data>(
        "Write", PassKind::Compute,
        [&](auto& b, auto& data)
        {
            data.texture = b.Write(texture, Use::Storage, coverage);
            texture = data.texture;
        },
        Empty);
}
PassHandle Read(Graph& graph, TextureHandle texture, bool root = true)
{
    return graph.AddPass<Data>(
        "Read", PassKind::Compute,
        [&](auto& b, auto& data)
        {
            data.texture = texture;
            b.Read(texture, Use::Sampled);
            if (root)
                b.SideEffect();
        },
        Empty);
}
TextureHandle Texture(Graph& graph)
{
    return graph.Create(rhi::TextureDesc{8, 8, rhi::Format::Rgba8, true, true, "Test.Texture"});
}
struct FakeBuffer : rhi::Buffer
{
    using Buffer::Buffer;
    std::span<std::byte> Map() override
    {
        return {};
    }
};
struct FakeRuntime : rhi::Runtime
{
    uint64_t completed = 0;
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
    std::shared_ptr<rhi::TextureView> CreateView(std::shared_ptr<rhi::Texture> t) override
    {
        return std::make_shared<rhi::TextureView>(t);
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
    std::unique_ptr<rhi::Swapchain> CreateSwapchain(platform::NativeSurface, uint32_t,
                                                    uint32_t) override
    {
        return {};
    }
    std::shared_ptr<Submission> Execute(const rhi::CommandList&, uint64_t, rhi::Swapchain*) override
    {
        return std::make_shared<Work>();
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
} // namespace
int main()
{
    try
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
            const auto& p = g.Compile();
            Require(!p.passes[old.index].live && p.schedule.size() == 1,
                    "Full overwrite must cull old producer");
        }
        {
            Graph g;
            auto t = Texture(g);
            Write(g, t);
            Write(g, t, Coverage::Preserve);
            g.Export(t, rhi::State::Sampled);
            Require(g.Compile().schedule.size() == 2, "Preserving write must retain producer");
        }
        {
            Graph g;
            auto t = Texture(g);
            Write(g, t);
            auto a = Read(g, t);
            auto b = Read(g, t);
            const auto& p = g.Compile();
            Require(p.schedule.size() == 3 &&
                        std::ranges::none_of(p.edges,
                                             [&](auto& e)
                                             {
                                                 return (e.before == a.index &&
                                                         e.after == b.index) ||
                                                        (e.before == b.index && e.after == a.index);
                                             }),
                    "Read-only consumers need no mutual order");
        }
        {
            Graph g;
            auto t = Texture(g);
            Write(g, t);
            auto old = t;
            auto writer = Write(g, t);
            auto reader = Read(g, old);
            g.Export(t, rhi::State::Sampled);
            const auto& p = g.Compile();
            Require(p.schedule[1].pass == reader.index && p.schedule[2].pass == writer.index,
                    "WAR must schedule late-registered old-version read before overwrite");
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
            auto t = Texture(g);
            auto old = t;
            Write(g, t);
            Fails([&] { Write(g, old); }, "Stale");
        }
    }
    catch (const std::exception& error)
    {
        // The dedicated cases below own their expected diagnostics.
        std::cerr << error.what() << '\n';
        return 1;
    }
    try
    {
        Fails(
            []
            {
                Graph g;
                auto t = Texture(g);
                g.AddPass<Data>(
                    "range", PassKind::Compute, [&](auto& b, auto&)
                    { b.Read(t, Use::Sampled, TextureRange{1, 1, 0, 1}); }, Empty);
            },
            "mip/layer");
        Fails(
            []
            {
                Graph g;
                auto t = Texture(g);
                g.AddPass<Data>(
                    "aspect", PassKind::Compute, [&](auto& b, auto&)
                    { b.Read(t, Use::Sampled, TextureRange{0, 1, 0, 1, Aspect::Depth}); }, Empty);
            },
            "aspect");
        Fails(
            []
            {
                Graph g;
                auto t = Texture(g);
                g.AddPass<Data>(
                    "wrong kind", PassKind::Copy, [&](auto& b, auto&) { b.Read(t, Use::Sampled); },
                    Empty);
            },
            "Copy pass");
        {
            Graph g;
            auto t = Texture(g);
            g.AddPass<Data>(
                "Load", PassKind::Raster,
                [&](auto& b, auto&) { t = b.Color(t, rhi::LoadOp::Load); }, Empty);
            g.Export(t, rhi::State::Sampled);
            Fails([&] { g.Compile(); }, "initialization");
        }
        {
            Graph g;
            auto t = Texture(g);
            g.AddPass<Data>(
                "Clear", PassKind::Raster, [&](auto& b, auto&) { t = b.Color(t); }, Empty);
            g.Export(t, rhi::State::Sampled);
            Require(g.Compile().schedule.size() == 1, "Clear initializes attachment");
        }
        FakeRuntime runtime;
        Pool pool(runtime, 4, 4096);
        bool rejectedThread = false;
        std::jthread otherThread(
            [&]
            {
                try
                {
                    pool.Stats();
                }
                catch (const std::logic_error&)
                {
                    rejectedThread = true;
                }
            });
        otherThread.join();
        Require(rejectedThread, "Pool must reject access from another thread");
        auto imported = runtime.CreateTexture({8, 8, rhi::Format::Rgba8, true, true, "Import"});
        {
            Graph g;
            auto t = g.Import(imported, {rhi::State::Sampled, true, 0});
            Read(g, t, false);
            Require(g.Compile().schedule.empty(), "Import alone must not pin consumers");
        }
        {
            Graph g;
            auto t = g.Import(imported, {rhi::State::Sampled, true, 0});
            Read(g, t, false);
            Require(g.Compile({false}).schedule.size() == 1, "Disable culling uses same compiler");
        }
        {
            Graph g;
            auto t = Texture(g);
            auto output = t;
            Write(g, output);
            Fails([&] { g.Export(t, rhi::State::Sampled); }, "latest");
        }
        {
            Graph g;
            auto t = Texture(g);
            Write(g, t);
            g.Export(t, rhi::State::Sampled);
            g.Compile();
            g.Prepare(pool);
            g.Record();
            Require(pool.Stats().allocations == 1, "Allocate transient");
        }
        {
            Graph g;
            auto t = Texture(g);
            Write(g, t);
            g.Export(t, rhi::State::Sampled);
            g.Compile();
            g.Prepare(pool);
            Require(pool.Stats().reuses == 1, "Cancelled recording must return lease");
            auto submit = g.Submit();
            Fails([&] { g.Submit(); }, "once");
            submit.exports.clear();
        }
        {
            Graph g;
            auto t = Texture(g);
            Write(g, t);
            g.Export(t, rhi::State::Sampled);
            g.Compile();
            g.Prepare(pool);
            Require(pool.Stats().allocations == 2, "In-flight pool resource must not be reused");
        }
        runtime.WaitIdle();
        {
            Graph g;
            auto t = Texture(g);
            Write(g, t);
            g.Export(t, rhi::State::Sampled);
            g.Compile();
            g.Prepare(pool);
            Require(pool.Stats().reuses >= 2, "Completed lease must be reusable");
        }
        {
            Graph g;
            auto importedBuffer = runtime.CreateBuffer({128, rhi::Memory::Device, "Input"});
            auto input = g.Import(importedBuffer, {rhi::State::CopySource, true, 0});
            auto output = g.Create(rhi::BufferDesc{128, rhi::Memory::Device, "Output"});
            struct CopyData
            {
                BufferHandle input, output;
            };
            g.AddPass<CopyData>(
                "bounded copy", PassKind::Copy,
                [&](auto& b, auto& d)
                {
                    b.Read(input, Use::CopySource, BufferRange{0, 16});
                    output = b.Write(output, Use::CopyDestination);
                    d.input = input;
                    d.output = output;
                    b.SideEffect();
                },
                [](const CopyData& d, ExecuteContext& c) { c.Copy(d.input, d.output, 32); });
            g.Compile();
            g.Prepare(pool);
            Fails([&] { g.Record(); }, "declared buffer range");
        }
        {
            Pool small(runtime, 1, 1024);
            auto first = small.Acquire(rhi::TextureDesc{8, 8});
            Fails([&] { small.Acquire(rhi::TextureDesc{8, 8}); }, "pool is full");
            first.reset();
            auto different = small.Acquire(rhi::TextureDesc{16, 16});
            Require(small.Stats().cached == 1, "Evict incompatible idle allocation within bounds");
        }
        {
            Graph g;
            auto t = Texture(g);
            Write(g, t);
            auto dead = Read(g, t, false);
            auto live = Read(g, t);
            g.After(live, dead);
            const auto& p = g.Compile();
            Require(!p.passes[dead.index].live, "Ordering must not retain unused work");
        }
        {
            Graph g;
            auto a = runtime.CreateBuffer({16, rhi::Memory::Upload, "Upload"});
            Fails([&] { g.Import(a, {rhi::State::CopyDest, false, 0}); }, "import/export state");
        }
        {
            Graph g;
            auto t = Texture(g);
            g.AddPass<Data>(
                "undeclared", PassKind::Compute,
                [&](auto& b, auto& d)
                {
                    d.texture = t;
                    b.SideEffect();
                },
                Empty, [](Data& d, PrepareContext& c) { c.View(d.texture); });
            g.Compile();
            Fails([&] { g.Prepare(pool); }, "Undeclared");
        }
        {
            Graph g;
            auto t = Texture(g);
            auto stale = t;
            g.AddPass<Data>(
                "old version", PassKind::Compute,
                [&](auto& b, auto& d)
                {
                    d.texture = stale;
                    t = b.Write(t, Use::Storage);
                    b.SideEffect();
                },
                Empty, [](Data& d, PrepareContext& c) { c.View(d.texture); });
            g.Compile();
            Fails([&] { g.Prepare(pool); }, "Undeclared");
        }
        pool.Trim();
        Require(pool.Stats().cached == 0, "Trim releases completed idle allocations");
        std::cout << "M3 graph contracts passed: versions, RAW/WAR/WAW, culling, cycles, ranges, "
                     "attachments, ownership, cancellation and completion-safe pool\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
