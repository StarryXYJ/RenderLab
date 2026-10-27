#include "rhi/vulkan/vulkan_common.hpp"

#include <format>
#include <stdexcept>

namespace renderlab::rhi::vulkan
{
void Check(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::format("{} failed (VkResult {})", operation, int(result)));
}
VkFormat NativeFormat(Format format)
{
    switch (format)
    {
    case Format::Rgba8:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::Bgra8:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::Rgba8Srgb:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::Bgra8Srgb:
        return VK_FORMAT_B8G8R8A8_SRGB;
    case Format::D32S8:
        return VK_FORMAT_D32_SFLOAT_S8_UINT;
    case Format::D32:
        return VK_FORMAT_D32_SFLOAT;
    case Format::Rgba32Float:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case Format::R32Float:
        return VK_FORMAT_R32_SFLOAT;
    }
    throw std::invalid_argument("Unknown format");
}
Access NativeState(State state)
{
    switch (state)
    {
    case State::Undefined:
        return {VK_PIPELINE_STAGE_2_NONE, 0, VK_IMAGE_LAYOUT_UNDEFINED};
    case State::Present:
        return {VK_PIPELINE_STAGE_2_NONE, 0, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    case State::HostWrite:
        return {VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL};
    case State::HostRead:
        return {VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT, VK_IMAGE_LAYOUT_GENERAL};
    case State::CopySource:
        return {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    case State::CopyDest:
        return {VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    case State::Vertex:
        return {VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL};
    case State::Index:
        return {VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, VK_ACCESS_2_INDEX_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL};
    case State::Indirect:
        return {VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL};
    case State::ShaderTableRead:
        return {VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_2_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL};
    case State::Storage:
        return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_IMAGE_LAYOUT_GENERAL};
    case State::UniformRead:
        return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_UNIFORM_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL};
    case State::ShaderRead:
        return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL};
    case State::Sampled:
        return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    case State::ColorAttachment:
        return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    case State::DepthWrite:
        return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    case State::AccelerationStructureBuildInput:
        return {VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL};
    case State::AccelerationStructureBuildWrite:
        return {VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR |
                    VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                VK_IMAGE_LAYOUT_GENERAL};
    case State::AccelerationStructureRead:
        return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR, VK_IMAGE_LAYOUT_GENERAL};
    }
    throw std::invalid_argument("Unknown state");
}
VkDescriptorType DescriptorType(BindingKind kind)
{
    switch (kind)
    {
    case BindingKind::StorageTexture:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case BindingKind::SampledTexture:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case BindingKind::Sampler:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    case BindingKind::ConstantBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case BindingKind::ReadOnlyBuffer:
    case BindingKind::StorageBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case BindingKind::AccelerationStructure:
        return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    }
    throw std::invalid_argument("Unknown binding kind");
}
} // namespace renderlab::rhi::vulkan
