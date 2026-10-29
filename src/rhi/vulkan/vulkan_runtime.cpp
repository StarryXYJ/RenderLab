#include "rhi/vulkan/vulkan_runtime.hpp"

#include <stdexcept>

namespace renderlab::rhi::vulkan
{
void VulkanRuntime::EnableAsyncCompute()
{
    if (!computeQueue_)
        throw std::invalid_argument("A second queue in the Graphics family is unavailable");
    if (computeTimeline_)
        return;
    VkSemaphoreTypeCreateInfo type{};
    type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    info.pNext = &type;
    VkSemaphore graphics = VK_NULL_HANDLE;
    Check(vkCreateSemaphore(device_, &info, nullptr, &graphics), "Graphics segment timeline");
    VkSemaphore compute = VK_NULL_HANDLE;
    const auto result = vkCreateSemaphore(device_, &info, nullptr, &compute);
    if (result != VK_SUCCESS)
    {
        vkDestroySemaphore(device_, graphics, nullptr);
        Check(result, "Compute timeline");
    }
    graphicsTimeline_ = graphics;
    computeTimeline_ = compute;
}
void VulkanRuntime::DrainNative()
{
    Check(vkDeviceWaitIdle(device_), "Drain actual submitted queues");
}
QueueClock VulkanRuntime::TimestampClock(QueueType queue) const
{
    if (queue == QueueType::Compute && !calibratedTimestamps_)
        throw std::invalid_argument("Cross-queue Vulkan profiling requires calibrated_timestamps");
    return Runtime::TimestampClock(queue);
}
VulkanRuntime::VulkanRuntime(VkInstance instance, VkPhysicalDevice physical, VkDevice device,
                             VkQueue queue, VkQueue computeQueue, uint32_t family, bool debug,
                             bool accelerationStructureEnabled, bool rayQueryEnabled,
                             bool rayTracingPipelineEnabled, bool calibratedTimestamps)
    : instance_(instance), physical_(physical), device_(device), queue_(queue),
      computeQueue_(computeQueue), family_(family), debug_(debug),
      accelerationStructureEnabled_(accelerationStructureEnabled),
      rayQueryEnabled_(rayQueryEnabled), rayTracingPipelineEnabled_(rayTracingPipelineEnabled),
      calibratedTimestamps_(calibratedTimestamps)
{
    vkGetPhysicalDeviceMemoryProperties(physical_, &memory_);
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical_, &props);
    timestampPeriod_ = props.limits.timestampPeriod;
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &features12;
    vkGetPhysicalDeviceFeatures2(physical_, &features);
    if (features12.shaderSampledImageArrayNonUniformIndexing &&
        props.limits.maxPerStageDescriptorSampledImages >= 80 &&
        props.limits.maxDescriptorSetSampledImages >= 80)
        sampledTextureArrayLimit_ = 64;
    indirect_ = {features.features.multiDrawIndirect ? props.limits.maxDrawIndirectCount : 1,
                 features12.drawIndirectCount != 0,
                 features.features.drawIndirectFirstInstance != 0, true};
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
    if (graphicsTimeline_)
        vkDestroySemaphore(device_, graphicsTimeline_, nullptr);
    if (computeTimeline_)
        vkDestroySemaphore(device_, computeTimeline_, nullptr);
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
                                             VkDevice device, VkQueue queue, VkQueue computeQueue,
                                             uint32_t family, bool debug,
                                             bool accelerationStructureEnabled,
                                             bool rayQueryEnabled, bool rayTracingPipelineEnabled,
                                             bool calibratedTimestamps)
{
    return std::make_unique<vulkan::VulkanRuntime>(instance, physical, device, queue, computeQueue,
                                                   family, debug, accelerationStructureEnabled,
                                                   rayQueryEnabled, rayTracingPipelineEnabled,
                                                   calibratedTimestamps);
}
} // namespace renderlab::rhi
