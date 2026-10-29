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
    uint32_t arrayElement = 0;
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
    Indirect,
    IndexedIndirect,
    IndexedSequence
};
/** @brief Prepared draw inputs. Resource names resolve in the current graph during Feature::Build.
 */
struct DrawPacket
{
    std::shared_ptr<rhi::Pipeline> pipeline;
    std::string vertices, indices, indirect, indirectCount;
    std::vector<TextureBinding> textures;
    std::vector<BufferBinding> buffers;
    std::vector<SamplerBinding> samplers;
    /// CPU reference commands sharing one immutable binding snapshot.
    std::vector<rhi::DrawIndexedArguments> indexedDraws;
    DrawKind kind = DrawKind::Vertices;
    uint32_t count = 0, first = 0;
    uint64_t indirectOffset = 0;
    uint64_t indirectCountOffset = 0;
    uint32_t indirectMaxCount = 1, instances = 1, firstInstance = 0;
    int32_t vertexOffset = 0;
    uint32_t stencilReference = 0;
    float distance = 0;
    int32_t queue = 2000;
};
/** @brief Per-frame immutable inputs; no graph handles or ownership of graph outputs. */
struct RenderFrame
{
    struct RayTracingInputs
    {
        std::shared_ptr<rhi::AccelerationStructure> scene;
        std::string storage;
        std::shared_ptr<rhi::Pipeline> shadowPipeline;
        BufferBinding parameters;
        std::shared_ptr<rhi::ShaderTable> reflectionTable;
        std::shared_ptr<rhi::Pipeline> historyPipeline, statisticsPipeline, combinePipeline,
            denoisePipeline;
        std::shared_ptr<rhi::Pipeline> temporalPipeline;
        // Prepare, three variance-guided scales and material/display resolve.
        std::array<std::shared_ptr<rhi::Pipeline>, 5> reconstructionPipelines;
        bool reflectionDiagnostics = false;
        BufferBinding temporalParameters;
        std::array<std::string, 4> previousTemporal, temporalOutputs;
        BufferBinding reflectionParameters;
        std::vector<BufferBinding> surfaces;
        std::vector<TextureBinding> reflectionTextures;
        std::shared_ptr<rhi::Sampler> sampler;
        std::string previousRadiance, historyRadiance, previousStatistics, historyStatistics;
    } rayTracing;
    std::vector<DrawPacket> opaque, transparent;
    std::vector<DrawPacket> gbuffer;
    DrawPacket deferredLighting;
    std::vector<DrawPacket> shadowCasters;
    uint32_t shadowResolution = 1;
    rhi::Format shadowDepthFormat = rhi::Format::D32;
    std::array<float, 4> clearColor = {.02f, .02f, .02f, 1};
};
} // namespace renderlab::renderer
