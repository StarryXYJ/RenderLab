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
const shader::Property& SemanticProperty(const shader::MaterialTemplate& materialTemplate,
                                         shader::MaterialSemantic semantic)
{
    for (const auto& property : materialTemplate.Properties())
        if (semantic != shader::MaterialSemantic::None && property.semantic == semantic)
            return property;
    throw std::invalid_argument("Shader does not declare the requested material semantic");
}
} // namespace
Asset Create(const shader::MaterialTemplate& materialTemplate)
{
    Asset result;
    result.shader = materialTemplate.GetPackage().definition.assetId;
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
    if (asset.shader != materialTemplate.GetPackage().definition.assetId)
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
    if (asset.shader != materialTemplate.GetPackage().definition.assetId)
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
void Set(Asset& asset, const shader::MaterialTemplate& materialTemplate,
         shader::MaterialSemantic semantic, std::span<const double> values)
{
    Set(asset, materialTemplate, SemanticProperty(materialTemplate, semantic).id, values);
}
void SetResource(Asset& asset, const shader::MaterialTemplate& materialTemplate,
                 shader::MaterialSemantic semantic, std::string reference)
{
    SetResource(asset, materialTemplate, SemanticProperty(materialTemplate, semantic).id,
                std::move(reference));
}
} // namespace renderlab::material
