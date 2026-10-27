#include "rhi/runtime.hpp"

#include "rhi/commands.hpp"
#include "rhi/detail/validation.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace renderlab::rhi
{
using detail::Require;

Runtime::Runtime() = default;
Runtime::~Runtime() = default;

void Runtime::SetAllocationBudget(uint64_t bytes)
{
    if (std::this_thread::get_id() != ownerThread_)
        throw std::logic_error("Allocation budget requires runtime owner thread");
    Require(bytes > 0 && bytes >= ledger_->stats.used,
            "Allocation budget must be nonzero and cover current native usage");
    ledger_->stats.budget = bytes;
}

void Runtime::CheckOwner(const std::shared_ptr<Object>& object) const
{
    Require(object && object->owner == this, "Null object or object from another runtime");
}
void Runtime::Validate(const BufferDesc& desc) const
{
    Require(desc.size > 0 && desc.size <= ledger_->stats.budget, "Invalid buffer size");
    Require(!desc.shaderTable || (desc.deviceAddress && SupportsRayTracingPipeline() &&
                                  desc.memory == Memory::Upload),
            "Invalid shader table buffer");
    Require(!desc.storage || desc.memory == Memory::Device,
            "Storage buffers require device memory");
    Require(!desc.deviceAddress || SupportsAccelerationStructures(),
            "Device-address buffers require enabled acceleration structure capabilities");
    Require((!desc.deviceAddress && !desc.accelerationStructureInput &&
             !desc.accelerationStructureStorage) ||
                desc.memory != Memory::Readback,
            "Readback buffers cannot participate in acceleration structure operations");
    Require(!desc.accelerationStructureInput || desc.deviceAddress,
            "Acceleration structure input requires a device address");
    Require(!desc.accelerationStructureStorage ||
                (desc.deviceAddress && desc.memory == Memory::Device),
            "Acceleration structure storage requires device-local addressable memory");
}
void Runtime::Validate(const TextureDesc& desc) const
{
    Require(desc.width && desc.height && desc.width <= 16384 && desc.height <= 16384,
            "Texture dimensions must be in [1, 16384]");
    Require(!IsDepthFormat(desc.format) || (desc.attachment && !desc.storage),
            "Depth texture requires attachment usage and forbids storage usage");
    Require(!desc.storage || desc.format == Format::Rgba8 || desc.format == Format::Rgba32Float ||
                desc.format == Format::R32Float,
            "Storage textures require RGBA8_UNORM, R32_FLOAT or RGBA32_FLOAT");
}
void Runtime::Validate(const AccelerationStructureDesc& desc) const
{
    Require(!desc.name.empty(), "Acceleration structure requires a name");
    if (desc.topLevel)
    {
        Require(desc.geometries.empty(),
                "Top-level acceleration structure requires instances only");
        Require(desc.instances.size() <= 0xFFFFFF, "Too many acceleration structure instances");
        for (const auto& instance : desc.instances)
        {
            CheckOwner(instance.bottomLevel);
            Require(!instance.bottomLevel->desc.topLevel && instance.mask <= 0xFF &&
                        instance.instanceId <= 0xFFFFFF,
                    "Invalid top-level acceleration structure instance");
            for (const auto value : instance.transform)
                Require(std::isfinite(value), "Instance transform must be finite");
            const auto& m = instance.transform;
            const auto determinant = m[0] * (m[5] * m[10] - m[6] * m[9]) -
                                     m[1] * (m[4] * m[10] - m[6] * m[8]) +
                                     m[2] * (m[4] * m[9] - m[5] * m[8]);
            Require(std::abs(determinant) > 1e-12f, "Instance transform must be invertible");
        }
    }
    else
    {
        Require(desc.instances.empty() && !desc.geometries.empty(),
                "Bottom-level acceleration structure requires triangle geometry only");
        for (const auto& geometry : desc.geometries)
        {
            CheckOwner(geometry.vertices);
            if (geometry.indices)
                CheckOwner(geometry.indices);
            Require(geometry.vertices->desc.deviceAddress &&
                        geometry.vertices->desc.accelerationStructureInput &&
                        geometry.vertexCount >= 3 && geometry.vertexStride >= 12 &&
                        geometry.vertexStride % 4 == 0 && geometry.vertexOffset % 4 == 0 &&
                        geometry.vertexOffset <= geometry.vertices->desc.size &&
                        uint64_t(geometry.vertexCount) * geometry.vertexStride <=
                            geometry.vertices->desc.size - geometry.vertexOffset,
                    "Invalid triangle vertex buffer for acceleration structure");
            if (geometry.indices)
                Require(geometry.indices->desc.deviceAddress &&
                            geometry.indices->desc.accelerationStructureInput &&
                            geometry.indexCount >= 3 && geometry.indexCount % 3 == 0 &&
                            geometry.indexOffset % 4 == 0 &&
                            geometry.indexOffset <= geometry.indices->desc.size &&
                            uint64_t(geometry.indexCount) * sizeof(uint32_t) <=
                                geometry.indices->desc.size - geometry.indexOffset,
                        "Invalid triangle index buffer for acceleration structure");
            else
                Require(geometry.indexCount == 0 && geometry.indexOffset == 0 &&
                            geometry.vertexCount % 3 == 0,
                        "Index data requires an index buffer");
        }
    }
}
void Runtime::Validate(const PipelineDesc& desc) const
{
    CheckOwner(desc.layout);
    if (desc.rayGeneration)
    {
        Require(SupportsRayTracingPipeline(), "Ray Tracing Pipeline is unavailable");
        CheckOwner(desc.rayGeneration);
        CheckOwner(desc.miss);
        CheckOwner(desc.closestHit);
        Require(!desc.compute && !desc.vertex && !desc.pixel &&
                    desc.rayGeneration->stage == ShaderStage::RayGeneration &&
                    desc.miss->stage == ShaderStage::Miss &&
                    desc.closestHit->stage == ShaderStage::ClosestHit && desc.rayPayloadSize > 0 &&
                    desc.rayPayloadSize <= 128 && desc.rayAttributeSize == 8 &&
                    desc.rayRecursionDepth > 0 && desc.rayRecursionDepth <= 2,
                "Invalid triangle ray tracing pipeline");
        return;
    }
    Require(!desc.miss && !desc.closestHit, "Ray shaders require a ray generation program");
    if (desc.compute)
    {
        CheckOwner(desc.compute);
        Require(!desc.vertex && !desc.pixel && desc.compute->stage == ShaderStage::Compute,
                "Invalid compute pipeline shaders");
    }
    else
    {
        Require(!desc.colorFormats.empty() && desc.colorFormats.size() <= 8,
                "Graphics pipeline requires one to eight color attachments");
        Require(std::ranges::none_of(desc.colorFormats, IsDepthFormat),
                "Color pipeline format cannot be depth/stencil");
        Require(!desc.depthStencilFormat || IsDepthFormat(*desc.depthStencilFormat),
                "Invalid depth/stencil attachment format");
        Require((!desc.depth.testEnabled && !desc.depth.writeEnabled && !desc.stencil.enabled) ||
                    desc.depthStencilFormat.has_value(),
                "Depth/stencil state requires an attachment");
        Require(!desc.stencil.enabled || HasStencil(*desc.depthStencilFormat),
                "Stencil testing requires a stencil attachment format");
        Require(desc.raster.colorWriteMask <= 15, "Invalid color write mask");
        const auto valid = [](auto value, auto last)
        { return static_cast<uint32_t>(value) <= static_cast<uint32_t>(last); };
        Require(valid(desc.raster.cull, CullMode::Back) &&
                    valid(desc.raster.frontFace, FrontFace::Clockwise) &&
                    valid(desc.depth.compare, CompareOp::Always),
                "Invalid raster/depth state");
        const auto alphaFactor = [](BlendFactor factor)
        {
            return factor == BlendFactor::Zero || factor == BlendFactor::One ||
                   factor == BlendFactor::SrcAlpha || factor == BlendFactor::OneMinusSrcAlpha ||
                   factor == BlendFactor::DstAlpha || factor == BlendFactor::OneMinusDstAlpha;
        };
        Require(valid(desc.blend.srcColor, BlendFactor::SrcAlphaSaturate) &&
                    valid(desc.blend.dstColor, BlendFactor::SrcAlphaSaturate) &&
                    alphaFactor(desc.blend.srcAlpha) && alphaFactor(desc.blend.dstAlpha) &&
                    valid(desc.blend.colorOp, BlendOp::Max) &&
                    valid(desc.blend.alphaOp, BlendOp::Max),
                "Invalid blend state or unsupported alpha factor");
        for (const auto& face : {desc.stencil.front, desc.stencil.back})
            Require(valid(face.compare, CompareOp::Always) &&
                        valid(face.fail, StencilOp::DecrementWrap) &&
                        valid(face.depthFail, StencilOp::DecrementWrap) &&
                        valid(face.pass, StencilOp::DecrementWrap),
                    "Invalid stencil state");
        CheckOwner(desc.vertex);
        CheckOwner(desc.pixel);
        Require(desc.vertex->stage == ShaderStage::Vertex &&
                    desc.pixel->stage == ShaderStage::Pixel,
                "Invalid graphics pipeline shaders");
        Require(desc.vertexStride > 0 && desc.vertexStride <= 2048 && !desc.attributes.empty(),
                "Graphics pipeline requires a vertex layout");
        for (const auto& attribute : desc.attributes)
        {
            const auto size = attribute.format == VertexFormat::Float2   ? 8u
                              : attribute.format == VertexFormat::Float3 ? 12u
                                                                         : 16u;
            Require(!attribute.semantic.empty() && attribute.offset <= desc.vertexStride &&
                        size <= desc.vertexStride - attribute.offset && attribute.location < 16,
                    "Invalid vertex attribute");
            Require(std::ranges::count_if(desc.attributes,
                                          [&](const auto& other)
                                          {
                                              return other.location == attribute.location ||
                                                     other.semantic == attribute.semantic;
                                          }) == 1,
                    "Duplicate vertex location or semantic");
        }
    }
}
void Runtime::Validate(const std::shared_ptr<BindingLayout>& layout,
                       const std::vector<Binding>& bindings) const
{
    CheckOwner(layout);
    Require(bindings.size() == layout->entries.size(), "Binding count mismatch");
    for (const auto& entry : layout->entries)
    {
        Require(entry.space < 4, "Binding space must be in [0,3]");
        Require(std::ranges::count_if(bindings, [&](const Binding& b)
                                      { return b.slot == entry.slot && b.space == entry.space; }) ==
                    1,
                "Missing or duplicate binding slot");
        const auto& b =
            *std::ranges::find_if(bindings, [&](const Binding& b)
                                  { return b.slot == entry.slot && b.space == entry.space; });
        if (entry.kind == BindingKind::ConstantBuffer ||
            entry.kind == BindingKind::ReadOnlyBuffer || entry.kind == BindingKind::StorageBuffer)
        {
            CheckOwner(b.buffer);
            Require(!b.view && !b.sampler && b.size && b.offset <= b.buffer->desc.size &&
                        b.size <= b.buffer->desc.size - b.offset,
                    "Invalid buffer binding range");
            Require(b.buffer->desc.memory != Memory::Readback,
                    "Cannot bind a readback buffer to a shader");
            if (entry.kind == BindingKind::ConstantBuffer)
                Require(b.offset % 256 == 0 && b.size % 256 == 0 && b.size <= 65536,
                        "Constant buffer range must be 256-byte aligned and <=64KiB");
            else
                Require(b.stride && b.stride % 4 == 0 && b.offset % b.stride == 0 &&
                            b.offset % 256 == 0 && b.size % b.stride == 0,
                        "Structured buffer requires aligned offset and whole elements");
            Require(entry.kind != BindingKind::StorageBuffer || b.buffer->desc.storage,
                    "Storage binding requires storage buffer usage");
            continue;
        }
        if (entry.kind == BindingKind::AccelerationStructure)
        {
            CheckOwner(b.accelerationStructure);
            Require(b.accelerationStructure->desc.topLevel,
                    "Shader acceleration structure bindings require a TLAS");
            Require(!b.view && !b.sampler && !b.buffer,
                    "Acceleration structure slot contains another resource type");
            continue;
        }
        Require(!b.buffer && !b.accelerationStructure,
                "Texture/sampler slot contains a non-texture resource");
        if (entry.kind == BindingKind::Sampler)
        {
            CheckOwner(b.sampler);
            Require(!b.view, "Sampler slot contains texture");
        }
        else
        {
            CheckOwner(b.view);
            Require(!b.sampler, "Texture slot contains sampler");
            Require(entry.kind != BindingKind::StorageTexture || b.view->texture->desc.storage,
                    "Storage binding requires storage texture");
            Require(!IsDepthFormat(b.view->texture->desc.format) ||
                        b.view->usage == TextureViewUsage::Sampled,
                    "Sampled depth textures require a sampled view");
        }
    }
}
std::shared_ptr<ShaderModule> Runtime::CreateShader(ShaderStage stage, std::vector<std::byte> code,
                                                    std::string entry)
{
    Require(!code.empty() && code.size() % 4 == 0, "Invalid shader binary");
    Require(!entry.empty(), "Shader entry cannot be empty");
    return std::make_shared<ShaderModule>(this, stage, std::move(code), std::move(entry));
}
AccelerationStructureSizes
Runtime::QueryAccelerationStructureSizes(const AccelerationStructureDesc& desc)
{
    if (!SupportsAccelerationStructures())
        throw std::runtime_error("Device does not support acceleration structures");
    Validate(desc);
    const auto sizes = QueryAccelerationStructureSizesNative(desc);
    Require(sizes.storage && sizes.storage <= ledger_->stats.budget &&
                sizes.scratch <= ledger_->stats.budget,
            "Invalid or over-budget acceleration structure sizes");
    return sizes;
}
AccelerationStructureSizes
Runtime::QueryAccelerationStructureSizesNative(const AccelerationStructureDesc&)
{
    throw std::runtime_error("Acceleration structures are not implemented by this runtime");
}
std::shared_ptr<AccelerationStructure>
Runtime::CreateAccelerationStructureNative(AccelerationStructureDesc, AccelerationStructureSizes)
{
    throw std::runtime_error("Acceleration structures are not implemented by this runtime");
}
std::shared_ptr<AccelerationStructure>
Runtime::CreateAccelerationStructure(const AccelerationStructureDesc& desc)
{
    Validate(desc);
    return CreateAccelerationStructureNative(desc, QueryAccelerationStructureSizes(desc));
}
std::unique_ptr<CommandList> Runtime::CreateCommands()
{
    return std::make_unique<CommandList>(this);
}
uint64_t Runtime::Submit(std::unique_ptr<CommandList> commands, Swapchain* presentation)
{
    Collect();
    Require(pending_.size() < 8,
            "At most eight submissions may remain in flight; wait or collect first");
    Require(commands && commands->owner_ == this, "Command list belongs to another runtime");
    commands->Recording();
    Require(!commands->rendering_ && commands->markers_ == 0, "Unclosed rendering or marker");
    Require(!presentation || presentation->owner == this, "Swapchain belongs to another runtime");
    // Reserve ownership before the native submission; allocation failure must not drop GPU uses.
    pending_.push_back({lastSubmitted_ + 1, std::move(commands), {}});
    auto& pending = pending_.back();
    try
    {
        auto cpuTimer = ProfileCpu(CpuOperation::NativeSubmit);
        pending.native = Execute(*pending.commands, pending.point, presentation);
    }
    catch (...)
    {
        pending_.pop_back();
        throw;
    }
    pending.commands->consumed_ = true;
    ++lastSubmitted_;
    Collect();
    return lastSubmitted_;
}
void Runtime::Collect()
{
    auto cpuTimer = ProfileCpu(CpuOperation::Retire);
    const auto completed = Completed();
    while (!pending_.empty() && pending_.front().point <= completed)
        pending_.pop_front();
}
void Runtime::Wait(uint64_t point)
{
    Require(point <= lastSubmitted_, "Cannot wait for an unsubmitted completion point");
    if (point && Completed() < point)
    {
        auto cpuTimer = ProfileCpu(CpuOperation::FenceWait);
        WaitNative(point);
    }
    Collect();
}
void Runtime::WaitIdle()
{
    Wait(lastSubmitted_);
}
} // namespace renderlab::rhi
