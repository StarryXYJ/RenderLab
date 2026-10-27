#pragma once

#include "rhi/vulkan/vulkan_common.hpp"

#include <array>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace renderlab::rhi::vulkan
{
struct Allocation
{
    VkDevice device = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    std::shared_ptr<AllocationLedger> ledger;
    uint64_t bytes = 0;
    ~Allocation()
    {
        if (memory)
            vkFreeMemory(device, memory, nullptr);
        if (ledger)
            ledger->Remove(bytes);
    }
};
struct VkBufferObject : Buffer
{
    using Buffer::Buffer;
    Allocation allocation;
    VkBuffer native = VK_NULL_HANDLE;
    void* mapped = nullptr;
    ~VkBufferObject() override
    {
        if (mapped)
            vkUnmapMemory(allocation.device, allocation.memory);
        if (native)
            vkDestroyBuffer(allocation.device, native, nullptr);
    }
    std::span<std::byte> Map() override
    {
        if (desc.memory == Memory::Device)
            throw std::invalid_argument("Cannot map device buffer");
        if (!mapped)
            Check(vkMapMemory(allocation.device, allocation.memory, 0, VK_WHOLE_SIZE, 0, &mapped),
                  "vkMapMemory");
        return {static_cast<std::byte*>(mapped), static_cast<size_t>(desc.size)};
    }
};
struct VkTexture : Texture
{
    using Texture::Texture;
    Allocation allocation;
    VkImage native = VK_NULL_HANDLE;
    bool borrowed = false;
    ~VkTexture() override
    {
        if (native && !borrowed)
            vkDestroyImage(allocation.device, native, nullptr);
    }
};
struct VkView : TextureView
{
    using TextureView::TextureView;
    VkDevice device = VK_NULL_HANDLE;
    VkImageView native = VK_NULL_HANDLE;
    ~VkView() override
    {
        if (native)
            vkDestroyImageView(device, native, nullptr);
    }
};
struct VkSamplerObject : Sampler
{
    using Sampler::Sampler;
    VkDevice device = VK_NULL_HANDLE;
    VkSampler native = VK_NULL_HANDLE;
    ~VkSamplerObject() override
    {
        if (native)
            vkDestroySampler(device, native, nullptr);
    }
};
struct VkLayout : BindingLayout
{
    using BindingLayout::BindingLayout;
    VkDevice device = VK_NULL_HANDLE;
    std::array<VkDescriptorSetLayout, 4> sets{};
    VkPipelineLayout native = VK_NULL_HANDLE;
    ~VkLayout() override
    {
        if (native)
            vkDestroyPipelineLayout(device, native, nullptr);
        for (auto set : sets)
            if (set)
                vkDestroyDescriptorSetLayout(device, set, nullptr);
    }
};
struct VkDescriptorBlock
{
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    std::shared_ptr<BindingLayout> layout;
    std::shared_ptr<DescriptorStats> stats;
    std::array<VkDescriptorSet, 4> native{};
    ~VkDescriptorBlock()
    {
        if (pool)
            vkDestroyDescriptorPool(device, pool, nullptr);
        if (stats)
        {
            --stats->pages;
            stats->capacity -= layout->entries.size();
        }
    }
};
struct VkGroup : BindingGroup
{
    using BindingGroup::BindingGroup;
    std::shared_ptr<VkDescriptorBlock> allocation;
    std::array<VkDescriptorSet, 4> native{};
    ~VkGroup() override
    {
        if (allocation)
            allocation->stats->inUse -= layout->entries.size();
    }
};
struct VkPipelineObject : Pipeline
{
    using Pipeline::Pipeline;
    VkDevice device = VK_NULL_HANDLE;
    VkPipeline native = VK_NULL_HANDLE;
    ~VkPipelineObject() override
    {
        if (native)
            vkDestroyPipeline(device, native, nullptr);
    }
};
struct VkAccelerationStructure final : AccelerationStructure
{
    using AccelerationStructure::AccelerationStructure;
    VkDevice device = VK_NULL_HANDLE;
    VkAccelerationStructureKHR native = VK_NULL_HANDLE;
    ~VkAccelerationStructure() override
    {
        if (native)
            vkDestroyAccelerationStructureKHR(device, native, nullptr);
    }
};
} // namespace renderlab::rhi::vulkan
