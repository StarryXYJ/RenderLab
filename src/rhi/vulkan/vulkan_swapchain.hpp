#pragma once

#include "platform/window.hpp"
#include "rhi/swapchain.hpp"
#include "rhi/vulkan/vulkan_common.hpp"

#include <vector>

namespace renderlab::rhi::vulkan
{
class VulkanRuntime;
class VulkanSwapchain final : public Swapchain
{
public:
    VulkanSwapchain(VulkanRuntime& runtime, VkInstance instance, VkPhysicalDevice physical,
                    VkDevice device, VkQueue queue, bool vsync);
    void Initialize(uint32_t family, platform::NativeSurface surface);
    ~VulkanSwapchain() override;
    AcquiredImage Acquire() override;
    void Present() override;
    void Resize(uint32_t width, uint32_t height) override;
    Format GetFormat() const override;
    VkSemaphore Available() const;
    VkSemaphore Finished() const;
    void Submitted(uint64_t point);
    VkSemaphore timeline = VK_NULL_HANDLE;

private:
    void Clear();
    VulkanRuntime& runtime_;
    VkInstance instance_;
    VkPhysicalDevice physical_;
    VkDevice device_;
    VkQueue queue_;
    bool vsync_;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR native_ = VK_NULL_HANDLE;
    Format format_ = Format::Rgba8;
    std::vector<std::shared_ptr<Texture>> images_;
    std::vector<std::shared_ptr<TextureView>> views_;
    std::vector<VkSemaphore> available_, finished_;
    std::vector<uint64_t> acquirePoints_;
    uint32_t acquireSlot_ = 0, imageIndex_ = 0;
    bool acquired_ = false, submitted_ = false, suspended_ = false;
};
} // namespace renderlab::rhi::vulkan
