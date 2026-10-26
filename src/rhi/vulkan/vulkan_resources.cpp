#include "platform/text.hpp"
#include "rhi/vulkan/vulkan_objects.hpp"
#include "rhi/vulkan/vulkan_runtime.hpp"

#include <stdexcept>

namespace renderlab::rhi::vulkan
{
std::shared_ptr<Buffer> VulkanRuntime::CreateBuffer(const BufferDesc& desc)
{
    auto cpuTimer = ProfileCpu(CpuOperation::BufferCreate);
    Validate(desc);
    auto buffer = std::make_shared<VkBufferObject>(this, desc);
    buffer->allocation.device = device_;
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = desc.size;
    info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                 VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    Check(vkCreateBuffer(device_, &info, nullptr, &buffer->native), "vkCreateBuffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, buffer->native, &requirements);
    Allocate(buffer->allocation, requirements, desc.memory);
    Check(vkBindBufferMemory(device_, buffer->native, buffer->allocation.memory, 0),
          "vkBindBufferMemory");
    Name(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(buffer->native), desc.name);
    return buffer;
}

std::shared_ptr<Texture> VulkanRuntime::CreateTexture(const TextureDesc& desc)
{
    Validate(desc);
    auto texture = std::make_shared<VkTexture>(this, desc);
    texture->allocation.device = device_;
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = NativeFormat(desc.format);
    info.extent = {desc.width, desc.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!IsDepthFormat(desc.format))
        info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (desc.storage)
        info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (desc.attachment)
        info.usage |= IsDepthFormat(desc.format) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                                                 : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkFormatProperties props{};
    vkGetPhysicalDeviceFormatProperties(physical_, info.format, &props);
    VkFormatFeatureFlags required =
        IsDepthFormat(desc.format) ? 0 : VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if (desc.storage)
        required |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    if (desc.attachment)
        required |= IsDepthFormat(desc.format) ? VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
                                               : VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    if ((props.optimalTilingFeatures & required) != required)
        throw std::runtime_error("Texture format lacks requested usage");
    Check(vkCreateImage(device_, &info, nullptr, &texture->native), "vkCreateImage");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, texture->native, &requirements);
    Allocate(texture->allocation, requirements, Memory::Device);
    Check(vkBindImageMemory(device_, texture->native, texture->allocation.memory, 0),
          "vkBindImageMemory");
    Name(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(texture->native), desc.name);
    return texture;
}

std::shared_ptr<TextureView> VulkanRuntime::CreateView(std::shared_ptr<Texture> texture)
{
    auto cpuTimer = ProfileCpu(CpuOperation::ViewCreate);
    CheckOwner(texture);
    auto view = std::make_shared<VkView>(texture);
    view->device = device_;
    VkImageViewCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = As<VkTexture>(texture).native;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = NativeFormat(texture->desc.format);
    info.subresourceRange = {
        VkImageAspectFlags(
            IsDepthFormat(texture->desc.format)
                ? VK_IMAGE_ASPECT_DEPTH_BIT |
                      (HasStencil(texture->desc.format) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0)
                : VK_IMAGE_ASPECT_COLOR_BIT),
        0, 1, 0, 1};
    Check(vkCreateImageView(device_, &info, nullptr, &view->native), "vkCreateImageView");
    return view;
}

std::shared_ptr<Sampler> VulkanRuntime::CreateSampler()
{
    auto sampler = std::make_shared<VkSamplerObject>(this);
    sampler->device = device_;
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = info.minFilter = VK_FILTER_NEAREST;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = info.addressModeV = info.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxLod = VK_LOD_CLAMP_NONE;
    Check(vkCreateSampler(device_, &info, nullptr, &sampler->native), "vkCreateSampler");
    return sampler;
}

void VulkanRuntime::Allocate(Allocation& allocation, const VkMemoryRequirements& requirements,
                             Memory kind)
{
    const VkMemoryPropertyFlags flags =
        kind == Memory::Device
            ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
            : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < memory_.memoryTypeCount; ++i)
        if ((requirements.memoryTypeBits & (1u << i)) &&
            (memory_.memoryTypes[i].propertyFlags & flags) == flags)
        {
            type = i;
            break;
        }
    if (type == UINT32_MAX)
        throw std::runtime_error(
            "No suitable memory type (M1 host buffers require coherent memory)");
    ledger_->Add(requirements.size);
    allocation.ledger = ledger_;
    allocation.bytes = requirements.size;
    VkMemoryAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    info.allocationSize = requirements.size;
    info.memoryTypeIndex = type;
    Check(vkAllocateMemory(device_, &info, nullptr, &allocation.memory), "vkAllocateMemory");
}

void VulkanRuntime::Name(VkObjectType type, uint64_t handle, const std::string& name)
{
    if (!debug_)
        return;
    VkDebugUtilsObjectNameInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name.c_str();
    Check(vkSetDebugUtilsObjectNameEXT(device_, &info), "SetObjectName");
}
} // namespace renderlab::rhi::vulkan
