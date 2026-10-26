#include "shader/definition.hpp"

#include <algorithm>
#include <stdexcept>

namespace renderlab::shader
{
namespace
{
const Property& Feature(const Definition& definition, const std::string& id)
{
    for (const auto& block : definition.blocks)
        if (block.scope == Scope::Material)
            for (const auto& property : block.properties)
                if (property.id == id && !property.keyword.empty())
                    return property;
    throw std::invalid_argument("Unknown static feature ID " + id);
}
std::vector<uint32_t> Choices(const Property& property)
{
    if (!property.type.enumType)
        return {0, 1};
    std::vector<uint32_t> result;
    for (const auto& item : property.enumItems)
        result.push_back(item.value);
    return result;
}
} // namespace
std::string VariantLabel(const VariantKey& key)
{
    if (key.empty())
        return "Default";
    std::string result;
    for (const auto& [id, value] : key)
        result += std::to_string(id.size()) + ":" + id + "=" + std::to_string(value) + ";";
    return result;
}
std::vector<Variant> EnumerateVariants(const Definition& definition, const Pass& pass)
{
    std::vector<Variant> variants{{"Default", {}, {}}};
    for (const auto& id : pass.features)
    {
        const auto& property = Feature(definition, id);
        std::vector<Variant> next;
        for (const auto& variant : variants)
            for (uint32_t value : Choices(property))
            {
                auto selected = variant;
                selected.axes.emplace_back(id, value);
                selected.name = VariantLabel(selected.axes);
                selected.defines.emplace(property.keyword, std::to_string(value));
                next.push_back(std::move(selected));
                if (next.size() > 64)
                    throw std::invalid_argument("Static variant limit (64) exceeded");
            }
        variants = std::move(next);
    }
    return variants;
}
VariantKey SelectVariantKey(const Definition& definition, const Pass& pass,
                            const std::map<std::string, uint32_t>& values)
{
    VariantKey key;
    for (const auto& id : pass.features)
    {
        const auto& property = Feature(definition, id);
        const auto value = values.find(id);
        const auto choices = Choices(property);
        if (value == values.end() || std::ranges::find(choices, value->second) == choices.end())
            throw std::invalid_argument("Missing/invalid static feature value " + id);
        key.emplace_back(id, value->second);
    }
    return key;
}
} // namespace renderlab::shader
