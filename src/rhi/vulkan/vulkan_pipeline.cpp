#include "rhi/vulkan/vulkan_objects.hpp"
#include "rhi/vulkan/vulkan_runtime.hpp"

#include <algorithm>
#include <stdexcept>

namespace renderlab::rhi::vulkan
{
std::shared_ptr<BindingLayout> VulkanRuntime::CreateLayout(std::vector<BindingDesc> entries)
{
    auto layout = std::make_shared<VkLayout>(this, std::move(entries));
    layout->device = device_;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (auto entry : layout->entries)
    {
        if (std::ranges::count_if(layout->entries, [&](auto e) { return e.slot == entry.slot; }) !=
            1)
            throw std::invalid_argument("Duplicate binding slot");
        bindings.push_back({entry.slot, DescriptorType(entry.kind), 1,
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                                VK_SHADER_STAGE_COMPUTE_BIT,
                            nullptr});
    }
    VkDescriptorSetLayoutCreateInfo set{};
    set.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set.bindingCount = static_cast<uint32_t>(bindings.size());
    set.pBindings = bindings.data();
    Check(vkCreateDescriptorSetLayout(device_, &set, nullptr, &layout->set), "DescriptorSetLayout");
    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = 1;
    info.pSetLayouts = &layout->set;
    Check(vkCreatePipelineLayout(device_, &info, nullptr, &layout->native), "PipelineLayout");
    return layout;
}

std::shared_ptr<BindingGroup> VulkanRuntime::CreateGroup(std::shared_ptr<BindingLayout> layout,
                                                         std::vector<Binding> bindings)
{
    Validate(layout, bindings);
    auto group = std::make_shared<VkGroup>(layout, std::move(bindings));
    group->device = device_;
    std::vector<VkDescriptorPoolSize> sizes;
    for (auto kind :
         {BindingKind::StorageTexture, BindingKind::SampledTexture, BindingKind::Sampler})
    {
        const auto count = static_cast<uint32_t>(
            std::ranges::count_if(layout->entries, [&](auto e) { return e.kind == kind; }));
        if (count)
            sizes.push_back({DescriptorType(kind), count});
    }
    VkDescriptorPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = 1;
    pool.poolSizeCount = static_cast<uint32_t>(sizes.size());
    pool.pPoolSizes = sizes.data();
    Check(vkCreateDescriptorPool(device_, &pool, nullptr, &group->pool), "DescriptorPool");
    VkDescriptorSetAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptorPool = group->pool;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &As<VkLayout>(layout).set;
    Check(vkAllocateDescriptorSets(device_, &allocate, &group->native), "DescriptorSet");
    for (auto entry : layout->entries)
    {
        const auto& binding = *std::ranges::find_if(group->bindings, [&](const Binding& b)
                                                    { return b.slot == entry.slot; });
        VkDescriptorImageInfo image{};
        if (entry.kind == BindingKind::Sampler)
            image.sampler = As<VkSamplerObject>(binding.sampler).native;
        else
        {
            image.imageView = As<VkView>(binding.view).native;
            image.imageLayout = entry.kind == BindingKind::StorageTexture
                                    ? VK_IMAGE_LAYOUT_GENERAL
                                    : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = group->native;
        write.dstBinding = entry.slot;
        write.descriptorCount = 1;
        write.descriptorType = DescriptorType(entry.kind);
        write.pImageInfo = &image;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }
    return group;
}

std::shared_ptr<Pipeline> VulkanRuntime::CreatePipeline(PipelineDesc desc)
{
    Validate(desc);
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
        raster.cullMode = desc.cull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
        // Negative viewport height matches DX12's clip-space Y; CCW is expressed in clip space.
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo sample{};
        sample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        sample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = depth.depthWriteEnable = desc.depth;
        depth.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.blendEnable = desc.blend;
        attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.colorBlendOp = VK_BLEND_OP_ADD;
        attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        attachment.colorWriteMask = 15;
        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments = &attachment;
        VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = states;
        const auto colorFormat = NativeFormat(desc.color);
        VkPipelineRenderingCreateInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &colorFormat;
        rendering.depthAttachmentFormat = desc.depth ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_UNDEFINED;
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
