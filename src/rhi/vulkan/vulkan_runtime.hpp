#pragma once

#include "rhi/runtime.hpp"
#include "rhi/vulkan/vulkan_common.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace renderlab::rhi::vulkan
{
struct Allocation;
struct VkDescriptorBlock;

/** @brief Backend device services; owns queue completion and native command contexts. */
class VulkanRuntime final : public Runtime
{
public:
    uint32_t SampledTextureArrayLimit() const override
    {
        return sampledTextureArrayLimit_;
    }
    IndirectSupport GetIndirectSupport() const override
    {
        return indirect_;
    }
    VulkanRuntime(VkInstance instance, VkPhysicalDevice physical, VkDevice device, VkQueue queue,
                  uint32_t family, bool debug, bool accelerationStructureEnabled,
                  bool rayQueryEnabled, bool rayTracingPipelineEnabled);
    ~VulkanRuntime() override;
    uint64_t Completed() const override;
    double TimestampPeriodNs() const override;
    bool SupportsAccelerationStructures() const override
    {
        return accelerationStructureEnabled_;
    }
    bool SupportsRayQuery() const override
    {
        return rayQueryEnabled_;
    }
    bool SupportsRayTracingPipeline() const override
    {
        return rayTracingPipelineEnabled_;
    }
    std::shared_ptr<Buffer> CreateBuffer(const BufferDesc& desc) override;
    std::shared_ptr<Texture> CreateTexture(const TextureDesc& desc) override;
    std::shared_ptr<TextureView> CreateView(std::shared_ptr<Texture> texture,
                                            TextureViewUsage usage) override;
    std::shared_ptr<Sampler> CreateSampler() override;
    std::shared_ptr<BindingLayout> CreateLayout(std::vector<BindingDesc> entries) override;
    std::shared_ptr<BindingGroup> CreateGroup(std::shared_ptr<BindingLayout> layout,
                                              std::vector<Binding> bindings) override;
    std::unique_ptr<Swapchain> CreateSwapchain(platform::NativeSurface surface, uint32_t width,
                                               uint32_t height, bool vsync = true) override;

protected:
    std::shared_ptr<ShaderTable> CreateShaderTableNative(std::shared_ptr<Pipeline>) override;
    std::shared_ptr<Pipeline> CreatePipelineNative(PipelineDesc desc) override;
    AccelerationStructureSizes
    QueryAccelerationStructureSizesNative(const AccelerationStructureDesc& desc) override;
    std::shared_ptr<AccelerationStructure>
    CreateAccelerationStructureNative(AccelerationStructureDesc desc,
                                      AccelerationStructureSizes sizes) override;
    void WaitNative(uint64_t point) override;
    struct Work : Submission
    {
        VkDevice device = VK_NULL_HANDLE;
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer list = VK_NULL_HANDLE;
        VkQueryPool queries = VK_NULL_HANDLE;
        ~Work() override
        {
            if (pool)
                vkDestroyCommandPool(device, pool, nullptr);
            if (queries)
                vkDestroyQueryPool(device, queries, nullptr);
        }
    };
    std::shared_ptr<Submission> Execute(const CommandList& commands, uint64_t point,
                                        Swapchain* presentation) override;

private:
    std::shared_ptr<VkDescriptorBlock> AcquireDescriptors(std::shared_ptr<BindingLayout> layout);
    std::vector<std::shared_ptr<VkDescriptorBlock>> descriptorBlocks_;
    void Allocate(Allocation& allocation, const VkMemoryRequirements& requirements, Memory kind,
                  bool deviceAddress = false);
    void Name(VkObjectType type, uint64_t handle, const std::string& name);
    VkInstance instance_;
    VkPhysicalDevice physical_;
    VkDevice device_;
    VkQueue queue_;
    uint32_t family_;
    bool debug_;
    IndirectSupport indirect_;
    uint32_t sampledTextureArrayLimit_ = 0;
    bool accelerationStructureEnabled_;
    bool rayQueryEnabled_, rayTracingPipelineEnabled_;
    VkPhysicalDeviceMemoryProperties memory_{};
    VkSemaphore timeline_ = VK_NULL_HANDLE;
    double timestampPeriod_ = 0;
    std::vector<std::shared_ptr<Work>> contexts_;
};
} // namespace renderlab::rhi::vulkan

namespace renderlab::rhi
{
std::unique_ptr<Runtime> CreateVulkanRuntime(VkInstance, VkPhysicalDevice, VkDevice, VkQueue,
                                             uint32_t, bool, bool, bool, bool);
} // namespace renderlab::rhi
