#include "rhi/vulkan/vulkan_runtime.hpp"

#include <stdexcept>

namespace renderlab::rhi::vulkan
{
VulkanRuntime::VulkanRuntime(VkInstance instance, VkPhysicalDevice physical, VkDevice device,
                             VkQueue queue, uint32_t family, bool debug,
                             bool accelerationStructureEnabled, bool rayQueryEnabled,
                             bool rayTracingPipelineEnabled)
    : instance_(instance), physical_(physical), device_(device), queue_(queue), family_(family),
      debug_(debug), accelerationStructureEnabled_(accelerationStructureEnabled),
      rayQueryEnabled_(rayQueryEnabled), rayTracingPipelineEnabled_(rayTracingPipelineEnabled)
{
    vkGetPhysicalDeviceMemoryProperties(physical_, &memory_);
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical_, &props);
    timestampPeriod_ = props.limits.timestampPeriod;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_, &count, families.data());
    if (families[family_].timestampValidBits != 64)
        throw std::runtime_error("M1 timestamps require 64 valid queue timestamp bits");
    VkSemaphoreTypeCreateInfo type{};
    type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    info.pNext = &type;
    Check(vkCreateSemaphore(device_, &info, nullptr, &timeline_), "Timeline semaphore");
}

VulkanRuntime::~VulkanRuntime()
{
    try
    {
        WaitIdle();
    }
    catch (...)
    { /* Device loss: no recovery in M1. */
    }
    if (timeline_)
        vkDestroySemaphore(device_, timeline_, nullptr);
}

uint64_t VulkanRuntime::Completed() const
{
    uint64_t value = 0;
    Check(vkGetSemaphoreCounterValue(device_, timeline_, &value), "Timeline counter");
    return value;
}

double VulkanRuntime::TimestampPeriodNs() const
{
    return timestampPeriod_;
}

void VulkanRuntime::WaitNative(uint64_t point)
{
    VkSemaphoreWaitInfo wait{};
    wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    wait.semaphoreCount = 1;
    wait.pSemaphores = &timeline_;
    wait.pValues = &point;
    Check(vkWaitSemaphores(device_, &wait, 30'000'000'000ull), "Wait graphics timeline");
}
} // namespace renderlab::rhi::vulkan

namespace renderlab::rhi
{
std::unique_ptr<Runtime> CreateVulkanRuntime(VkInstance instance, VkPhysicalDevice physical,
                                             VkDevice device, VkQueue queue, uint32_t family,
                                             bool debug, bool accelerationStructureEnabled,
                                             bool rayQueryEnabled, bool rayTracingPipelineEnabled)
{
    return std::make_unique<vulkan::VulkanRuntime>(instance, physical, device, queue, family, debug,
                                                   accelerationStructureEnabled, rayQueryEnabled,
                                                   rayTracingPipelineEnabled);
}
} // namespace renderlab::rhi
