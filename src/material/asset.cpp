#include "material/asset.hpp"

#include "shader/value.hpp"

#include <algorithm>
#include <stdexcept>

namespace renderlab::material
{
namespace
{
std::string ResourceType(const shader::Property& property)
{
    return property.type.name + (property.type.resource == rhi::BindingKind::Sampler
                                     ? ""
                                     : "<" + property.type.element + ">");
}
} // namespace
void SelectVariant(Asset& asset, const shader::MaterialTemplate& materialTemplate,
                   const std::string& variant)
{
    if (asset.shader != materialTemplate.GetPackage().definition.name)
        throw std::invalid_argument("Material belongs to another shader");
    if (std::ranges::none_of(materialTemplate.GetPackage().definition.variants,
                             [&](auto& v) { return v.name == variant; }))
        throw std::invalid_argument("Undeclared shader variant: " + variant);
    asset.variant = variant;
}
Asset Create(const shader::MaterialTemplate& materialTemplate)
{
    Asset result;
    result.shader = materialTemplate.GetPackage().definition.name;
    result.variant = materialTemplate.GetPackage().definition.variants.front().name;
    for (const auto& property : materialTemplate.Properties())
        if (property.type.Numeric())
            result.values.emplace(property.id, Value{property.type.name, property.type.count,
                                                     property.defaults, property.type.array});
    for (const auto& resource : materialTemplate.Resources())
        result.resources.emplace(
            resource.property.id,
            ResourceReference{ResourceType(resource.property), resource.property.resourceDefault});
    return result;
}
void Set(Asset& asset, const shader::MaterialTemplate& materialTemplate, const std::string& id,
         std::span<const double> values)
{
    if (asset.shader != materialTemplate.GetPackage().definition.name)
        throw std::invalid_argument("Material belongs to another shader");
    for (const auto& property : materialTemplate.Properties())
        if (property.id == id && property.type.Numeric())
        {
            shader::ValidateValue(property, values);
            asset.values.insert_or_assign(id, Value{property.type.name,
                                                    property.type.count,
                                                    {values.begin(), values.end()},
                                                    property.type.array});
            return;
        }
    throw std::invalid_argument("Unknown material parameter ID " + id);
}
void SetResource(Asset& asset, const shader::MaterialTemplate& materialTemplate,
                 const std::string& id, std::string reference)
{
    if (asset.shader != materialTemplate.GetPackage().definition.name)
        throw std::invalid_argument("Material belongs to another shader");
    ValidateResourceReference(reference);
    for (const auto& resource : materialTemplate.Resources())
        if (resource.property.id == id)
        {
            asset.resources.insert_or_assign(
                id, ResourceReference{ResourceType(resource.property), std::move(reference)});
            return;
        }
    throw std::invalid_argument("Unknown material resource ID " + id);
}
Asset Migrate(const Asset& previous, const shader::MaterialTemplate& materialTemplate)
{
    auto result = Create(materialTemplate);
    if (result.shader != previous.shader)
        throw std::invalid_argument("Shader identity changed during reload");
    SelectVariant(result, materialTemplate, previous.variant);
    for (auto& [id, value] : result.values)
    {
        if (previous.resources.contains(id))
            throw std::invalid_argument("Reload numeric/resource type conflict: " + id);
        const auto old = previous.values.find(id);
        if (old == previous.values.end())
            continue;
        if (value.type != old->second.type || value.count != old->second.count ||
            value.array != old->second.array)
            throw std::invalid_argument("Reload type conflict for parameter " + id);
        Set(result, materialTemplate, id, old->second.numbers);
    }
    for (auto& [id, resource] : result.resources)
    {
        if (previous.values.contains(id))
            throw std::invalid_argument("Reload resource/numeric type conflict: " + id);
        const auto old = previous.resources.find(id);
        if (old == previous.resources.end())
            continue;
        if (resource.type != old->second.type)
            throw std::invalid_argument("Reload type conflict for resource " + id);
        SetResource(result, materialTemplate, id, old->second.reference);
    }
    return result;
}
} // namespace renderlab::material
