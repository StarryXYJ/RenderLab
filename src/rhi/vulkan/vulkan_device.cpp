#include "rhi/device.hpp"
#include "rhi/vulkan/vulkan_runtime.hpp"

#include <volk.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <numeric>
#include <stdexcept>
#include <string_view>

namespace renderlab::rhi
{
namespace
{
using vulkan::Check;

template <class T, class F> std::vector<T> Enumerate(F function)
{
    // Results may change between calls; retry VK_INCOMPLETE instead of accepting a truncated list.
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        uint32_t count = 0;
        Check(function(&count, static_cast<T*>(nullptr)), "Vulkan enumeration count");
        std::vector<T> values(count);
        const auto result = function(&count, values.data());
        if (result == VK_INCOMPLETE)
        {
            continue;
        }
        Check(result, "Vulkan enumeration");
        values.resize(count);
        return values;
    }
    throw std::runtime_error("Vulkan enumeration changed repeatedly");
}

std::string Version(uint32_t version)
{
    return std::format("{}.{}.{}", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version),
                       VK_API_VERSION_PATCH(version));
}

bool HasExtension(const std::vector<VkExtensionProperties>& extensions, std::string_view name)
{
    return std::ranges::any_of(extensions,
                               [&](const auto& item) { return name == item.extensionName; });
}

class VulkanDevice final : public Device
{
public:
    explicit VulkanDevice(Logger& logger) : logger_(logger) {}

    ~VulkanDevice() override
    {
        Shutdown();
    }

