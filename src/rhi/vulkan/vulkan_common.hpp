#pragma once

#include "rhi/resources.hpp"

#include <volk.h>

#include <memory>

namespace renderlab::rhi::vulkan
{
void Check(VkResult result, const char* operation);
VkFormat NativeFormat(Format format);
struct Access
{
    VkPipelineStageFlags2 stage;
    VkAccessFlags2 access;
    VkImageLayout layout;
};
Access NativeState(State state);
VkDescriptorType DescriptorType(BindingKind kind);

template <class T, class U> T& As(const std::shared_ptr<U>& object)
{
    return static_cast<T&>(*object);
}
} // namespace renderlab::rhi::vulkan
