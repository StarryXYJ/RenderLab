#include "rhi/vulkan/vulkan_objects.hpp"
#include "rhi/vulkan/vulkan_runtime.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace renderlab::rhi::vulkan
{
namespace
{
VkCompareOp Native(CompareOp value)
{
    constexpr std::array values = {VK_COMPARE_OP_NEVER,
                                   VK_COMPARE_OP_LESS,
                                   VK_COMPARE_OP_EQUAL,
                                   VK_COMPARE_OP_LESS_OR_EQUAL,
                                   VK_COMPARE_OP_GREATER,
                                   VK_COMPARE_OP_NOT_EQUAL,
                                   VK_COMPARE_OP_GREATER_OR_EQUAL,
                                   VK_COMPARE_OP_ALWAYS};
    return values.at(static_cast<size_t>(value));
}
VkStencilOp Native(StencilOp value)
{
    constexpr std::array values = {VK_STENCIL_OP_KEEP,
                                   VK_STENCIL_OP_ZERO,
                                   VK_STENCIL_OP_REPLACE,
                                   VK_STENCIL_OP_INCREMENT_AND_CLAMP,
                                   VK_STENCIL_OP_DECREMENT_AND_CLAMP,
                                   VK_STENCIL_OP_INVERT,
                                   VK_STENCIL_OP_INCREMENT_AND_WRAP,
                                   VK_STENCIL_OP_DECREMENT_AND_WRAP};
    return values.at(static_cast<size_t>(value));
}
VkBlendFactor Native(BlendFactor value)
{
    constexpr std::array values = {VK_BLEND_FACTOR_ZERO,
                                   VK_BLEND_FACTOR_ONE,
                                   VK_BLEND_FACTOR_SRC_COLOR,
                                   VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
                                   VK_BLEND_FACTOR_DST_COLOR,
                                   VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
                                   VK_BLEND_FACTOR_SRC_ALPHA,
                                   VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                   VK_BLEND_FACTOR_DST_ALPHA,
                                   VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
                                   VK_BLEND_FACTOR_SRC_ALPHA_SATURATE};
    return values.at(static_cast<size_t>(value));
}
VkBlendOp Native(BlendOp value)
{
    constexpr std::array values = {VK_BLEND_OP_ADD, VK_BLEND_OP_SUBTRACT,
                                   VK_BLEND_OP_REVERSE_SUBTRACT, VK_BLEND_OP_MIN, VK_BLEND_OP_MAX};
    return values.at(static_cast<size_t>(value));
}
VkStencilOpState Native(const StencilFaceState& face, const StencilState& stencil)
{
    return {Native(face.fail),
            Native(face.pass),
            Native(face.depthFail),
            Native(face.compare),
            stencil.readMask,
            stencil.writeMask,
            0};
}
} // namespace

std::shared_ptr<BindingLayout> VulkanRuntime::CreateLayout(std::vector<BindingDesc> entries)
{
    auto layout = std::make_shared<VkLayout>(this, std::move(entries));
    layout->device = device_;
    std::array<std::vector<VkDescriptorSetLayoutBinding>, 4> bindings;
    for (auto entry : layout->entries)
    {
        if (entry.space >= 4 ||
            std::ranges::count_if(layout->entries, [&](auto e)
                                  { return e.slot == entry.slot && e.space == entry.space; }) != 1)
            throw std::invalid_argument("Duplicate binding slot");
        bindings[entry.space].push_back({entry.slot, DescriptorType(entry.kind), 1,
                                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                                             VK_SHADER_STAGE_COMPUTE_BIT,
                                         nullptr});
    }
    VkDescriptorSetLayoutCreateInfo set{};
    set.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    for (uint32_t space = 0; space < 4; ++space)
    {
        set.bindingCount = static_cast<uint32_t>(bindings[space].size());
        set.pBindings = bindings[space].data();
        Check(vkCreateDescriptorSetLayout(device_, &set, nullptr, &layout->sets[space]),
              "DescriptorSetLayout");
    }
    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = 4;
    info.pSetLayouts = layout->sets.data();
    Check(vkCreatePipelineLayout(device_, &info, nullptr, &layout->native), "PipelineLayout");
    return layout;
}

std::shared_ptr<VkDescriptorBlock>
VulkanRuntime::AcquireDescriptors(std::shared_ptr<BindingLayout> layout)
{
    for (const auto& allocation : descriptorBlocks_)
        if (allocation.use_count() == 1 && allocation->layout == layout)
            return allocation;
    if (descriptorBlocks_.size() == 256)
    {
        const auto idle = std::ranges::find_if(descriptorBlocks_, [](const auto& item)
                                               { return item.use_count() == 1; });
        if (idle == descriptorBlocks_.end())
            throw std::runtime_error(
                "Vulkan descriptor block budget exhausted; release groups or retire work");
        descriptorBlocks_.erase(idle);
    }
    auto allocation = std::make_shared<VkDescriptorBlock>();
    allocation->device = device_;
    allocation->layout = layout;
    std::vector<VkDescriptorPoolSize> sizes;
    for (auto type :
         {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
          VK_DESCRIPTOR_TYPE_SAMPLER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR})
    {
        const auto count = static_cast<uint32_t>(std::ranges::count_if(
            layout->entries, [&](auto e) { return DescriptorType(e.kind) == type; }));
        if (count)
            sizes.push_back({type, count});
    }
    VkDescriptorPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = 4;
    pool.poolSizeCount = static_cast<uint32_t>(sizes.size());
    pool.pPoolSizes = sizes.data();
    {
        auto cpuTimer = ProfileCpu(CpuOperation::DescriptorPageCreate);
        Check(vkCreateDescriptorPool(device_, &pool, nullptr, &allocation->pool), "DescriptorPool");
    }
    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = allocation->pool;
    allocate.descriptorSetCount = 4;
    allocate.pSetLayouts = As<VkLayout>(layout).sets.data();
    Check(vkAllocateDescriptorSets(device_, &allocate, allocation->native.data()), "DescriptorSet");
    allocation->stats = descriptorStats_;
    ++descriptorStats_->pages;
    ++descriptorStats_->creations;
    descriptorStats_->capacity += layout->entries.size();
    descriptorBlocks_.push_back(allocation);
    return allocation;
}

std::shared_ptr<BindingGroup> VulkanRuntime::CreateGroup(std::shared_ptr<BindingLayout> layout,
                                                         std::vector<Binding> bindings)
{
    auto cpuTimer = ProfileCpu(CpuOperation::GroupCreate);
    Validate(layout, bindings);
    auto group = std::make_shared<VkGroup>(layout, std::move(bindings));
    group->allocation = AcquireDescriptors(layout);
    descriptorStats_->inUse += layout->entries.size();
    descriptorStats_->highWater = std::max(descriptorStats_->highWater, descriptorStats_->inUse);
    group->native = group->allocation->native;
    for (auto entry : layout->entries)
    {
        const auto& binding =
            *std::ranges::find_if(group->bindings, [&](const Binding& b)
                                  { return b.slot == entry.slot && b.space == entry.space; });
        VkDescriptorImageInfo image{};
        VkDescriptorBufferInfo buffer{};
        const bool isBuffer = entry.kind == BindingKind::ConstantBuffer ||
                              entry.kind == BindingKind::ReadOnlyBuffer ||
                              entry.kind == BindingKind::StorageBuffer;
        if (isBuffer)
        {
            buffer = {As<VkBufferObject>(binding.buffer).native, binding.offset, binding.size};
        }
        else if (entry.kind == BindingKind::Sampler)
            image.sampler = As<VkSamplerObject>(binding.sampler).native;
        else if (entry.kind != BindingKind::AccelerationStructure)
        {
            image.imageView = As<VkView>(binding.view).native;
            image.imageLayout = entry.kind == BindingKind::StorageTexture
                                    ? VK_IMAGE_LAYOUT_GENERAL
                                    : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = group->native[entry.space];
        write.dstBinding = entry.slot;
        write.descriptorCount = 1;
        write.descriptorType = DescriptorType(entry.kind);
        VkWriteDescriptorSetAccelerationStructureKHR acceleration{};
        VkAccelerationStructureKHR nativeAcceleration = VK_NULL_HANDLE;
        if (entry.kind == BindingKind::AccelerationStructure)
        {
            acceleration.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
            nativeAcceleration = As<VkAccelerationStructure>(binding.accelerationStructure).native;
            acceleration.accelerationStructureCount = 1;
            acceleration.pAccelerationStructures = &nativeAcceleration;
            write.pNext = &acceleration;
        }
        write.pImageInfo =
            isBuffer || entry.kind == BindingKind::AccelerationStructure ? nullptr : &image;
        write.pBufferInfo = isBuffer ? &buffer : nullptr;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }
    return group;
}

std::shared_ptr<Pipeline> VulkanRuntime::CreatePipelineNative(PipelineDesc desc)
{
    auto pipeline = std::make_shared<VkPipelineObject>(this, desc);
    pipeline->device = device_;
    struct Modules
    {
        VkDevice device;
        std::vector<VkShaderModule> values;
        ~Modules()
        {
            for (auto value : values)
                vkDestroyShaderModule(device, value, nullptr);
        }
    } modules{device_, {}};
    const auto stage = [&](const std::shared_ptr<ShaderModule>& shader, VkShaderStageFlagBits flag)
    {
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = shader->code.size();
        info.pCode = reinterpret_cast<const uint32_t*>(shader->code.data());
        VkShaderModule module{};
        Check(vkCreateShaderModule(device_, &info, nullptr, &module), "ShaderModule");
        modules.values.push_back(module);
        VkPipelineShaderStageCreateInfo result{};
        result.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        result.stage = flag;
        result.module = module;
        result.pName = shader->entry.c_str();
        return result;
    };
    if (desc.compute)
    {
        VkComputePipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        info.stage = stage(desc.compute, VK_SHADER_STAGE_COMPUTE_BIT);
        info.layout = As<VkLayout>(desc.layout).native;
        Check(
            vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline->native),
            "ComputePipeline");
    }
    else
    {
        VkPipelineShaderStageCreateInfo stages[] = {
            stage(desc.vertex, VK_SHADER_STAGE_VERTEX_BIT),
            stage(desc.pixel, VK_SHADER_STAGE_FRAGMENT_BIT)};
        VkVertexInputBindingDescription binding{0, desc.vertexStride, VK_VERTEX_INPUT_RATE_VERTEX};
        std::vector<VkVertexInputAttributeDescription> attributes;
        for (const auto& attribute : desc.attributes)
        {
            const auto format = attribute.format == VertexFormat::Float2 ? VK_FORMAT_R32G32_SFLOAT
                                : attribute.format == VertexFormat::Float3
                                    ? VK_FORMAT_R32G32B32_SFLOAT
                                    : VK_FORMAT_R32G32B32A32_SFLOAT;
            attributes.push_back({attribute.location, 0, format, attribute.offset});
        }
        VkPipelineVertexInputStateCreateInfo vertex{};
        vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex.vertexBindingDescriptionCount = 1;
        vertex.pVertexBindingDescriptions = &binding;
        vertex.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertex.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{};
        assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{};
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = desc.raster.cull == CullMode::Back    ? VK_CULL_MODE_BACK_BIT
                          : desc.raster.cull == CullMode::Front ? VK_CULL_MODE_FRONT_BIT
                                                                : VK_CULL_MODE_NONE;
        // Negative viewport height matches DX12's clip-space Y; CCW is expressed in clip space.
        raster.frontFace = desc.raster.frontFace == FrontFace::CounterClockwise
                               ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                               : VK_FRONT_FACE_CLOCKWISE;
        raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo sample{};
        sample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        sample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = desc.depth.testEnabled || desc.depth.writeEnabled;
        depth.depthWriteEnable = desc.depth.writeEnabled;
        depth.depthCompareOp =
            Native(desc.depth.testEnabled ? desc.depth.compare : CompareOp::Always);
        depth.stencilTestEnable = desc.stencil.enabled;
        depth.front = Native(desc.stencil.front, desc.stencil);
        depth.back = Native(desc.stencil.back, desc.stencil);
        std::vector<VkPipelineColorBlendAttachmentState> attachments(desc.colorFormats.size());
        for (auto& attachment : attachments)
        {
            attachment.blendEnable = desc.blend.enabled;
            attachment.srcColorBlendFactor = Native(desc.blend.srcColor);
            attachment.dstColorBlendFactor = Native(desc.blend.dstColor);
            attachment.colorBlendOp = Native(desc.blend.colorOp);
            attachment.srcAlphaBlendFactor = Native(desc.blend.srcAlpha);
            attachment.dstAlphaBlendFactor = Native(desc.blend.dstAlpha);
            attachment.alphaBlendOp = Native(desc.blend.alphaOp);
            attachment.colorWriteMask = desc.raster.colorWriteMask;
        }
        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = static_cast<uint32_t>(attachments.size());
        blend.pAttachments = attachments.data();
        VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                   VK_DYNAMIC_STATE_STENCIL_REFERENCE};
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = 3;
        dynamic.pDynamicStates = states;
        std::vector<VkFormat> colorFormats;
        colorFormats.reserve(desc.colorFormats.size());
        for (const auto format : desc.colorFormats)
            colorFormats.push_back(NativeFormat(format));
        VkPipelineRenderingCreateInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        rendering.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
        rendering.pColorAttachmentFormats = colorFormats.data();
        rendering.depthAttachmentFormat =
            desc.depthStencilFormat ? NativeFormat(*desc.depthStencilFormat) : VK_FORMAT_UNDEFINED;
        rendering.stencilAttachmentFormat =
            desc.depthStencilFormat && HasStencil(*desc.depthStencilFormat)
                ? rendering.depthAttachmentFormat
                : VK_FORMAT_UNDEFINED;
        VkGraphicsPipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.pNext = &rendering;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertex;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &sample;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = As<VkLayout>(desc.layout).native;
        Check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr,
                                        &pipeline->native),
              "GraphicsPipeline");
    }
    Name(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(pipeline->native),
         desc.compute ? "M1.Compute" : "M1.Graphics");
    return pipeline;
}
} // namespace renderlab::rhi::vulkan
