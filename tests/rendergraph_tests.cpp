#include "renderer/forward.hpp"
#include "renderer/pipeline.hpp"
#include "rendergraph/graph.hpp"
#include "rendergraph/pool.hpp"
#include "rhi/commands.hpp"
#include <functional>

#include <algorithm>
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
    std::span<std::byte> Map() override
    {
        return {};
    }
};
struct FakeRuntime : rhi::Runtime
{
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
    std::shared_ptr<rhi::TextureView> CreateView(std::shared_ptr<rhi::Texture> t) override
    {
        if (auto cached = t->defaultView.lock())
            return cached;
        auto view = std::make_shared<rhi::TextureView>(t);
        t->defaultView = view;
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
    std::unique_ptr<rhi::Swapchain> CreateSwapchain(platform::NativeSurface, uint32_t,
                                                    uint32_t) override
    {
        return {};
    }
    std::shared_ptr<Submission> Execute(const rhi::CommandList& commands, uint64_t,
                                        rhi::Swapchain*) override
    {
        for (const auto& command : commands.GetCommands())
            if (const auto* draw = std::get_if<rhi::Draw>(&command))
                drawnVertices.push_back(draw->first);
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

std::shared_ptr<rhi::Pipeline> Compute(FakeRuntime& runtime, rhi::BindingKind kind)
{
    rhi::PipelineDesc desc;
    desc.layout = runtime.CreateLayout({{0, kind}});
    desc.compute = std::make_shared<rhi::ShaderModule>(&runtime, rhi::ShaderStage::Compute,
                                                       std::vector<std::byte>{}, "CSMain");
    return std::make_shared<rhi::Pipeline>(&runtime, std::move(desc));
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
        g.Compile();
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
    Fails([&] { graph.Execute(pool); }, "32 live passes");
    Submission::GpuProfile profile;
    Require(profile.Read(runtime).empty(), "Disabled profile must not access readback memory");
    profile.ticks = runtime.CreateBuffer({16, rhi::Memory::Readback, "Profile"});
    profile.names = {"test"};
    profile.completion = 1;
    Fails([&] { profile.Read(runtime); }, "completed submission");
}
} // namespace
int main()
{
    try
    {
        Dependencies();
        NamesAndBuilders();
        AccessValidation();
        Lifecycle();
        RecordingValidation();
        FeatureCreationContracts();
        ForwardContracts();
        StencilContracts();
        ProfilingContracts();
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
