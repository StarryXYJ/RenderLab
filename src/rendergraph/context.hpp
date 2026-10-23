#pragma once

#include "rendergraph/types.hpp"

#include <memory>

namespace renderlab::rhi
{
class Runtime;
class CommandList;
} // namespace renderlab::rhi
namespace renderlab::rendergraph
{
class Graph;
/** @brief Scoped resolution for declared resources. Not copyable; never retain the context. */
class PrepareContext
{
public:
    PrepareContext(const PrepareContext&) = delete;
    PrepareContext& operator=(const PrepareContext&) = delete;
    std::shared_ptr<rhi::Texture> Texture(TextureHandle handle) const;
    std::shared_ptr<rhi::TextureView> View(TextureHandle handle) const;
    std::shared_ptr<rhi::Buffer> Buffer(BufferHandle handle) const;
    rhi::Runtime& Device() const;

private:
    friend class Graph;
    PrepareContext(Graph& graph, uint32_t pass) : graph_(graph), pass_(pass) {}
    Graph& graph_;
    uint32_t pass_;
};
/** @brief Checked command facade. No native commands, barriers or runtime allocation escape. */
class ExecuteContext
{
public:
    ExecuteContext(const ExecuteContext&) = delete;
    ExecuteContext& operator=(const ExecuteContext&) = delete;
    void Bind(std::shared_ptr<rhi::Pipeline> pipeline);
    void Bind(std::shared_ptr<rhi::BindingGroup> group);
    void Geometry(BufferHandle vertices, BufferHandle indices = {});
    void Draw(uint32_t count, uint32_t first = 0);
    void DrawIndexed(uint32_t count, uint32_t first = 0);
    void DrawIndirect(BufferHandle arguments, uint64_t offset = 0);
    void Dispatch(uint32_t x, uint32_t y, uint32_t z = 1);
    void Copy(BufferHandle source, BufferHandle destination, uint64_t size,
              uint64_t sourceOffset = 0, uint64_t destinationOffset = 0);
    void CopyTexture(BufferHandle buffer, TextureHandle texture, uint32_t rowPitch, bool upload,
                     uint64_t offset = 0);

private:
    friend class Graph;
    ExecuteContext(Graph& graph, uint32_t pass, rhi::CommandList& commands)
        : graph_(graph), pass_(pass), commands_(commands)
    {
    }
    Graph& graph_;
    uint32_t pass_;
    rhi::CommandList& commands_;
};
} // namespace renderlab::rendergraph
