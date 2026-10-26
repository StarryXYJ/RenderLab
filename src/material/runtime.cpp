#include "material/runtime.hpp"
#include "material/render_state.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace renderlab::material
{
struct GpuGeneration
{
    std::shared_ptr<const shader::Package> package;
    std::map<std::string, std::shared_ptr<rhi::BindingLayout>> layouts;
    std::map<const shader::Program*, std::shared_ptr<rhi::ShaderModule>> modules;
    // RHI lookups are weak. Keep active state combinations resident even when all submitted
    // frames have retired before the next Prepare; otherwise owner-thread driver work repeats.
    std::vector<std::shared_ptr<rhi::Pipeline>> pipelines;
};
Runtime::Runtime(rhi::Runtime& runtime, shader::Target target) : runtime_(runtime), target_(target)
{
}
Prepared Runtime::Prepare(std::shared_ptr<const shader::Package> package, const Asset& asset,
                          const Preparation& preparation, const ResourceResolver& resolve)
{
    if (std::this_thread::get_id() != owner_)
        throw std::logic_error("Material preparation requires owner thread");
    if (!package)
        throw std::invalid_argument("Material preparation needs a shader package");
    const auto validated = Migrate(asset, shader::MaterialTemplate(package));
    const auto& pass = package->GetPass(package->ResolvePass(preparation.pass));
    if (!PassEnabled(package->definition, pass, validated))
        throw std::invalid_argument("Cannot prepare disabled shader pass " + pass.name);
    const auto& interface = package->Layout(pass.name);
    for (const auto& name : preparation.deferredResources)
    {
        const auto found =
            std::ranges::find_if(interface.resources,
                                 [&](const auto& resource)
                                 {
                                     return resource.scope != shader::Scope::Material &&
                                            resource.property.symbol == name;
                                 });
        if (found == interface.resources.end() || preparation.bindings.contains(name))
            throw std::invalid_argument("Invalid or already bound deferred resource: " + name);
    }
    std::erase_if(generations_, [](const auto& generation) { return generation.expired(); });
    std::shared_ptr<GpuGeneration> generation;
    for (const auto& weak : generations_)
        if (auto item = weak.lock(); item && item->package == package)
        {
            generation = std::move(item);
            break;
        }
    if (!generation)
    {
        generation = std::make_shared<GpuGeneration>();
        generation->package = package;
        for (const auto& declaredPass : package->definition.passes)
            generation->layouts.emplace(
                declaredPass.name,
                runtime_.CreateLayout(package->Layout(declaredPass.name).Bindings()));
        for (const auto& program : package->programs)
            if (program.target == target_)
                generation->modules.emplace(
                    &program, runtime_.CreateShader(program.stage, program.code, program.entry));
        if (generations_.size() == 64)
            generations_.erase(generations_.begin());
        generations_.push_back(generation);
    }
    const auto state = ResolveState(package->definition, pass, validated);
    rhi::PipelineDesc desc;
    desc.layout = generation->layouts.at(pass.name);
    desc.color = preparation.color;
    desc.raster = state.raster;
    desc.depth = state.depth;
    desc.blend = state.blend;
    desc.stencil = state.stencil;
    desc.depthStencilFormat = preparation.depthStencilFormat;
    if (!desc.depthStencilFormat &&
        (state.depth.testEnabled || state.depth.writeEnabled || state.stencil.enabled))
        desc.depthStencilFormat = state.stencil.enabled ? rhi::Format::D32S8 : rhi::Format::D32;
    if (preparation.mirrored)
        desc.raster.frontFace = desc.raster.frontFace == rhi::FrontFace::CounterClockwise
                                    ? rhi::FrontFace::Clockwise
                                    : rhi::FrontFace::CounterClockwise;
    desc.vertexStride = preparation.vertexStride;
    desc.attributes = preparation.attributes;
    std::map<std::string, uint32_t> featureValues;
    for (const auto& id : pass.features)
        featureValues.emplace(id, static_cast<uint32_t>(validated.values.at(id).numbers.at(0)));
    const auto key = shader::SelectVariantKey(package->definition, pass, featureValues);
    for (const auto& entry : pass.entries)
    {
        auto module = generation->modules.at(
            shader::HasKeywords(package->definition)
                ? &package->Find(pass.name, key, entry.stage, target_)
                : &package->Find(pass.name, validated.variant, entry.stage, target_));
        if (entry.stage == rhi::ShaderStage::Vertex)
            desc.vertex = module;
        else if (entry.stage == rhi::ShaderStage::Pixel)
            desc.pixel = module;
        else
            desc.compute = module;
    }
    Prepared result;
    result.stencilReference = state.stencilReference;
    result.generation = generation;
    result.pipeline = runtime_.CreatePipeline(std::move(desc));
    if (std::ranges::find(generation->pipelines, result.pipeline) == generation->pipelines.end())
    {
        if (generation->pipelines.size() == 64)
            generation->pipelines.erase(generation->pipelines.begin());
        generation->pipelines.push_back(result.pipeline);
    }
    std::vector<rhi::Binding> bindings;
    for (const auto& block : interface.blocks)
    {
        rhi::Binding binding{};
        if (block.scope != shader::Scope::Material && preparation.bindings.contains(block.name))
            binding = preparation.bindings.at(block.name);
        else
        {
            static const std::map<std::string, Value> empty;
            const auto& values = block.scope == shader::Scope::Material ? validated.values
                                 : preparation.constants.contains(block.name)
                                     ? preparation.constants.at(block.name)
                                     : empty;
            const auto bytes = Pack(block, values);
            binding.buffer = runtime_.CreateBuffer(
                {bytes.size(), rhi::Memory::Upload, "Constants." + block.name});
            std::memcpy(binding.buffer->Map().data(), bytes.data(), bytes.size());
            binding.size = bytes.size();
            result.constants.push_back(binding.buffer);
        }
        binding.slot = block.slot;
        binding.space = uint32_t(block.scope);
        bindings.push_back(std::move(binding));
    }
    for (const auto& resource : interface.resources)
    {
        const auto& property = resource.property;
        rhi::Binding binding{};
        if (resource.scope == shader::Scope::Material)
        {
            const auto& reference = validated.resources.at(property.id).reference;
            if (!resolve)
                throw std::invalid_argument("Missing material resource resolver: " + property.id);
            binding = resolve(property, reference);
        }
        else
        {
            if (preparation.deferredResources.contains(property.symbol))
                continue;
            const auto found = preparation.bindings.find(property.symbol);
            if (found == preparation.bindings.end())
                throw std::invalid_argument("Missing pass resource: " + property.symbol);
            binding = found->second;
        }
        if (property.srgb &&
            (!binding.view || (binding.view->texture->desc.format != rhi::Format::Rgba8Srgb &&
                               binding.view->texture->desc.format != rhi::Format::Bgra8Srgb)))
            throw std::invalid_argument("sRGB property requires an sRGB texture: " + property.id);
        if (property.type.resource == rhi::BindingKind::ReadOnlyBuffer ||
            property.type.resource == rhi::BindingKind::StorageBuffer)
        {
            const auto element = shader::ParseType(property.type.element, property.source);
            if (binding.stride != shader::ElementBytes(element))
                throw std::invalid_argument("Structured buffer stride mismatch: " +
                                            property.symbol);
        }
        binding.slot = resource.slot;
        binding.space = uint32_t(resource.scope);
        bindings.push_back(std::move(binding));
    }
    result.resources = std::move(bindings);
    return result;
}
} // namespace renderlab::material
