#pragma once

#include "rendergraph/types.hpp"

#include <memory>
#include <vector>

namespace renderlab::rhi
{
class CommandList;
}
namespace renderlab::rendergraph
{
class Graph;
/** @brief Handle-based bindings shared by raster/compute. No native resource or device escape. */
class BindingContext
{
public:
    BindingContext(const BindingContext&) = delete;
    BindingContext& operator=(const BindingContext&) = delete;
    void Bind(std::shared_ptr<rhi::Pipeline> pipeline);
    void BindTexture(uint32_t slot, TextureHandle handle, uint32_t space = 0,
                     uint32_t arrayElement = 0);
    void BindBuffer(uint32_t slot, BufferHandle handle, uint64_t offset = 0, uint64_t size = 0,
                    uint32_t stride = 0, uint32_t space = 0);
    void BindSampler(uint32_t slot, std::shared_ptr<rhi::Sampler> sampler, uint32_t space = 0);
    void
    BindAccelerationStructure(uint32_t slot,
                              std::shared_ptr<rhi::AccelerationStructure> accelerationStructure,
                              BufferHandle storage, uint32_t space = 0);

protected:
    BindingContext(Graph& graph, uint32_t pass, rhi::CommandList& commands)
        : graph_(graph), pass_(pass), commands_(commands)
    {
    }
    void SetBinding(rhi::Binding binding);
    void FlushBindings();
    Graph& graph_;
    uint32_t pass_;
    rhi::CommandList& commands_;
    std::shared_ptr<rhi::Pipeline> pipeline_;
    std::vector<rhi::Binding> bindings_;
    bool dirty_ = true;
};
class RasterContext final : public BindingContext
{
public:
    void StencilReference(uint32_t reference);
    void Geometry(BufferHandle vertices, BufferHandle indices = {});
    void Draw(uint32_t count, uint32_t first = 0, uint32_t instances = 1,
              uint32_t firstInstance = 0);
    void DrawIndexed(uint32_t count, uint32_t first = 0, uint32_t instances = 1,
                     uint32_t firstInstance = 0, int32_t vertexOffset = 0);
    void DrawIndirect(BufferHandle arguments, uint64_t offset = 0, uint32_t maxCount = 1,
                      bool indexed = false, BufferHandle count = {}, uint64_t countOffset = 0);

private:
    friend class Graph;
    RasterContext(Graph& g, uint32_t p, rhi::CommandList& c) : BindingContext(g, p, c) {}
};
class ComputeContext final : public BindingContext
{
public:
    void Dispatch(uint32_t x, uint32_t y, uint32_t z = 1);
    void
    BuildAccelerationStructure(std::shared_ptr<rhi::AccelerationStructure> accelerationStructure,
                               BufferHandle storage, BufferHandle scratch, bool update = false);

private:
    friend class Graph;
    ComputeContext(Graph& g, uint32_t p, rhi::CommandList& c) : BindingContext(g, p, c) {}
};
class RayTracingContext final : public BindingContext
{
public:
    void TraceRays(std::shared_ptr<rhi::ShaderTable> table, BufferHandle records, uint32_t width,
                   uint32_t height, uint32_t depth = 1);

private:
    friend class Graph;
    RayTracingContext(Graph& g, uint32_t p, rhi::CommandList& c) : BindingContext(g, p, c) {}
};
/** @brief Copy-only recording, restricted to the current pass declarations. */
class CopyContext final
{
public:
    CopyContext(const CopyContext&) = delete;
    CopyContext& operator=(const CopyContext&) = delete;
    void Copy(BufferHandle source, BufferHandle destination, uint64_t size,
              uint64_t sourceOffset = 0, uint64_t destinationOffset = 0);
    void CopyTexture(BufferHandle buffer, TextureHandle texture, uint32_t rowPitch, bool upload,
                     uint64_t offset = 0);
    void CopyTexture(TextureHandle source, TextureHandle destination);

private:
    friend class Graph;
    CopyContext(Graph& g, uint32_t p, rhi::CommandList& c) : graph_(g), pass_(p), commands_(c) {}
    Graph& graph_;
    uint32_t pass_;
    rhi::CommandList& commands_;
};
} // namespace renderlab::rendergraph
