#pragma once

#include "rhi/commands.hpp"

#include <array>
#include <span>
#include <type_traits>

namespace renderlab::rendergraph
{
struct CommandStatistics
{
    uint64_t direct = 0, indexed = 0, indirect = 0, indirectCapacity = 0;
    uint64_t batches = 0, directInstances = 0, dispatch = 0, threadGroups = 0;
    uint64_t traceRays = 0, rays = 0, asBuilds = 0, asUpdates = 0, copyBytes = 0;
    bool indirectInstancesUnknown = false;
    std::array<uint32_t, 3> lastDispatch{}, lastTrace{};
};

/** @brief Counts accepted command packets at their emission boundary, before native recording.
 * A batch is a contiguous draw run sharing pipeline, bindings and geometry. Failed submissions
 * never publish these counters. Parallel native chunks need no shared counter or per-draw lock.
 */
inline CommandStatistics CountCommands(std::span<const rhi::Command> commands)
{
    CommandStatistics result;
    bool batchChanged = true;
    const void* pipeline = nullptr;
    const void* bindings = nullptr;
    const void* vertex = nullptr;
    const void* index = nullptr;
    uint32_t stencil = 0;
    for (const auto& command : commands)
        std::visit(
            [&](const auto& value)
            {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, rhi::SetPipeline>)
                {
                    batchChanged |= pipeline != value.pipeline.get();
                    pipeline = value.pipeline.get();
                }
                else if constexpr (std::is_same_v<T, rhi::SetBindings>)
                {
                    batchChanged |= bindings != value.group.get();
                    bindings = value.group.get();
                }
                else if constexpr (std::is_same_v<T, rhi::SetGeometry>)
                {
                    batchChanged |= vertex != value.vertex.get() || index != value.index.get();
                    vertex = value.vertex.get();
                    index = value.index.get();
                }
                else if constexpr (std::is_same_v<T, rhi::SetStencilReference>)
                {
                    batchChanged |= stencil != value.reference;
                    stencil = value.reference;
                }
                else if constexpr (std::is_same_v<T, rhi::Draw>)
                {
                    ++result.direct;
                    result.indexed += value.indexed ? 1 : 0;
                    result.directInstances += value.instances;
                    result.batches += batchChanged ? 1 : 0;
                    batchChanged = false;
                }
                else if constexpr (std::is_same_v<T, rhi::DrawIndirect>)
                {
                    ++result.indirect;
                    result.indirectCapacity += value.maxCount;
                    result.indirectInstancesUnknown = true;
                    result.batches += batchChanged ? 1 : 0;
                    batchChanged = false;
                }
                else if constexpr (std::is_same_v<T, rhi::Dispatch>)
                {
                    ++result.dispatch;
                    result.threadGroups += uint64_t(value.x) * value.y * value.z;
                    result.lastDispatch = {value.x, value.y, value.z};
                }
                else if constexpr (std::is_same_v<T, rhi::TraceRays>)
                {
                    ++result.traceRays;
                    result.rays += uint64_t(value.width) * value.height * value.depth;
                    result.lastTrace = {value.width, value.height, value.depth};
                }
                else if constexpr (std::is_same_v<T, rhi::BuildAccelerationStructure>)
                {
                    result.asBuilds += value.update ? 0 : 1;
                    result.asUpdates += value.update ? 1 : 0;
                }
                else if constexpr (std::is_same_v<T, rhi::BufferCopy>)
                    result.copyBytes += value.size;
                else if constexpr (std::is_same_v<T, rhi::TextureCopy>)
                    result.copyBytes += uint64_t(value.rowPitch) * value.texture->desc.height;
                else if constexpr (std::is_same_v<T, rhi::TextureToTextureCopy>)
                    result.copyBytes += uint64_t(value.source->desc.width) *
                                        value.source->desc.height *
                                        rhi::FormatBytes(value.source->desc.format);
            },
            command);
    return result;
}
} // namespace renderlab::rendergraph