    void Shutdown() noexcept override
    {
        // Keep the messenger alive while destroying the device to capture cleanup diagnostics.
        if (device_ != VK_NULL_HANDLE)
        {
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
            queue_ = VK_NULL_HANDLE;
        }
        if (messenger_ != VK_NULL_HANDLE)
        {
            vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
            messenger_ = VK_NULL_HANDLE;
        }
        if (instance_ != VK_NULL_HANDLE)
        {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
        if (loaderInitialized_)
        {
            volkFinalize();
            loaderInitialized_ = false;
        }
    }

    void Initialize(const DeviceOptions& options)
    {
        report_.backend = "vulkan";
        Check(volkInitialize(), "volkInitialize (Vulkan loader required)");
        loaderInitialized_ = true;
        uint32_t loaderVersion = VK_API_VERSION_1_0;
        if (vkEnumerateInstanceVersion != nullptr)
        {
            Check(vkEnumerateInstanceVersion(&loaderVersion), "vkEnumerateInstanceVersion");
        }
        if (loaderVersion < VK_API_VERSION_1_3)
        {
            throw std::runtime_error("Vulkan 1.3 loader is required");
        }
        report_.details.emplace_back("loaderApiVersion", Version(loaderVersion));
        const auto extensions = Enumerate<VkExtensionProperties>(
            [](uint32_t* count, auto* values)
            { return vkEnumerateInstanceExtensionProperties(nullptr, count, values); });
        std::vector<const char*> enabledExtensions;
        for (const auto* required :
             {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME})
        {
            if (!HasExtension(extensions, required))
            {
                throw std::runtime_error(std::string("Required instance extension missing: ") +
                                         required);
            }
            enabledExtensions.push_back(required);
        }
        const auto layers = Enumerate<VkLayerProperties>(
            [](uint32_t* count, auto* values)
            { return vkEnumerateInstanceLayerProperties(count, values); });
        const bool hasValidation = std::ranges::any_of(
            layers, [](const auto& layer)
            { return std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0; });
        const bool hasDebugUtils = HasExtension(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        std::vector<const char*> enabledLayers;
        if (options.validation != ValidationMode::Off)
        {
            if (hasValidation && hasDebugUtils)
            {
                report_.validationEnabled = true;
                enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
                enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            }
            else if (options.validation == ValidationMode::On)
            {
                throw std::runtime_error("Vulkan validation requires VK_LAYER_KHRONOS_validation "
                                         "and VK_EXT_debug_utils");
            }
            else
            {
                Warn("Vulkan validation unavailable; install Vulkan SDK validation layers for "
                     "--validation on");
            }
        }
        VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
        debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugInfo.pfnUserCallback = DebugCallback;
        debugInfo.pUserData = this;
        VkValidationFeaturesEXT validationFeatures{};
        validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        const VkValidationFeatureEnableEXT synchronization =
            VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        if (report_.validationEnabled)
        {
            const auto layerExtensions = Enumerate<VkExtensionProperties>(
                [](uint32_t* count, auto* values)
                {
                    return vkEnumerateInstanceExtensionProperties("VK_LAYER_KHRONOS_validation",
                                                                  count, values);
                });
            if (!HasExtension(layerExtensions, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME))
                throw std::runtime_error(
                    "Validation layer lacks synchronization validation settings");
            enabledExtensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
            validationFeatures.enabledValidationFeatureCount = 1;
            validationFeatures.pEnabledValidationFeatures = &synchronization;
            validationFeatures.pNext = &debugInfo;
            report_.details.emplace_back("synchronizationValidation", "enabled");
        }
        VkApplicationInfo application{};
        application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.pApplicationName = "RenderLab Sandbox";
        application.pEngineName = "RenderLab";
        application.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo instanceInfo{};
        instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.pApplicationInfo = &application;
        instanceInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
        instanceInfo.ppEnabledExtensionNames = enabledExtensions.data();
        instanceInfo.enabledLayerCount = static_cast<uint32_t>(enabledLayers.size());
        instanceInfo.ppEnabledLayerNames = enabledLayers.data();
        // Also capture instance creation/destruction messages outside the explicit messenger's
        // lifetime.
        instanceInfo.pNext = report_.validationEnabled ? &validationFeatures : nullptr;
        Check(vkCreateInstance(&instanceInfo, nullptr, &instance_), "vkCreateInstance");
        volkLoadInstance(instance_);
        if (report_.validationEnabled)
        {
            Check(vkCreateDebugUtilsMessengerEXT(instance_, &debugInfo, nullptr, &messenger_),
                  "vkCreateDebugUtilsMessengerEXT");
        }
        const auto physicalDevices = Enumerate<VkPhysicalDevice>(
            [&](uint32_t* count, auto* values)
            { return vkEnumeratePhysicalDevices(instance_, count, values); });
        std::vector<VkPhysicalDeviceProperties> properties(physicalDevices.size());
        for (size_t index = 0; index < physicalDevices.size(); ++index)
        {
            auto& props = properties[index];
            vkGetPhysicalDeviceProperties(physicalDevices[index], &props);
            VkPhysicalDeviceMemoryProperties memory{};
            vkGetPhysicalDeviceMemoryProperties(physicalDevices[index], &memory);
            uint64_t localMemory = 0;
            for (uint32_t heap = 0; heap < memory.memoryHeapCount; ++heap)
            {
                if ((memory.memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
                {
                    localMemory += memory.memoryHeaps[heap].size;
                }
            }
            report_.adapters.push_back({static_cast<uint32_t>(index), props.deviceName,
                                        props.vendorID, props.deviceID, localMemory,
                                        props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU});
        }
        if (options.adapterIndex && *options.adapterIndex >= physicalDevices.size())
        {
            throw std::runtime_error(
                std::format("Vulkan adapter index {} out of range ({} adapters)",
                            *options.adapterIndex, physicalDevices.size()));
        }
        std::vector<uint32_t> order(physicalDevices.size());
        // Sort candidate indices without changing the indices exposed in reports and --adapter.
        std::iota(order.begin(), order.end(), 0u);
        const auto priority = [&](uint32_t index)
        {
            const auto type = properties[index].deviceType;
            return type == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 3
                   : type == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                   : type == VK_PHYSICAL_DEVICE_TYPE_CPU            ? 0
                                                                    : 1;
        };
        std::stable_sort(order.begin(), order.end(),
                         [&](uint32_t a, uint32_t b) { return priority(a) > priority(b); });
        for (const auto index : order)
        {
            if (options.adapterIndex ? index != *options.adapterIndex
                                     : report_.adapters[index].software)
            {
                continue;
            }
            if (SelectPhysicalDevice(physicalDevices[index], properties[index]))
            {
                report_.selectedAdapter = index;
                break;
            }
        }
        if (physical_ == VK_NULL_HANDLE)
        {
            throw std::runtime_error(
                "No selected GPU meets Vulkan 1.3, dynamic rendering, synchronization2, shader "
                "demote, timeline semaphore and Win32 presentation requirements");
        }
        // Request the base runtime features, including DXC clip/discard helper invocations.
        VkPhysicalDeviceVulkan12Features enabled12{};
        enabled12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        enabled12.timelineSemaphore = VK_TRUE;
        enabled12.drawIndirectCount = report_.supported.indirectCount;
        VkPhysicalDeviceFeatures enabledBase{};
        enabledBase.multiDrawIndirect = report_.supported.multiDrawIndirect;
        enabledBase.drawIndirectFirstInstance = report_.supported.indirectFirstInstance;
        enabled12.bufferDeviceAddress = report_.supported.accelerationStructure;
        VkPhysicalDeviceVulkan13Features enabled13{};
        enabled13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        enabled13.dynamicRendering = VK_TRUE;
        enabled13.synchronization2 = VK_TRUE;
        enabled13.shaderDemoteToHelperInvocation = VK_TRUE;
        enabled13.pNext = &enabled12;
        VkPhysicalDeviceAccelerationStructureFeaturesKHR enabledAcceleration{};
        enabledAcceleration.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        enabledAcceleration.accelerationStructure = report_.supported.accelerationStructure;
        VkPhysicalDeviceRayQueryFeaturesKHR enabledRayQuery{};
        enabledRayQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        enabledRayQuery.rayQuery = report_.supported.rayQuery;
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR enabledRayPipeline{};
        enabledRayPipeline.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
        enabledRayPipeline.rayTracingPipeline = report_.supported.rayTracingPipeline;
        void** featureTail = &enabled12.pNext;
        if (report_.supported.accelerationStructure)
        {
            *featureTail = &enabledAcceleration;
            featureTail = &enabledAcceleration.pNext;
        }
        if (report_.supported.rayQuery)
        {
            *featureTail = &enabledRayQuery;
            featureTail = &enabledRayQuery.pNext;
        }
        if (report_.supported.rayTracingPipeline)
        {
            *featureTail = &enabledRayPipeline;
            featureTail = &enabledRayPipeline.pNext;
        }
        const float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = queueFamily_;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;
        std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        if (report_.supported.accelerationStructure)
        {
            deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        }
        if (report_.supported.rayQuery)
            deviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        if (report_.supported.rayTracingPipeline)
            deviceExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.pNext = &enabled13;
        deviceInfo.pEnabledFeatures = &enabledBase;
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
        Check(vkCreateDevice(physical_, &deviceInfo, nullptr, &device_), "vkCreateDevice");
        // Device dispatch is loaded only after creation; volk loads global/instance dispatch
        // earlier.
        volkLoadDevice(device_);
        vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
        if (queue_ == VK_NULL_HANDLE)
        {
            throw std::runtime_error("vkGetDeviceQueue returned a null queue");
        }
        report_.details.emplace_back(
            "enabledFeatures",
            "dynamicRendering, synchronization2, "
            "shaderDemoteToHelperInvocation, timelineSemaphore" +
                std::string(report_.supported.accelerationStructure ? "; accelerationStructure"
                                                                    : "") +
                (report_.supported.rayQuery ? "; rayQuery" : "") +
                (report_.supported.rayTracingPipeline ? "; rayTracingPipeline" : "") +
                (report_.supported.multiDrawIndirect ? "; multiDrawIndirect" : "") +
                (report_.supported.indirectCount ? "; drawIndirectCount" : "") +
                (report_.supported.indirectFirstInstance ? "; drawIndirectFirstInstance" : "") +
                "; one graphics queue");
    }

    const DeviceReport& GetReport() const override
    {
        return report_;
    }
    std::unique_ptr<Runtime> CreateRuntime() override
    {
        return CreateVulkanRuntime(
            instance_, physical_, device_, queue_, queueFamily_, report_.validationEnabled,
            report_.supported.accelerationStructure, report_.supported.rayQuery,
            report_.supported.rayTracingPipeline);
    }
    void PollDiagnostics() override {}
    uint32_t GetValidationErrorCount() const override
    {
        return validationErrors_.load();
    }

private:
    bool SelectPhysicalDevice(VkPhysicalDevice physical, const VkPhysicalDeviceProperties& props)
    {
        if (props.apiVersion < VK_API_VERSION_1_3)
        {
            return false;
        }
        const auto extensions = Enumerate<VkExtensionProperties>(
            [&](uint32_t* count, auto* values)
            { return vkEnumerateDeviceExtensionProperties(physical, nullptr, count, values); });
        if (!HasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
        {
            return false;
        }
        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features13.pNext = &features12;
        VkPhysicalDeviceFeatures2 features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &features13;
        VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration{};
        acceleration.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        VkPhysicalDeviceRayQueryFeaturesKHR rayQuery{};
        rayQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayPipeline{};
        rayPipeline.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
        VkPhysicalDeviceMeshShaderFeaturesEXT mesh{};
        mesh.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
        void** tail = &features12.pNext;
        // Chain local query structures only for extensions advertised by this physical device.
        const auto append = [&](auto& feature)
        {
            *tail = &feature;
            tail = &feature.pNext;
        };
        if (HasExtension(extensions, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME))
        {
            append(acceleration);
        }
        if (HasExtension(extensions, VK_KHR_RAY_QUERY_EXTENSION_NAME))
        {
            append(rayQuery);
        }
        if (HasExtension(extensions, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME))
        {
            append(rayPipeline);
        }
        if (HasExtension(extensions, VK_EXT_MESH_SHADER_EXTENSION_NAME))
        {
            append(mesh);
        }
        vkGetPhysicalDeviceFeatures2(physical, &features);
        if (!features12.timelineSemaphore || !features13.dynamicRendering ||
            !features13.synchronization2 || !features13.shaderDemoteToHelperInvocation)
        {
            return false;
        }
        uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &queueCount, queues.data());
        std::optional<uint32_t> selectedQueue;
        // M0 uses one family for graphics, compute and presentation; separate queues come later.
        for (uint32_t index = 0; index < queueCount; ++index)
        {
            const auto flags = queues[index].queueFlags;
            if (queues[index].queueCount > 0 && (flags & VK_QUEUE_GRAPHICS_BIT) != 0 &&
                (flags & VK_QUEUE_COMPUTE_BIT) != 0 &&
                vkGetPhysicalDeviceWin32PresentationSupportKHR(physical, index))
            {
                selectedQueue = index;
                break;
            }
        }
        if (!selectedQueue)
        {
            return false;
        }
        physical_ = physical;
        queueFamily_ = *selectedQueue;
        report_.apiVersion = Version(props.apiVersion);
        report_.shaderTarget = "SPIR-V 1.6 (Vulkan 1.3 target); M1 build-time DXC";
        VkPhysicalDeviceDriverProperties driver{};
        driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &driver;
        vkGetPhysicalDeviceProperties2(physical, &props2);
        report_.driverVersion = std::format("{}; {} (raw 0x{:08X})", driver.driverName,
                                            driver.driverInfo, props.driverVersion);
        for (uint32_t index = 0; index < queueCount; ++index)
        {
            const auto flags = queues[index].queueFlags;
            report_.queues.push_back(
                {"Vulkan queue family", index, queues[index].queueCount,
                 (flags & VK_QUEUE_GRAPHICS_BIT) != 0, (flags & VK_QUEUE_COMPUTE_BIT) != 0,
                 (flags & (VK_QUEUE_TRANSFER_BIT | VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) !=
                     0,
                 index == queueFamily_});
        }
        auto& caps = report_.supported;
        caps.indirectDraw = true;
        caps.multiDrawIndirect = features.features.multiDrawIndirect != VK_FALSE;
        caps.indirectCount = features12.drawIndirectCount != VK_FALSE;
        caps.indirectFirstInstance = features.features.drawIndirectFirstInstance != VK_FALSE;
        report_.details.emplace_back("maxDrawIndirectCount",
                                     std::to_string(props.limits.maxDrawIndirectCount));
        caps.descriptorIndexing = features12.descriptorIndexing &&
                                  features12.runtimeDescriptorArray &&
                                  features12.shaderSampledImageArrayNonUniformIndexing;
        caps.accelerationStructure =
            HasExtension(extensions, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
            acceleration.accelerationStructure && features12.bufferDeviceAddress &&
            HasExtension(extensions, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        caps.rayQuery = caps.accelerationStructure &&
                        HasExtension(extensions, VK_KHR_RAY_QUERY_EXTENSION_NAME) &&
                        rayQuery.rayQuery;
        caps.rayTracingPipeline =
            caps.accelerationStructure &&
            HasExtension(extensions, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) &&
            rayPipeline.rayTracingPipeline;
        caps.meshShader = mesh.meshShader != VK_FALSE;
        caps.maxTexture2D = props.limits.maxImageDimension2D;
        caps.maxColorAttachments = props.limits.maxColorAttachments;
        report_.details.emplace_back("requestedApiVersion", "1.3.0");
        report_.details.emplace_back(
            "maxPerStageSampledImages",
            std::to_string(props.limits.maxPerStageDescriptorSampledImages));
        report_.details.emplace_back("maxPushConstantsSize",
                                     std::to_string(props.limits.maxPushConstantsSize));
        report_.details.emplace_back("bufferDeviceAddressSupported",
                                     features12.bufferDeviceAddress ? "true" : "false");
        for (const auto& [name, format] :
             {std::pair{"RGBA8_UNORM", VK_FORMAT_R8G8B8A8_UNORM},
              std::pair{"RGBA16_FLOAT", VK_FORMAT_R16G16B16A16_SFLOAT},
              std::pair{"RGBA32_FLOAT", VK_FORMAT_R32G32B32A32_SFLOAT},
              std::pair{"R32_FLOAT", VK_FORMAT_R32_SFLOAT},
              std::pair{"D32_FLOAT", VK_FORMAT_D32_SFLOAT},
              std::pair{"D32_FLOAT_S8X24_UINT", VK_FORMAT_D32_SFLOAT_S8_UINT}})
        {
            VkFormatProperties support{};
            vkGetPhysicalDeviceFormatProperties(physical, format, &support);
            const auto flags = support.optimalTilingFeatures;
            report_.formats.push_back(
                {name, (flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0,
                 (flags & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0,
                 (flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0,
                 (flags & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0});
        }
        return true;
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL
    DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                  const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) noexcept
    {
        auto& self = *static_cast<VulkanDevice*>(user);
        const bool error = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0;
        if (error)
        {
            ++self.validationErrors_;
        }
        try
        {
            self.logger_.Write(error ? LogLevel::Error : LogLevel::Warning,
                               data->pMessage != nullptr ? data->pMessage
                                                         : "Vulkan validation message");
        }
        catch (...)
        {
            // Exceptions must never cross a driver callback boundary.
        }
        return VK_FALSE;
    }

    void Warn(std::string message)
    {
        logger_.Write(LogLevel::Warning, message);
        report_.warnings.push_back(std::move(message));
    }

    Logger& logger_;
    DeviceReport report_;
    std::atomic<uint32_t> validationErrors_ = 0;
    bool loaderInitialized_ = false;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
};
} // namespace

std::unique_ptr<Device> CreateVulkanDevice(const DeviceOptions& options, Logger& logger)
{
    auto device = std::make_unique<VulkanDevice>(logger);
    device->Initialize(options);
    return device;
}
} // namespace renderlab::rhi
