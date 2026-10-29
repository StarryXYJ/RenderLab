#include "rhi/vulkan/vulkan_runtime.hpp"

#include "rhi/commands.hpp"
#include "rhi/vulkan/vulkan_objects.hpp"
#include "rhi/vulkan/vulkan_swapchain.hpp"

#include <algorithm>
#include <type_traits>

namespace renderlab::rhi::vulkan
{
std::shared_ptr<Runtime::Submission> VulkanRuntime::Execute(const CommandList& commands,
                                                            uint64_t point, Swapchain* presentation)
{
    const auto ranges = commands.RecordingRanges();
    const bool async = std::ranges::any_of(ranges, [](const auto& range)
                                           { return range.queue == QueueType::Compute; });
    if (async)
        EnableAsyncCompute();
    struct Batch final : Submission
    {
        std::vector<std::shared_ptr<Work>> contexts;
    };
    auto batch = std::make_shared<Batch>();
    batch->contexts.reserve(ranges.size());
    try
    {
        for (size_t i = 0; i < ranges.size(); ++i)
        {
            std::shared_ptr<Work> work;
            const auto reusable =
                std::ranges::find_if(contexts_, [](const auto& w) { return w.use_count() == 1; });
            if (reusable != contexts_.end())
            {
                work = *reusable;
                batch->contexts.push_back(work);
                Check(vkResetCommandPool(device_, work->pool, 0), "Reset retired command pool");
            }
            else
            {
                work = std::make_shared<Work>();
                batch->contexts.push_back(work);
                work->device = device_;
                VkCommandPoolCreateInfo pool{};
                pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                pool.queueFamilyIndex = family_;
                pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
                Check(vkCreateCommandPool(device_, &pool, nullptr, &work->pool), "CommandPool");
                VkCommandBufferAllocateInfo allocate{};
                allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                allocate.commandPool = work->pool;
                allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                allocate.commandBufferCount = 1;
                Check(vkAllocateCommandBuffers(device_, &allocate, &work->list), "CommandBuffer");
                VkQueryPoolCreateInfo query{};
                query.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
                query.queryType = VK_QUERY_TYPE_TIMESTAMP;
                query.queryCount = MaxTimestampSlots;
                Check(vkCreateQueryPool(device_, &query, nullptr, &work->queries),
                      "Timestamp QueryPool");
                contexts_.push_back(work);
            }
        }
        const auto queries = batch->contexts.front()->queries;
        RecordNative(
            ranges.size(),
            [&](size_t index)
            {
                const auto& work = batch->contexts[index];
                auto list = work->list;
                VkCommandBufferBeginInfo begin{};
                begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                Check(vkBeginCommandBuffer(list, &begin), "BeginCommandBuffer");
                if (index == 0)
                    vkCmdResetQueryPool(list, queries, 0, MaxTimestampSlots);
                VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
                VkPipelineLayout layout = VK_NULL_HANDLE;
                const auto first = ranges[index].first, last = ranges[index].last;
                for (size_t packet = first; packet < last; ++packet)
                    std::visit(
                        [&](const auto& c)
                        {
                            using T = std::decay_t<decltype(c)>;
                            if constexpr (std::is_same_v<T, BufferBarrier>)
                            {
                                auto before = NativeState(c.before), after = NativeState(c.after);
                                VkBufferMemoryBarrier2 b{};
                                b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                                b.srcStageMask = before.stage;
                                b.srcAccessMask = before.access;
                                b.dstStageMask = after.stage;
                                b.dstAccessMask = after.access;
                                b.srcQueueFamilyIndex = b.dstQueueFamilyIndex =
                                    VK_QUEUE_FAMILY_IGNORED;
                                b.buffer = As<VkBufferObject>(c.buffer).native;
                                b.size = VK_WHOLE_SIZE;
                                VkDependencyInfo dependency{};
                                dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                                dependency.bufferMemoryBarrierCount = 1;
                                dependency.pBufferMemoryBarriers = &b;
                                vkCmdPipelineBarrier2(list, &dependency);
                            }
                            else if constexpr (std::is_same_v<T, TextureBarrier>)
                            {
                                auto before = NativeState(c.before), after = NativeState(c.after);
                                VkImageMemoryBarrier2 b{};
                                b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
                                b.srcStageMask = before.stage;
                                b.srcAccessMask = before.access;
                                b.dstStageMask = after.stage;
                                b.dstAccessMask = after.access;
                                b.oldLayout = before.layout;
                                b.newLayout = after.layout;
                                b.srcQueueFamilyIndex = b.dstQueueFamilyIndex =
                                    VK_QUEUE_FAMILY_IGNORED;
                                b.image = As<VkTexture>(c.texture).native;
                                b.subresourceRange = {
                                    VkImageAspectFlags(IsDepthFormat(c.texture->desc.format)
                                                           ? VK_IMAGE_ASPECT_DEPTH_BIT |
                                                                 (HasStencil(c.texture->desc.format)
                                                                      ? VK_IMAGE_ASPECT_STENCIL_BIT
                                                                      : 0)
                                                           : VK_IMAGE_ASPECT_COLOR_BIT),
                                    0, 1, 0, 1};
                                VkDependencyInfo dependency{};
                                dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                                dependency.imageMemoryBarrierCount = 1;
                                dependency.pImageMemoryBarriers = &b;
                                vkCmdPipelineBarrier2(list, &dependency);
                            }
                            else if constexpr (std::is_same_v<T, BufferCopy>)
                            {
                                VkBufferCopy copy{c.sourceOffset, c.destinationOffset, c.size};
                                vkCmdCopyBuffer(list, As<VkBufferObject>(c.source).native,
                                                As<VkBufferObject>(c.destination).native, 1, &copy);
                            }
                            else if constexpr (std::is_same_v<T, TextureCopy>)
                            {
                                VkBufferImageCopy copy{};
                                copy.bufferOffset = c.offset;
                                copy.bufferRowLength =
                                    c.rowPitch / FormatBytes(c.texture->desc.format);
                                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                                copy.imageExtent = {c.texture->desc.width, c.texture->desc.height,
                                                    1};
                                if (c.upload)
                                    vkCmdCopyBufferToImage(
                                        list, As<VkBufferObject>(c.buffer).native,
                                        As<VkTexture>(c.texture).native,
                                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                                else
                                    vkCmdCopyImageToBuffer(list, As<VkTexture>(c.texture).native,
                                                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                           As<VkBufferObject>(c.buffer).native, 1,
                                                           &copy);
                            }
                            else if constexpr (std::is_same_v<T, TextureToTextureCopy>)
                            {
                                VkImageCopy copy{};
                                copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                                copy.dstSubresource = copy.srcSubresource;
                                copy.extent = {c.source->desc.width, c.source->desc.height, 1};
                                vkCmdCopyImage(list, As<VkTexture>(c.source).native,
                                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                               As<VkTexture>(c.destination).native,
                                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                            }
                            else if constexpr (std::is_same_v<T, BeginRendering>)
                            {
                                std::vector<VkRenderingAttachmentInfo> colors(c.colors.size());
                                for (size_t i = 0; i < c.colors.size(); ++i)
                                {
                                    auto& color = colors[i];
                                    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                                    color.imageView = As<VkView>(c.colors[i]).native;
                                    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                                    color.loadOp = c.colorLoad == LoadOp::Clear
                                                       ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                                   : c.colorLoad == LoadOp::Load
                                                       ? VK_ATTACHMENT_LOAD_OP_LOAD
                                                       : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                                    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                                    std::copy(c.clear.begin(), c.clear.end(),
                                              color.clearValue.color.float32);
                                }
                                VkRenderingAttachmentInfo depth{};
                                depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                                if (c.depth)
                                {
                                    depth.imageView = As<VkView>(c.depth).native;
                                    depth.imageLayout =
                                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                                    depth.loadOp = c.depthLoad == LoadOp::Clear
                                                       ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                                   : c.depthLoad == LoadOp::Load
                                                       ? VK_ATTACHMENT_LOAD_OP_LOAD
                                                       : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                                    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                                    depth.clearValue.depthStencil.depth = 0;
                                }
                                auto stencil = depth;
                                const bool hasStencil =
                                    c.depth && HasStencil(c.depth->texture->desc.format);
                                stencil.loadOp = c.stencilLoad == LoadOp::Clear
                                                     ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                                 : c.stencilLoad == LoadOp::Load
                                                     ? VK_ATTACHMENT_LOAD_OP_LOAD
                                                     : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                                stencil.clearValue.depthStencil.stencil = c.clearStencil;
                                const auto& d = c.colors.front()->texture->desc;
                                VkRenderingInfo rendering{};
                                rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                                rendering.renderArea.extent = {d.width, d.height};
                                rendering.layerCount = 1;
                                rendering.colorAttachmentCount =
                                    static_cast<uint32_t>(colors.size());
                                rendering.pColorAttachments = colors.data();
                                rendering.pDepthAttachment = c.depth ? &depth : nullptr;
                                rendering.pStencilAttachment = hasStencil ? &stencil : nullptr;
                                vkCmdBeginRendering(list, &rendering);
                                VkViewport viewport{
                                    0, float(d.height), float(d.width), -float(d.height), 0, 1};
                                VkRect2D scissor{{0, 0}, {d.width, d.height}};
                                vkCmdSetViewport(list, 0, 1, &viewport);
                                vkCmdSetScissor(list, 0, 1, &scissor);
                            }
                            else if constexpr (std::is_same_v<T, EndRendering>)
                                vkCmdEndRendering(list);
                            else if constexpr (std::is_same_v<T, SetPipeline>)
                            {
                                bindPoint = c.pipeline->desc.rayGeneration
                                                ? VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR
                                            : c.pipeline->desc.compute
                                                ? VK_PIPELINE_BIND_POINT_COMPUTE
                                                : VK_PIPELINE_BIND_POINT_GRAPHICS;
                                layout = As<VkLayout>(c.pipeline->desc.layout).native;
                                vkCmdBindPipeline(list, bindPoint,
                                                  As<VkPipelineObject>(c.pipeline).native);
                                if (bindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS)
                                    vkCmdSetStencilReference(list, VK_STENCIL_FACE_FRONT_AND_BACK,
                                                             0);
                            }
                            else if constexpr (std::is_same_v<T, SetStencilReference>)
                                vkCmdSetStencilReference(list, VK_STENCIL_FACE_FRONT_AND_BACK,
                                                         c.reference);
                            else if constexpr (std::is_same_v<T, SetBindings>)
                                vkCmdBindDescriptorSets(list, bindPoint, layout, 0, 4,
                                                        As<VkGroup>(c.group).native.data(), 0,
                                                        nullptr);
                            else if constexpr (std::is_same_v<T, SetGeometry>)
                            {
                                VkDeviceSize offset = 0;
                                vkCmdBindVertexBuffers(
                                    list, 0, 1, &As<VkBufferObject>(c.vertex).native, &offset);
                                if (c.index)
                                    vkCmdBindIndexBuffer(list, As<VkBufferObject>(c.index).native,
                                                         0, VK_INDEX_TYPE_UINT32);
                            }
                            else if constexpr (std::is_same_v<T, Draw>)
                            {
                                if (c.indexed)
                                    vkCmdDrawIndexed(list, c.count, c.instances, c.first,
                                                     c.vertexOffset, c.firstInstance);
                                else
                                    vkCmdDraw(list, c.count, c.instances, c.first, c.firstInstance);
                            }
                            else if constexpr (std::is_same_v<T, DrawIndirect>)
                            {
                                const auto arguments = As<VkBufferObject>(c.arguments).native;
                                if (c.countBuffer)
                                {
                                    const auto count = As<VkBufferObject>(c.countBuffer).native;
                                    if (c.indexed)
                                        vkCmdDrawIndexedIndirectCount(
                                            list, arguments, c.offset, count, c.countOffset,
                                            c.maxCount, sizeof(DrawIndexedArguments));
                                    else
                                        vkCmdDrawIndirectCount(list, arguments, c.offset, count,
                                                               c.countOffset, c.maxCount,
                                                               sizeof(DrawArguments));
                                }
                                else if (c.indexed)
                                    vkCmdDrawIndexedIndirect(list, arguments, c.offset, c.maxCount,
                                                             sizeof(DrawIndexedArguments));
                                else
                                    vkCmdDrawIndirect(list, arguments, c.offset, c.maxCount,
                                                      sizeof(DrawArguments));
                            }
                            else if constexpr (std::is_same_v<T, Dispatch>)
                                vkCmdDispatch(list, c.x, c.y, c.z);
                            else if constexpr (std::is_same_v<T, TraceRays>)
                            {
                                VkBufferDeviceAddressInfo info{};
                                info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
                                info.buffer = As<VkBufferObject>(c.table->buffer).native;
                                const auto address =
                                    vkGetBufferDeviceAddress(device_, &info) + c.table->offset;
                                const auto stride = c.table->stride;
                                const VkStridedDeviceAddressRegionKHR raygen{address, stride,
                                                                             stride};
                                const VkStridedDeviceAddressRegionKHR miss{address + stride, stride,
                                                                           stride};
                                const VkStridedDeviceAddressRegionKHR hit{address + stride * 2,
                                                                          stride, stride};
                                const VkStridedDeviceAddressRegionKHR callable{};
                                vkCmdTraceRaysKHR(list, &raygen, &miss, &hit, &callable, c.width,
                                                  c.height, c.depth);
                            }
                            else if constexpr (std::is_same_v<T, BuildAccelerationStructure>)
                            {
                                if (!accelerationStructureEnabled_ ||
                                    !vkCmdBuildAccelerationStructuresKHR)
                                    throw std::runtime_error(
                                        "Vulkan acceleration structure commands are not enabled");
                                const auto& accelerationStructure = *c.accelerationStructure;
                                std::vector<VkAccelerationStructureGeometryKHR> geometries;
                                std::vector<uint32_t> primitiveCounts;
                                if (accelerationStructure.desc.topLevel)
                                {
                                    geometries.resize(1);
                                    primitiveCounts.push_back(static_cast<uint32_t>(
                                        accelerationStructure.desc.instances.size()));
                                }
                                else
                                {
                                    geometries.resize(accelerationStructure.desc.geometries.size());
                                    primitiveCounts.resize(geometries.size());
                                }
                                const auto address = [&](const std::shared_ptr<Buffer>& buffer)
                                {
                                    VkBufferDeviceAddressInfo info{};
                                    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
                                    info.buffer = As<VkBufferObject>(buffer).native;
                                    return vkGetBufferDeviceAddress(device_, &info);
                                };
                                for (size_t i = 0; i < geometries.size(); ++i)
                                {
                                    auto& geometry = geometries[i];
                                    geometry.sType =
                                        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
                                    if (accelerationStructure.desc.topLevel)
                                    {
                                        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
                                        geometry.geometry.instances.sType =
                                            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
                                        geometry.geometry.instances.arrayOfPointers = VK_FALSE;
                                        geometry.geometry.instances.data.deviceAddress =
                                            address(accelerationStructure.instanceData);
                                    }
                                    else
                                    {
                                        const auto& source =
                                            accelerationStructure.desc.geometries[i];
                                        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
                                        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
                                        geometry.geometry.triangles.sType =
                                            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
                                        geometry.geometry.triangles.vertexFormat =
                                            VK_FORMAT_R32G32B32_SFLOAT;
                                        geometry.geometry.triangles.vertexData.deviceAddress =
                                            address(source.vertices) + source.vertexOffset;
                                        geometry.geometry.triangles.vertexStride =
                                            source.vertexStride;
                                        geometry.geometry.triangles.maxVertex =
                                            source.vertexCount - 1;
                                        geometry.geometry.triangles.indexType =
                                            source.indices ? VK_INDEX_TYPE_UINT32
                                                           : VK_INDEX_TYPE_NONE_KHR;
                                        if (source.indices)
                                            geometry.geometry.triangles.indexData.deviceAddress =
                                                address(source.indices) + source.indexOffset;
                                        primitiveCounts[i] = source.indices
                                                                 ? source.indexCount / 3
                                                                 : source.vertexCount / 3;
                                    }
                                }
                                VkAccelerationStructureBuildGeometryInfoKHR build{};
                                build.sType =
                                    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
                                build.type = accelerationStructure.desc.topLevel
                                                 ? VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR
                                                 : VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
                                build.flags =
                                    VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
                                if (accelerationStructure.desc.allowUpdate)
                                    build.flags |=
                                        VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
                                build.mode = c.update
                                                 ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
                                                 : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
                                build.srcAccelerationStructure =
                                    c.update ? As<VkAccelerationStructure>(c.accelerationStructure)
                                                   .native
                                             : VK_NULL_HANDLE;
                                build.dstAccelerationStructure =
                                    As<VkAccelerationStructure>(c.accelerationStructure).native;
                                build.geometryCount = static_cast<uint32_t>(geometries.size());
                                build.pGeometries = geometries.data();
                                VkPhysicalDeviceAccelerationStructurePropertiesKHR properties{};
                                properties.sType =
                                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
                                VkPhysicalDeviceProperties2 physicalProperties{};
                                physicalProperties.sType =
                                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
                                physicalProperties.pNext = &properties;
                                vkGetPhysicalDeviceProperties2(physical_, &physicalProperties);
                                const auto scratch = address(c.scratch);
                                const auto alignment = std::max<uint64_t>(
                                    1, properties.minAccelerationStructureScratchOffsetAlignment);
                                build.scratchData.deviceAddress =
                                    (scratch + alignment - 1) & ~(alignment - 1);
                                VkAccelerationStructureBuildRangeInfoKHR range{};
                                range.primitiveCount = primitiveCounts.front();
                                std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges(
                                    geometries.size(), range);
                                for (size_t i = 0;
                                     i < ranges.size() && !accelerationStructure.desc.topLevel; ++i)
                                    ranges[i].primitiveCount = primitiveCounts[i];
                                const VkAccelerationStructureBuildRangeInfoKHR* rangePointer =
                                    ranges.data();
                                vkCmdBuildAccelerationStructuresKHR(list, 1, &build, &rangePointer);
                            }
                            else if constexpr (std::is_same_v<T, Marker>)
                            {
                                if (debug_)
                                {
                                    if (c.begin)
                                    {
                                        VkDebugUtilsLabelEXT label{};
                                        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
                                        label.pLabelName = c.name.c_str();
                                        vkCmdBeginDebugUtilsLabelEXT(list, &label);
                                    }
                                    else
                                        vkCmdEndDebugUtilsLabelEXT(list);
                                }
                            }
                            else if constexpr (std::is_same_v<T, Timestamp>)
                                vkCmdWriteTimestamp2(list, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                                     queries, c.index);
                            else if constexpr (std::is_same_v<T, ResolveTimestamps>)
                                vkCmdCopyQueryPoolResults(
                                    list, queries, 0, c.count,
                                    As<VkBufferObject>(c.destination).native, 0, 8,
                                    VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
                        },
                        commands.GetCommands()[packet]);
                Check(vkEndCommandBuffer(list), "EndCommandBuffer");
            },
            commands.RecordingThreads());
    }
    catch (...)
    {
        std::erase_if(
            contexts_, [&](const auto& context)
            { return std::ranges::find(batch->contexts, context) != batch->contexts.end(); });
        throw;
    }
    std::vector<VkCommandBufferSubmitInfo> buffers(ranges.size());
    for (size_t i = 0; i < buffers.size(); ++i)
    {
        buffers[i].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        buffers[i].commandBuffer = batch->contexts[i]->list;
    }
    VkSemaphoreSubmitInfo signals[2]{};
    signals[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signals[0].semaphore = timeline_;
    signals[0].value = point;
    signals[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSemaphoreSubmitInfo wait{};
    wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    VkSubmitInfo2 info{};
    info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    info.commandBufferInfoCount = static_cast<uint32_t>(buffers.size());
    info.pCommandBufferInfos = buffers.data();
    info.signalSemaphoreInfoCount = 1;
    info.pSignalSemaphoreInfos = signals;
    auto* swapchain = static_cast<VulkanSwapchain*>(presentation);
    if (swapchain)
    {
        wait.semaphore = swapchain->Available();
        wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signals[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signals[1].semaphore = swapchain->Finished();
        signals[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        info.waitSemaphoreInfoCount = 1;
        info.pWaitSemaphoreInfos = &wait;
        info.signalSemaphoreInfoCount = 2;
    }
    if (async && nativeSequence_ > UINT64_MAX - ranges.size())
        throw std::overflow_error("Queue timeline exhausted");
    batch->trace.reserve(ranges.size() * 4 + 8);
    RetainSubmitting(batch);
    if (!async)
    {
        Check(vkQueueSubmit2(queue_, 1, &info, VK_NULL_HANDLE), "Graphics submit");
        if (swapchain)
            batch->trace.push_back({QueueType::Graphics, QueueAction::WaitAcquire});
        batch->trace.push_back({QueueType::Graphics, QueueAction::Execute, buffers.size()});
        batch->trace.push_back({QueueType::Graphics, QueueAction::SignalFrame, point});
    }
    else
    {
        uint64_t prologue = 0, compute = 0;
        for (size_t i = 0; i < ranges.size(); ++i)
        {
            const bool onCompute = ranges[i].queue == QueueType::Compute;
            VkSemaphoreSubmitInfo dependencies[3]{};
            uint32_t count = 0;
            const auto dependency = [&](VkSemaphore semaphore, uint64_t value)
            {
                auto& entry = dependencies[count++];
                entry.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
                entry.semaphore = semaphore;
                entry.value = value;
                entry.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            };
            if (onCompute)
            {
                if (!prologue)
                    throw std::logic_error("Async compute requires a Graphics prologue");
                dependency(graphicsTimeline_, prologue);
                if (LastSubmitted())
                    dependency(timeline_, LastSubmitted());
            }
            else
            {
                if (ranges[i].waitCompute && compute)
                    dependency(computeTimeline_, compute);
                if (i == 0 && swapchain)
                    dependency(swapchain->Available(), 0);
            }
            VkSemaphoreSubmitInfo produced{};
            produced.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            produced.semaphore = onCompute ? computeTimeline_ : graphicsTimeline_;
            produced.value = ++nativeSequence_;
            produced.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            VkSubmitInfo2 segment{};
            segment.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
            segment.commandBufferInfoCount = 1;
            segment.pCommandBufferInfos = &buffers[i];
            segment.waitSemaphoreInfoCount = count;
            segment.pWaitSemaphoreInfos = dependencies;
            segment.signalSemaphoreInfoCount = 1;
            segment.pSignalSemaphoreInfos = &produced;
            Check(vkQueueSubmit2(onCompute ? computeQueue_ : queue_, 1, &segment, VK_NULL_HANDLE),
                  "Submit queue segment");
            const auto queue = onCompute ? QueueType::Compute : QueueType::Graphics;
            const auto range = static_cast<uint32_t>(i);
            if (onCompute)
            {
                batch->trace.push_back({queue, QueueAction::WaitGraphics, prologue, range});
                if (LastSubmitted())
                    batch->trace.push_back({queue, QueueAction::WaitFrame, LastSubmitted(), range});
            }
            else
            {
                if (ranges[i].waitCompute && compute)
                    batch->trace.push_back({queue, QueueAction::WaitCompute, compute, range});
                if (i == 0 && swapchain)
                    batch->trace.push_back({queue, QueueAction::WaitAcquire, 0, range});
            }
            batch->trace.push_back({queue, QueueAction::Execute, 1, range});
            batch->trace.push_back(
                {queue, onCompute ? QueueAction::SignalCompute : QueueAction::SignalGraphics,
                 produced.value, range});
            if (onCompute)
                compute = produced.value;
            else if (!prologue)
                prologue = produced.value;
        }
        // The public frame point always follows every actual queue, including unused producers.
        info.commandBufferInfoCount = 0;
        info.pCommandBufferInfos = nullptr;
        info.waitSemaphoreInfoCount = compute ? 1 : 0;
        wait.semaphore = computeTimeline_;
        wait.value = compute;
        wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        info.pWaitSemaphoreInfos = &wait;
        Check(vkQueueSubmit2(queue_, 1, &info, VK_NULL_HANDLE), "Submit aggregate Graphics tail");
        if (compute)
            batch->trace.push_back({QueueType::Graphics, QueueAction::WaitCompute, compute});
        batch->trace.push_back({QueueType::Graphics, QueueAction::SignalFrame, point});
    }
    if (swapchain)
        batch->trace.push_back({QueueType::Graphics, QueueAction::SignalPresent});
    if (swapchain)
        swapchain->Submitted(point);
    return batch;
}
} // namespace renderlab::rhi::vulkan
