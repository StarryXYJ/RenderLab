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
    std::shared_ptr<Work> work;
    const auto reusable =
        std::ranges::find_if(contexts_, [](const auto& w) { return w.use_count() == 1; });
    if (reusable != contexts_.end())
    {
        work = *reusable;
        Check(vkResetCommandPool(device_, work->pool, 0), "Reset retired command pool");
    }
    else
    {
        work = std::make_shared<Work>();
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
        query.queryCount = 2;
        Check(vkCreateQueryPool(device_, &query, nullptr, &work->queries), "Timestamp QueryPool");
        contexts_.push_back(work);
    }
    auto list = work->list;
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(vkBeginCommandBuffer(list, &begin), "BeginCommandBuffer");
    vkCmdResetQueryPool(list, work->queries, 0, 2);
    bool compute = false;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    for (const auto& command : commands.GetCommands())
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
                    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
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
                    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    b.image = As<VkTexture>(c.texture).native;
                    b.subresourceRange = {VkImageAspectFlags(c.texture->desc.format == Format::D32
                                                                 ? VK_IMAGE_ASPECT_DEPTH_BIT
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
                    copy.bufferRowLength = c.rowPitch / FormatBytes(c.texture->desc.format);
                    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    copy.imageExtent = {c.texture->desc.width, c.texture->desc.height, 1};
                    if (c.upload)
                        vkCmdCopyBufferToImage(list, As<VkBufferObject>(c.buffer).native,
                                               As<VkTexture>(c.texture).native,
                                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                    else
                        vkCmdCopyImageToBuffer(list, As<VkTexture>(c.texture).native,
                                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                               As<VkBufferObject>(c.buffer).native, 1, &copy);
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
                    VkRenderingAttachmentInfo color{};
                    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                    color.imageView = As<VkView>(c.color).native;
                    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    color.loadOp = c.colorLoad == LoadOp::Clear  ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                   : c.colorLoad == LoadOp::Load ? VK_ATTACHMENT_LOAD_OP_LOAD
                                                                 : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    std::copy(c.clear.begin(), c.clear.end(), color.clearValue.color.float32);
                    VkRenderingAttachmentInfo depth{};
                    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                    if (c.depth)
                    {
                        depth.imageView = As<VkView>(c.depth).native;
                        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                        depth.loadOp = c.depthLoad == LoadOp::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                       : c.depthLoad == LoadOp::Load
                                           ? VK_ATTACHMENT_LOAD_OP_LOAD
                                           : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                        depth.clearValue.depthStencil.depth = 0;
                    }
                    const auto& d = c.color->texture->desc;
                    VkRenderingInfo rendering{};
                    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                    rendering.renderArea.extent = {d.width, d.height};
                    rendering.layerCount = 1;
                    rendering.colorAttachmentCount = 1;
                    rendering.pColorAttachments = &color;
                    rendering.pDepthAttachment = c.depth ? &depth : nullptr;
                    vkCmdBeginRendering(list, &rendering);
                    VkViewport viewport{0, float(d.height), float(d.width), -float(d.height), 0, 1};
                    VkRect2D scissor{{0, 0}, {d.width, d.height}};
                    vkCmdSetViewport(list, 0, 1, &viewport);
                    vkCmdSetScissor(list, 0, 1, &scissor);
                }
                else if constexpr (std::is_same_v<T, EndRendering>)
                    vkCmdEndRendering(list);
                else if constexpr (std::is_same_v<T, SetPipeline>)
                {
                    compute = bool(c.pipeline->desc.compute);
                    layout = As<VkLayout>(c.pipeline->desc.layout).native;
                    vkCmdBindPipeline(list,
                                      compute ? VK_PIPELINE_BIND_POINT_COMPUTE
                                              : VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      As<VkPipelineObject>(c.pipeline).native);
                }
                else if constexpr (std::is_same_v<T, SetBindings>)
                    vkCmdBindDescriptorSets(
                        list,
                        compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
                        layout, 0, 4, As<VkGroup>(c.group).native.data(), 0, nullptr);
                else if constexpr (std::is_same_v<T, SetGeometry>)
                {
                    VkDeviceSize offset = 0;
                    vkCmdBindVertexBuffers(list, 0, 1, &As<VkBufferObject>(c.vertex).native,
                                           &offset);
                    if (c.index)
                        vkCmdBindIndexBuffer(list, As<VkBufferObject>(c.index).native, 0,
                                             VK_INDEX_TYPE_UINT32);
                }
                else if constexpr (std::is_same_v<T, Draw>)
                {
                    if (c.indexed)
                        vkCmdDrawIndexed(list, c.count, 1, c.first, 0, 0);
                    else
                        vkCmdDraw(list, c.count, 1, c.first, 0);
                }
                else if constexpr (std::is_same_v<T, DrawIndirect>)
                    vkCmdDrawIndirect(list, As<VkBufferObject>(c.arguments).native, c.offset, 1,
                                      sizeof(DrawArguments));
                else if constexpr (std::is_same_v<T, Dispatch>)
                    vkCmdDispatch(list, c.x, c.y, c.z);
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
                    vkCmdWriteTimestamp2(list, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, work->queries,
                                         c.index);
                else if constexpr (std::is_same_v<T, ResolveTimestamps>)
                    vkCmdCopyQueryPoolResults(list, work->queries, 0, 2,
                                              As<VkBufferObject>(c.destination).native, 0, 8,
                                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
            },
            command);
    Check(vkEndCommandBuffer(list), "EndCommandBuffer");
    VkCommandBufferSubmitInfo buffer{};
    buffer.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    buffer.commandBuffer = list;
    VkSemaphoreSubmitInfo signals[2]{};
    signals[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signals[0].semaphore = timeline_;
    signals[0].value = point;
    signals[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSemaphoreSubmitInfo wait{};
    wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    VkSubmitInfo2 info{};
    info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    info.commandBufferInfoCount = 1;
    info.pCommandBufferInfos = &buffer;
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
    Check(vkQueueSubmit2(queue_, 1, &info, VK_NULL_HANDLE), "Graphics submit");
    if (swapchain)
        swapchain->Submitted(point);
    return work;
}
} // namespace renderlab::rhi::vulkan
