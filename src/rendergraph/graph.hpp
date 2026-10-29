#pragma once

#include "rendergraph/context.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <variant>

namespace renderlab::rhi
{
class Swapchain;
}
namespace renderlab::rendergraph
{
class Graph;
class Pool;
namespace detail
{
using Callback =
    std::variant<std::monostate, std::function<void(RasterContext&)>,
                 std::function<void(ComputeContext&)>, std::function<void(CopyContext&)>,
                 std::function<void(RayTracingContext&)>>;
}
/** @brief Immediate resource declarations. Execute seals the builder; never retain it across
 * graphs. */
class PassBuilder
{
public:
    PassBuilder(const PassBuilder&) = delete;
    PassBuilder& operator=(const PassBuilder&) = delete;
    PassBuilder(PassBuilder&&) = default;
    TextureHandle ReadTexture(TextureHandle handle, std::optional<Use> use = {},
                              TextureRange range = {});
    TextureHandle ReadTexture(std::string_view name, std::optional<Use> use = {},
                              TextureRange range = {});
    BufferHandle ReadBuffer(BufferHandle handle, std::optional<Use> use = {},
                            BufferRange range = {});
    BufferHandle ReadBuffer(std::string_view name, std::optional<Use> use = {},
                            BufferRange range = {});
    TextureHandle WriteTexture(TextureHandle handle, std::optional<Use> use = {},
                               TextureRange range = {});
    TextureHandle WriteTexture(std::string_view name, std::optional<Use> use = {},
                               TextureRange range = {});
    BufferHandle WriteBuffer(BufferHandle handle, std::optional<Use> use = {},
                             BufferRange range = {});
    BufferHandle WriteBuffer(std::string_view name, std::optional<Use> use = {},
                             BufferRange range = {});
    TextureHandle ReadWriteTexture(TextureHandle handle, TextureRange range = {});
    TextureHandle ReadWriteTexture(std::string_view name, TextureRange range = {});
    BufferHandle ReadWriteBuffer(BufferHandle handle, BufferRange range = {});
    BufferHandle ReadWriteBuffer(std::string_view name, BufferRange range = {});
    BufferHandle ReadWriteBuffer(BufferHandle handle, Use use, BufferRange range = {});
    BufferHandle ReadWriteBuffer(std::string_view name, Use use, BufferRange range = {});
    void SideEffect();
    PassHandle Handle() const;

protected:
    PassBuilder(Graph& graph, uint32_t pass) : graph_(graph), pass_(pass) {}
    PassHandle Finish(detail::Callback callback);
    TextureHandle Write(TextureHandle, Use, Coverage, TextureRange);
    BufferHandle Write(BufferHandle, Use, Coverage, BufferRange);
    Use DefaultRead(bool texture) const;
    Use DefaultWrite() const;
    TextureHandle Color(TextureHandle, rhi::LoadOp, std::array<float, 4>);
    TextureHandle Depth(TextureHandle, AttachmentAspect, AttachmentAspect, uint32_t);
    Graph& graph_;
    uint32_t pass_;
};
class RasterPassBuilder final : public PassBuilder
{
public:
    TextureHandle ColorTarget(TextureHandle handle, rhi::LoadOp load = rhi::LoadOp::Clear,
                              std::array<float, 4> clear = {.02f, .02f, .02f, 1});
    TextureHandle ColorTarget(std::string_view name, rhi::LoadOp load = rhi::LoadOp::Clear,
                              std::array<float, 4> clear = {.02f, .02f, .02f, 1});
    TextureHandle DepthTarget(TextureHandle handle, rhi::LoadOp load = rhi::LoadOp::Clear,
                              AttachmentAccess access = AttachmentAccess::ReadWrite);
    TextureHandle DepthTarget(std::string_view name, rhi::LoadOp load = rhi::LoadOp::Clear,
                              AttachmentAccess access = AttachmentAccess::ReadWrite);
    /// @brief Explicit depth/stencil load operations and permissions; requires a stencil format.
    TextureHandle DepthStencilTarget(TextureHandle handle, AttachmentAspect depth,
                                     AttachmentAspect stencil, uint32_t clearStencil = 0);
    TextureHandle DepthStencilTarget(std::string_view name, AttachmentAspect depth,
                                     AttachmentAspect stencil, uint32_t clearStencil = 0);
    PassHandle Execute(std::function<void(RasterContext&)> callback);

private:
    friend class Graph;
    RasterPassBuilder(Graph& g, uint32_t p) : PassBuilder(g, p) {}
};
class ComputePassBuilder final : public PassBuilder
{
public:
    /// Opt-in feeder work: Upload reads and transient Storage buffers only. Validated at Compile.
    void AllowAsyncCompute();
    PassHandle Execute(std::function<void(ComputeContext&)> callback);

private:
    friend class Graph;
    ComputePassBuilder(Graph& g, uint32_t p) : PassBuilder(g, p) {}
};
class CopyPassBuilder final : public PassBuilder
{
public:
    PassHandle Execute(std::function<void(CopyContext&)> callback);

private:
    friend class Graph;
    CopyPassBuilder(Graph& g, uint32_t p) : PassBuilder(g, p) {}
};
class RayTracingPassBuilder final : public PassBuilder
{
public:
    PassHandle Execute(std::function<void(RayTracingContext&)> callback);

private:
    friend class Graph;
    RayTracingPassBuilder(Graph& g, uint32_t p) : PassBuilder(g, p) {}
};
/** @brief One-frame resource graph. Names resolve during building; callbacks own captured values.
 */
class Graph final
{
public:
    Graph();
    ~Graph();
    Graph(const Graph&) = delete;
    Graph& operator=(const Graph&) = delete;
    TextureHandle CreateTexture(std::string name, rhi::TextureDesc desc);
    BufferHandle CreateBuffer(std::string name, rhi::BufferDesc desc);
    TextureHandle ImportTexture(std::string name, std::shared_ptr<rhi::Texture>, ImportInfo);
    BufferHandle ImportBuffer(std::string name, std::shared_ptr<rhi::Buffer>, ImportInfo);
    TextureHandle GetTexture(std::string_view name) const;
    BufferHandle GetBuffer(std::string_view name) const;
    void SetTexture(std::string_view name, TextureHandle handle);
    void SetBuffer(std::string_view name, BufferHandle handle);
    rhi::TextureDesc GetTextureDesc(TextureHandle handle) const;
    rhi::BufferDesc GetBufferDesc(BufferHandle handle) const;
    void Export(TextureHandle, rhi::State);
    void Export(BufferHandle, rhi::State);
    void Export(std::string_view name, rhi::State);
    RasterPassBuilder AddRasterPass(std::string name,
                                    std::source_location source = std::source_location::current());
    ComputePassBuilder
    AddComputePass(std::string name, std::source_location source = std::source_location::current());
    CopyPassBuilder AddCopyPass(std::string name,
                                std::source_location source = std::source_location::current());
    RayTracingPassBuilder
    AddRayTracingPass(std::string name,
                      std::source_location source = std::source_location::current());
    uint32_t PassCount() const;
    PassHandle GetPass(uint32_t index) const;
    /// Ordering only; never retains an otherwise dead pass.
    void After(PassHandle pass, PassHandle predecessor);
    void Before(PassHandle pass, PassHandle successor)
    {
        After(successor, pass);
    }
    const Plan& Compile(CompileOptions options = {});
    Submission Execute(Pool& pool, rhi::Swapchain* presentation = nullptr);
    const Plan& GetPlan() const;

private:
    friend class PassBuilder;
    friend class RasterPassBuilder;
    friend class ComputePassBuilder;
    friend class BindingContext;
    friend class RasterContext;
    friend class ComputeContext;
    friend class RayTracingContext;
    friend class CopyContext;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    uint32_t BeginPass(std::string name, PassKind, std::source_location);
    void Prepare(Pool&);
    void Record();
    Submission Submit(rhi::Swapchain*);
    void Poison();
};
} // namespace renderlab::rendergraph
