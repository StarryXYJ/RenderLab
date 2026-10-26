#pragma once

#include "rendergraph/types.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace renderlab::renderer
{
struct TextureBinding
{
    uint32_t slot;
    std::string texture;
    uint32_t space = 0;
};
struct BufferBinding
{
    uint32_t slot;
    std::string buffer;
    rendergraph::Use use = rendergraph::Use::ShaderRead;
    rendergraph::BufferRange range;
    uint32_t stride = 0, space = 0;
};
struct SamplerBinding
{
    uint32_t slot;
    std::shared_ptr<rhi::Sampler> sampler;
    uint32_t space = 0;
};
enum class DrawKind
{
    Vertices,
    Indexed,
    Indirect
};
/** @brief Prepared draw inputs. Resource names resolve in the current graph during Feature::Build.
 */
struct DrawPacket
{
    std::shared_ptr<rhi::Pipeline> pipeline;
    std::string vertices, indices, indirect;
    std::vector<TextureBinding> textures;
    std::vector<BufferBinding> buffers;
    std::vector<SamplerBinding> samplers;
    DrawKind kind = DrawKind::Vertices;
    uint32_t count = 0, first = 0;
    uint64_t indirectOffset = 0;
    uint32_t stencilReference = 0;
    float distance = 0;
    int32_t queue = 2000;
};
/** @brief Per-frame immutable inputs; no graph handles or ownership of graph outputs. */
struct RenderFrame
{
    std::vector<DrawPacket> opaque, transparent;
    std::vector<DrawPacket> shadowCasters;
    uint32_t shadowResolution = 1;
    rhi::Format shadowDepthFormat = rhi::Format::D32;
    std::array<float, 4> clearColor = {.02f, .02f, .02f, 1};
};
} // namespace renderlab::renderer
