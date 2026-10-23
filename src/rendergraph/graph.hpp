#pragma once

#include "rendergraph/context.hpp"

#include <concepts>
#include <functional>
#include <memory>
#include <source_location>
#include <string>
#include <type_traits>
#include <utility>

namespace renderlab::rhi
{
class Swapchain;
}
namespace renderlab::rendergraph
{
class Pool;
class Graph;
class PassBuilder
{
public:
    void Read(TextureHandle handle, Use use, TextureRange range = {});
    void Read(BufferHandle handle, Use use, BufferRange range = {});
    TextureHandle Write(TextureHandle previous, Use use, Coverage coverage = Coverage::Full,
                        TextureRange range = {});
    BufferHandle Write(BufferHandle previous, Use use, Coverage coverage = Coverage::Full,
                       BufferRange range = {});
    TextureHandle Color(TextureHandle previous, rhi::LoadOp load = rhi::LoadOp::Clear,
                        std::array<float, 4> clear = {.02f, .02f, .02f, 1});
    TextureHandle Depth(TextureHandle previous, rhi::LoadOp load = rhi::LoadOp::Clear);
    void SideEffect();

private:
    friend class Graph;
    PassBuilder(Graph& graph, uint32_t pass) : graph_(graph), pass_(pass) {}
    Graph& graph_;
    uint32_t pass_;
};
namespace detail
{
struct Callbacks
{
    virtual ~Callbacks() = default;
    virtual void Prepare(PrepareContext&) = 0;
    virtual void Execute(ExecuteContext&) const = 0;
};
struct NoPrepare
{
    template <class Data> void operator()(Data&, PrepareContext&) const {}
};
template <class Data, class ExecuteFn, class PrepareFn> struct PassCallbacks final : Callbacks
{
    Data data{};
    ExecuteFn execute;
    PrepareFn prepare;
    PassCallbacks(ExecuteFn execute, PrepareFn prepare)
        : execute(std::move(execute)), prepare(std::move(prepare))
    {
    }
    void Prepare(PrepareContext& context) override
    {
        prepare(data, context);
    }
    void Execute(ExecuteContext& context) const override
    {
        execute(data, context);
    }
};
} // namespace detail
/** @brief One frame, one queue, one-shot submission. Owns PassData; handles never cross graphs. */
class Graph final
{
public:
    Graph();
    ~Graph();
    Graph(const Graph&) = delete;
    Graph& operator=(const Graph&) = delete;
    TextureHandle Create(rhi::TextureDesc desc);
    BufferHandle Create(rhi::BufferDesc desc);
    TextureHandle Import(std::shared_ptr<rhi::Texture> texture, ImportInfo info);
    BufferHandle Import(std::shared_ptr<rhi::Buffer> buffer, ImportInfo info);
    void Export(TextureHandle handle, rhi::State state);
    void Export(BufferHandle handle, rhi::State state);
    template <class Data, class Setup, class ExecuteFn, class PrepareFn = detail::NoPrepare>
    PassHandle AddPass(std::string name, PassKind kind, Setup setup, ExecuteFn execute,
                       PrepareFn prepare = {},
                       std::source_location source = std::source_location::current())
    {
        static_assert(std::default_initializable<Data> && !std::is_pointer_v<Data>);
        static_assert(
            std::is_empty_v<ExecuteFn> && std::is_empty_v<PrepareFn>,
            "Deferred callbacks must be captureless; copy values into graph-owned PassData");
        auto callbacks = std::make_unique<detail::PassCallbacks<Data, ExecuteFn, PrepareFn>>(
            std::move(execute), std::move(prepare));
        auto* data = &callbacks->data;
        auto builder = BeginPass(std::move(name), kind, std::move(callbacks), source);
        try
        {
            setup(builder, *data);
        }
        catch (...)
        {
            Poison();
            throw;
        }
        return EndPass(builder);
    }
    /// Ordering only: does not retain an otherwise culled pass.
    void After(PassHandle pass, PassHandle predecessor);
    void Before(PassHandle pass, PassHandle successor)
    {
        After(successor, pass);
    }
    const Plan& Compile(CompileOptions options = {});
    void Prepare(Pool& pool);
    void Record();
    Submission Submit(rhi::Swapchain* presentation = nullptr);
    const Plan& GetPlan() const;

private:
    friend class PassBuilder;
    friend class PrepareContext;
    friend class ExecuteContext;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    PassBuilder BeginPass(std::string name, PassKind kind,
                          std::unique_ptr<detail::Callbacks> callbacks,
                          std::source_location source);
    PassHandle EndPass(PassBuilder& builder);
    void Poison();
};
} // namespace renderlab::rendergraph
