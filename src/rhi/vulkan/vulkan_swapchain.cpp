#include "rhi/vulkan/vulkan_swapchain.hpp"
#include "rhi/vulkan/vulkan_objects.hpp"
#include "rhi/vulkan/vulkan_runtime.hpp"

#include <algorithm>
#include <stdexcept>

namespace renderlab::rhi::vulkan
{
VulkanSwapchain::VulkanSwapchain(VulkanRuntime& runtime, VkInstance instance,
                                 VkPhysicalDevice physical, VkDevice device, VkQueue queue)
    : Swapchain(&runtime), runtime_(runtime), instance_(instance), physical_(physical),
      device_(device), queue_(queue)
{
}

void VulkanSwapchain::Initialize(uint32_t family, platform::NativeSurface surface)
{
    VkWin32SurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    info.hinstance = static_cast<HINSTANCE>(surface.instance);
    info.hwnd = static_cast<HWND>(surface.window);
    Check(vkCreateWin32SurfaceKHR(instance_, &info, nullptr, &surface_), "vkCreateWin32SurfaceKHR");
    VkBool32 supported = VK_FALSE;
    Check(vkGetPhysicalDeviceSurfaceSupportKHR(physical_, family, surface_, &supported),
          "SurfaceSupport");
    if (!supported)
        throw std::runtime_error("Graphics queue cannot present to this surface");
}

VulkanSwapchain::~VulkanSwapchain()
{
    // Timeline completion covers rendering, not the presentation engine's binary semaphore
    // wait.
    if (native_)
        vkQueueWaitIdle(queue_);
    Clear();
    if (surface_)
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
}

AcquiredImage VulkanSwapchain::Acquire()
{
    if (!native_ || suspended_)
        return {};
    if (acquired_)
        throw std::invalid_argument("Swapchain image already acquired");
    // Acquire semaphores are per frame slot and reused only after their graphics timeline
    // point.
    acquireSlot_ = (acquireSlot_ + 1) % static_cast<uint32_t>(available_.size());
    if (acquirePoints_[acquireSlot_])
    {
        VkSemaphoreWaitInfo wait{};
        wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        wait.semaphoreCount = 1;
        wait.pSemaphores = &timeline;
        wait.pValues = &acquirePoints_[acquireSlot_];
        Check(vkWaitSemaphores(device_, &wait, 30'000'000'000ull), "Acquire slot wait");
    }
    const auto result = vkAcquireNextImageKHR(
        device_, native_, UINT64_MAX, available_[acquireSlot_], VK_NULL_HANDLE, &imageIndex_);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
        return {};
    if (result != VK_SUBOPTIMAL_KHR)
        Check(result, "vkAcquireNextImageKHR");
    acquired_ = true;
    return {images_[imageIndex_], imageIndex_};
}

void VulkanSwapchain::Present()
{
    if (!acquired_ || !submitted_)
        throw std::invalid_argument("Present requires acquired submitted image");
    VkPresentInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &finished_[imageIndex_];
    info.swapchainCount = 1;
    info.pSwapchains = &native_;
    info.pImageIndices = &imageIndex_;
    const auto result = vkQueuePresentKHR(queue_, &info);
    acquired_ = submitted_ = false;
    if (result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR)
        Check(result, "vkQueuePresentKHR");
}

void VulkanSwapchain::Resize(uint32_t width, uint32_t height)
{
    if (native_)
        Check(vkQueueWaitIdle(queue_), "Wait presentation before resize");
    Clear();
    suspended_ = !width || !height;
    if (suspended_)
        return;
    VkSurfaceCapabilitiesKHR caps{};
    Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps),
          "SurfaceCapabilities");
    uint32_t count = 0;
    Check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, nullptr),
          "SurfaceFormat count");
    std::vector<VkSurfaceFormatKHR> formats(count);
    Check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, formats.data()),
          "SurfaceFormats");
    auto selected =
        std::ranges::find_if(formats,
                             [](auto f)
                             {
                                 return f.format == VK_FORMAT_R8G8B8A8_SRGB &&
                                        f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
                             });
    if (selected == formats.end())
        selected =
            std::ranges::find_if(formats,
                                 [](auto f)
                                 {
                                     return f.format == VK_FORMAT_B8G8R8A8_SRGB &&
                                            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
                                 });
    if (selected == formats.end())
        throw std::runtime_error("sRGB SDR swapchain format unavailable");
    format_ = selected->format == VK_FORMAT_R8G8B8A8_SRGB ? Format::Rgba8Srgb : Format::Bgra8Srgb;
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX)
        extent = {std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width),
                  std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height)};
    if (!extent.width || !extent.height)
    {
        suspended_ = true;
        return;
    }
    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = surface_;
    info.minImageCount = std::max(3u, caps.minImageCount);
    if (caps.maxImageCount)
        info.minImageCount = std::min(info.minImageCount, caps.maxImageCount);
    info.imageFormat = selected->format;
    info.imageColorSpace = selected->colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & info.compositeAlpha))
        info.compositeAlpha = static_cast<VkCompositeAlphaFlagBitsKHR>(
            caps.supportedCompositeAlpha & (~caps.supportedCompositeAlpha + 1));
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    Check(vkCreateSwapchainKHR(device_, &info, nullptr, &native_), "vkCreateSwapchainKHR");
    Check(vkGetSwapchainImagesKHR(device_, native_, &count, nullptr), "Swapchain image count");
    std::vector<VkImage> images(count);
    Check(vkGetSwapchainImagesKHR(device_, native_, &count, images.data()), "Swapchain images");
    for (auto image : images)
    {
        auto texture =
            std::make_shared<VkTexture>(owner, TextureDesc{extent.width, extent.height, format_,
                                                           false, true, "Swapchain.Image"});
        texture->allocation.device = device_;
        texture->native = image;
        texture->borrowed = true;
        views_.push_back(runtime_.CreateView(texture));
        images_.push_back(std::move(texture));
    }
    available_.resize(2);
    acquirePoints_.resize(2);
    finished_.resize(images.size());
    VkSemaphoreCreateInfo semaphore{};
    semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (auto& value : available_)
        Check(vkCreateSemaphore(device_, &semaphore, nullptr, &value), "Acquire semaphore");
    // A render-finished semaphore belongs to an image, not a frame slot. Reacquisition retires
    // its previous present wait.
    for (auto& value : finished_)
        Check(vkCreateSemaphore(device_, &semaphore, nullptr, &value), "Present semaphore");
}

