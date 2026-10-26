#include "core/hash.hpp"
#include "shader/package.hpp"

#include <algorithm>
#include <format>
#include <set>
#include <sstream>
#include <stdexcept>

namespace renderlab::shader
{
namespace
{
uint32_t Align(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}
std::string HlslType(const Type& type)
{
    if (type.enumType)
        return "uint";
    if (type.scalar != Scalar::Bool)
        return type.name;
    return type.rows == 1 ? "uint" : "uint" + std::to_string(type.rows);
}
} // namespace
std::vector<rhi::BindingDesc> Interface::Bindings() const
{
    std::vector<rhi::BindingDesc> result;
    for (const auto& block : blocks)
        result.push_back({block.slot, rhi::BindingKind::ConstantBuffer, uint32_t(block.scope)});
    for (const auto& resource : resources)
        result.push_back(
            {resource.slot, *resource.property.type.resource, uint32_t(resource.scope)});
    std::ranges::sort(result, {}, [](const auto& b) { return uint64_t(b.space) * 1024 + b.slot; });
    return result;
}
const BlockLayout* Interface::Material() const
{
    const auto found =
        std::ranges::find_if(blocks, [](auto& b) { return b.scope == Scope::Material; });
    return found == blocks.end() ? nullptr : &*found;
}
Interface BuildInterface(const Definition& definition, const Pass& pass)
{
    Interface result;
    result.enums = definition.enums;
    uint32_t next[4]{};
    std::set<std::string> names, schemaIds;
    std::set<Property::BindingSemantic> bindingSemantics;
    const auto append = [&](const ParameterBlock& input)
    {
        const auto typeName = input.typeName.empty() ? input.name + "Data" : input.typeName;
        if (!names.insert(input.name).second || !names.insert(typeName).second)
            throw std::runtime_error(input.source.Describe() + ": Conflicting parameter block " +
                                     input.name);
        const auto space = uint32_t(input.scope);
        BlockLayout block{input.name, input.scope, next[space], 0, {}};
        block.schemaId = input.schemaId;
        block.schemaVersion = input.schemaVersion;
        block.typeName = typeName;
        if (!block.schemaId.empty() && !schemaIds.insert(block.schemaId).second)
            throw std::invalid_argument(input.source.Describe() +
                                        ": Duplicate parameter schema ID");
        for (const auto& property : input.properties)
        {
            if (!property.type.Numeric() || !property.keyword.empty())
                continue;
            const auto& type = property.type;
            FieldLayout field{property};
            field.matrixStride = type.columns > 1 ? 16u : 0u;
            const auto bytes =
                type.columns > 1 ? (type.columns - 1u) * 16u + type.rows * 4u : type.rows * 4u;
            field.arrayStride = type.array ? Align(bytes, 16) : 0;
            field.size = type.array ? field.arrayStride * (type.count - 1u) + bytes : bytes;
            if (type.array || type.columns > 1 || block.size % 16 + bytes > 16)
                block.size = Align(block.size, 16);
            field.offset = block.size;
            block.size += field.size;
            block.fields.push_back(std::move(field));
        }
        if (!block.fields.empty())
        {
            block.size = Align(block.size, 16);
            if (block.size > 16384)
                throw std::runtime_error(input.source.Describe() +
                                         ": Parameter block exceeds portable M2 limit (16KiB)");
            std::string identity = std::to_string(space) + ":" + std::to_string(block.size) + ";";
            for (const auto& field : block.fields)
            {
                const auto& type = field.property.type;
                identity += std::to_string(field.property.id.size()) + ":" + field.property.id +
                            ":" + type.name + ":" + std::to_string(type.rows) + ":" +
                            std::to_string(type.columns) + ":" + std::to_string(type.count) + ":" +
                            std::to_string(type.array) + ":" + std::to_string(field.offset) + ":" +
                            std::to_string(field.size) + ":" + std::to_string(field.arrayStride) +
                            ":" + std::to_string(field.matrixStride) + ";";
                std::vector<uint32_t> choices;
                for (const auto& item : field.property.enumItems)
                    choices.push_back(item.value);
                std::ranges::sort(choices);
                for (auto value : choices)
                    identity += std::to_string(value) + ",";
                identity += ";";
            }
            block.layoutHash = Sha256(identity);
            ++next[space];
            result.blocks.push_back(std::move(block));
        }
        for (const auto& property : input.properties)
        {
            if (property.type.Numeric())
                continue;
            if (property.binding != Property::BindingSemantic::None &&
                !bindingSemantics.insert(property.binding).second)
                throw std::invalid_argument(property.source.Describe() +
                                            ": Duplicate resource binding semantic");
            if (!names.insert(property.symbol).second)
                throw std::runtime_error(property.source.Describe() +
                                         ": Global resource symbol conflict " + property.symbol);
            result.resources.push_back({property, input.scope, next[space]++});
        }
    };
    // Common blocks are always assigned before pass-local blocks, so material interfaces cannot
    // move.
    for (const auto& block : definition.blocks)
        append(block);
    for (const auto& block : pass.blocks)
        append(block);
    if (result.Bindings().size() > 32)
        throw std::runtime_error(pass.source.Describe() + ": M2 binding limit exceeded (32)");
    return result;
}
std::string GenerateHeader(const Interface& interface, Target target)
{
    std::ostringstream out;
    out << "#ifndef RENDERLAB_GENERATED_PARAMETERS\n#define RENDERLAB_GENERATED_PARAMETERS\n";
    out << "#include <renderlab/generated/types.hlsli>\n";
    const auto binding = [&](uint32_t slot, Scope scope, char category)
    {
        if (target == Target::Spirv)
            out << "[[vk::binding(" << slot << "," << uint32_t(scope) << ")]] ";
        return std::string(" : register(") + category + std::to_string(slot) + ",space" +
               std::to_string(uint32_t(scope)) + ");\n";
    };
    for (const auto& block : interface.blocks)
    {
        const auto suffix = binding(block.slot, block.scope, 'b');
        out << "ConstantBuffer<" << block.typeName << "> " << block.name << suffix;
    }
    for (const auto& resource : interface.resources)
    {
        const auto kind = *resource.property.type.resource;
        if (target == Target::Spirv && kind == rhi::BindingKind::StorageTexture)
            out << "[[vk::image_format(\"rgba8\")]] ";
        const auto suffix = binding(resource.slot, resource.scope,
                                    kind == rhi::BindingKind::Sampler ? 's'
                                    : kind == rhi::BindingKind::StorageTexture ||
                                            kind == rhi::BindingKind::StorageBuffer
                                        ? 'u'
                                        : 't');
        out << resource.property.type.name;
        if (kind != rhi::BindingKind::Sampler)
            out << "<" << resource.property.type.element << ">";
        out << " " << resource.property.symbol << suffix;
    }
    out << "#endif\n";
    return out.str();
}
std::string GenerateTypesHeader(const Interface& interface)
{
    std::ostringstream out;
    out << "#ifndef RENDERLAB_GENERATED_TYPES\n#define RENDERLAB_GENERATED_TYPES\n";
    for (const auto& enumeration : interface.enums)
        for (const auto& item : enumeration.items)
            out << "#define " << enumeration.name << "_" << item.name << " " << item.value << "\n";
    for (const auto& block : interface.blocks)
    {
        out << "struct " << block.typeName << "\n{\n";
        for (const auto& field : block.fields)
        {
            out << "    " << (field.property.type.columns > 1 ? "column_major " : "")
                << HlslType(field.property.type) << " " << field.property.symbol;
            if (field.property.type.array)
                out << "[" << field.property.type.count << "]";
            out << ";\n";
        }
        out << "};\n";
    }
    out << "#endif\n";
    return out.str();
}
void ValidateReflection(const Interface& interface, const std::vector<ReflectedBinding>& bindings)
{
    const auto expected = interface.Bindings();
    for (const auto& binding : bindings)
    {
        auto found = std::ranges::find_if(
            expected, [&](auto& b) { return b.slot == binding.slot && b.space == binding.space; });
        if (found == expected.end() || binding.count != 1 || found->kind != binding.kind)
            throw std::runtime_error("Undeclared or incompatible reflected binding: " +
                                     binding.name);
        if (binding.kind != rhi::BindingKind::ConstantBuffer)
        {
            const auto resource = std::ranges::find_if(
                interface.resources, [&](const auto& r)
                { return r.slot == binding.slot && uint32_t(r.scope) == binding.space; });
            if (resource == interface.resources.end() || resource->property.symbol != binding.name)
                throw std::runtime_error("Resource identity mismatch: " + binding.name);
            continue;
        }
        auto block = std::ranges::find_if(
            interface.blocks,
            [&](auto& b) { return b.slot == binding.slot && uint32_t(b.scope) == binding.space; });
        if (block == interface.blocks.end() || block->name != binding.name)
            throw std::runtime_error("Constant buffer identity mismatch: " + binding.name);
        for (const auto& field : binding.fields)
        {
            const auto member = std::ranges::find_if(block->fields, [&](auto& f)
                                                     { return f.property.symbol == field.name; });
            if (member == block->fields.end() || member->offset != field.offset ||
                member->property.type.rows != field.rows ||
                member->property.type.columns != field.columns ||
                member->property.type.count != field.count ||
                (member->property.type.scalar == Scalar::Bool
                     ? Scalar::Uint
                     : member->property.type.scalar) != field.scalar ||
                (field.columns > 1 && !field.columnMajor) ||
                (field.arrayStride && field.arrayStride != member->arrayStride) ||
                (field.matrixStride && field.matrixStride != member->matrixStride))
                throw std::runtime_error(std::format(
                    "Reflected constant layout mismatch: {}.{} (offset={}, rows={}, columns={}, "
                    "count={}, arrayStride={}, matrixStride={}, scalar={})",
                    binding.name, field.name, field.offset, field.rows, field.columns, field.count,
                    field.arrayStride, field.matrixStride, int(field.scalar)));
        }
    }
}
const Interface& Package::Layout(const std::string& pass) const
{
    for (size_t i = 0; i < definition.passes.size(); ++i)
        if (definition.passes[i].name == pass)
            return interfaces.at(i);
    throw std::invalid_argument("Shader lacks pass " + pass);
}
PassHandle Package::ResolvePass(const PassSelection& selection) const
{
    std::optional<uint32_t> match;
    for (uint32_t i = 0; i < definition.passes.size(); ++i)
    {
        const auto& pass = definition.passes[i];
        const bool selected = std::holds_alternative<std::string>(selection)
                                  ? pass.name == std::get<std::string>(selection)
                                  : std::get<PassRole>(selection) != PassRole::Custom &&
                                        pass.role == std::get<PassRole>(selection);
        if (!selected)
            continue;
        if (match)
            throw std::invalid_argument("Ambiguous shader pass role");
        match = i;
    }
    if (!match)
        throw std::invalid_argument("Shader lacks pass or required role");
    return {this, *match};
}
const Pass& Package::GetPass(PassHandle handle) const
{
    if (handle.package != this || handle.index >= definition.passes.size())
        throw std::invalid_argument("Stale or foreign shader pass handle");
    return definition.passes[handle.index];
}
bool Package::HasRole(PassRole role) const
{
    return role != PassRole::Custom &&
           std::ranges::any_of(definition.passes,
                               [role](const auto& pass) { return pass.role == role; });
}
const Program& Package::Find(const std::string& pass, const std::string& variant,
                             rhi::ShaderStage stage, Target target) const
{
    for (const auto& program : programs)
        if (program.pass == pass && program.variant == variant && program.stage == stage &&
            program.target == target)
            return program;
    throw std::invalid_argument("Missing prepared shader program: " + pass + "/" + variant);
}
const Program& Package::Find(const std::string& pass, const VariantKey& selectedAxes,
                             rhi::ShaderStage stage, Target target) const
{
    for (const auto& program : programs)
        if (program.pass == pass && program.axes == selectedAxes && program.stage == stage &&
            program.target == target)
            return program;
    throw std::invalid_argument("Missing prepared static program: " + pass + "/" +
                                VariantLabel(selectedAxes));
}
MaterialTemplate::MaterialTemplate(std::shared_ptr<const Package> package)
    : package_(std::move(package))
{
    if (!package_)
        throw std::invalid_argument("MaterialTemplate requires a package");
}
const std::vector<FieldLayout>& MaterialTemplate::Fields() const
{
    static const std::vector<FieldLayout> empty;
    auto block = package_->interfaces.front().Material();
    return block ? block->fields : empty;
}
const std::vector<Property>& MaterialTemplate::Properties() const
{
    for (const auto& block : package_->definition.blocks)
        if (block.scope == Scope::Material)
            return block.properties;
    static const std::vector<Property> empty;
    return empty;
}
std::vector<ResourceLayout> MaterialTemplate::Resources() const
{
    std::vector<ResourceLayout> result;
    for (const auto& r : package_->interfaces.front().resources)
        if (r.scope == Scope::Material)
            result.push_back(r);
    return result;
}
} // namespace renderlab::shader
