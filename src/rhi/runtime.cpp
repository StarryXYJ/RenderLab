#include "rhi/runtime.hpp"

#include "rhi/commands.hpp"
#include "rhi/detail/validation.hpp"

#include <algorithm>
#include <utility>

namespace renderlab::rhi
{
using detail::Require;

Runtime::Runtime() = default;
Runtime::~Runtime() = default;

void Runtime::CheckOwner(const std::shared_ptr<Object>& object) const
{
    Require(object && object->owner == this, "Null object or object from another runtime");
}
void Runtime::Validate(const BufferDesc& desc) const
{
    Require(desc.size > 0 && desc.size <= ledger_->stats.budget, "Invalid buffer size");
    Require(!desc.storage || desc.memory == Memory::Device,
            "Storage buffers require device memory");
}
void Runtime::Validate(const TextureDesc& desc) const
{
    Require(desc.width && desc.height && desc.width <= 16384 && desc.height <= 16384,
            "Texture dimensions must be in [1, 16384]");
    Require(desc.format != Format::D32 || (desc.attachment && !desc.storage),
            "Depth texture requires attachment usage and forbids storage usage");
    Require(!desc.storage || desc.format == Format::Rgba8,
            "M1 storage textures require RGBA8_UNORM");
}
void Runtime::Validate(const PipelineDesc& desc) const
{
    CheckOwner(desc.layout);
    if (desc.compute)
    {
        CheckOwner(desc.compute);
        Require(!desc.vertex && !desc.pixel && desc.compute->stage == ShaderStage::Compute,
                "Invalid compute pipeline shaders");
    }
    else
    {
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
        Require(!b.buffer, "Texture/sampler slot contains a buffer");
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
            Require(b.view->texture->desc.format != Format::D32, "M1 depth sampling unsupported");
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
    const auto completed = Completed();
    while (!pending_.empty() && pending_.front().point <= completed)
        pending_.pop_front();
}
void Runtime::Wait(uint64_t point)
{
    Require(point <= lastSubmitted_, "Cannot wait for an unsubmitted completion point");
    if (point && Completed() < point)
        WaitNative(point);
    Collect();
}
void Runtime::WaitIdle()
{
    Wait(lastSubmitted_);
}
} // namespace renderlab::rhi