Format VulkanSwapchain::GetFormat() const
{
    return format_;
}

VkSemaphore VulkanSwapchain::Available() const
{
    if (!acquired_ || submitted_)
        throw std::invalid_argument("Swapchain submit requires fresh acquisition");
    return available_[acquireSlot_];
}

VkSemaphore VulkanSwapchain::Finished() const
{
    return finished_[imageIndex_];
}

void VulkanSwapchain::Submitted(uint64_t point)
{
    acquirePoints_[acquireSlot_] = point;
    submitted_ = true;
}

void VulkanSwapchain::Clear()
{
    views_.clear();
    images_.clear();
    for (auto value : available_)
        if (value)
            vkDestroySemaphore(device_, value, nullptr);
    for (auto value : finished_)
        if (value)
            vkDestroySemaphore(device_, value, nullptr);
    available_.clear();
    finished_.clear();
    acquirePoints_.clear();
    if (native_)
        vkDestroySwapchainKHR(device_, native_, nullptr);
    native_ = VK_NULL_HANDLE;
    acquired_ = submitted_ = false;
}

std::unique_ptr<Swapchain> VulkanRuntime::CreateSwapchain(platform::NativeSurface surface,
                                                          uint32_t width, uint32_t height)
{
    auto swapchain =
        std::make_unique<VulkanSwapchain>(*this, instance_, physical_, device_, queue_);
    swapchain->Initialize(family_, surface);
    swapchain->timeline = timeline_;
    swapchain->Resize(width, height);
    return swapchain;
}
} // namespace renderlab::rhi::vulkan
