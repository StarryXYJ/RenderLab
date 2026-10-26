#include "shader/package.hpp"

#include <nlohmann/json.hpp>

namespace renderlab::shader
{
using Json = nlohmann::json;
std::string SchemaToJson(const Package& package)
{
    Json output{{"shader", package.definition.name},
                {"key", package.key},
                {"generation", package.generation},
                {"cacheHit", package.cacheHit}};
    output["dependencies"] = Json::array();
    for (const auto& dependency : package.dependencies)
        output["dependencies"].push_back(ToUtf8(dependency));
    output["variants"] = Json::array();
    for (const auto& variant : package.definition.variants)
        output["variants"].push_back(Json{{"name", variant.name}, {"defines", variant.defines}});
    output["properties"] = Json::array();
    for (const auto& block : package.definition.blocks)
        if (block.scope == Scope::Material)
            for (const auto& property : block.properties)
            {
                Json choices = Json::array();
                for (const auto& item : property.enumItems)
                    choices.push_back({{"name", item.name}, {"value", item.value}});
                output["properties"].push_back({{"id", property.id},
                                                {"symbol", property.symbol},
                                                {"type", property.type.name},
                                                {"keyword", property.keyword},
                                                {"choices", choices},
                                                {"default", property.defaults},
                                                {"label", property.label},
                                                {"group", property.group}});
            }
    output["passes"] = Json::array();
    for (size_t i = 0; i < package.interfaces.size(); ++i)
    {
        const auto& interface = package.interfaces[i];
        Json pass{{"name", package.definition.passes[i].name}};
        const auto& definition = package.definition.passes[i];
        pass["role"] = definition.role == PassRole::Forward        ? "Forward"
                       : definition.role == PassRole::ShadowCaster ? "ShadowCaster"
                                                                   : "Custom";
        pass["statePreset"] = static_cast<int>(definition.state.preset);
        pass["state"] = Json::array();
        for (const auto& field : definition.state.overrides)
            pass["state"].push_back({{"field", StateFields()[field.field].path},
                                     {"property", field.value.property},
                                     {"literal", field.value.literal}});
        pass["features"] = definition.features;
        pass["variants"] = Json::array();
        for (const auto& variant : EnumerateVariants(package.definition, definition))
            pass["variants"].push_back(
                {{"label", variant.name}, {"axes", variant.axes}, {"defines", variant.defines}});
        pass["blocks"] = Json::array();
        pass["resources"] = Json::array();
        for (const auto& b : interface.blocks)
        {
            Json block{{"name", b.name},
                       {"space", uint32_t(b.scope)},
                       {"slot", b.slot},
                       {"size", b.size},
                       {"fields", Json::array()}};
            for (const auto& f : b.fields)
            {
                Json field{{"id", f.property.id},
                           {"symbol", f.property.symbol},
                           {"type", f.property.type.name},
                           {"count", f.property.type.count},
                           {"array", f.property.type.array},
                           {"default", f.property.defaults},
                           {"label", f.property.label},
                           {"group", f.property.group},
                           {"tooltip", f.property.tooltip},
                           {"unit", f.property.unit},
                           {"color", f.property.color},
                           {"hdr", f.property.hdr},
                           {"enum", f.property.enumeration},
                           {"offset", f.offset},
                           {"size", f.size},
                           {"arrayStride", f.arrayStride},
                           {"matrixStride", f.matrixStride}};
                if (f.property.minimum)
                    field["min"] = *f.property.minimum;
                if (f.property.maximum)
                    field["max"] = *f.property.maximum;
                block["fields"].push_back(std::move(field));
            }
            pass["blocks"].push_back(std::move(block));
        }
        for (const auto& r : interface.resources)
            pass["resources"].push_back({{"id", r.property.id},
                                         {"symbol", r.property.symbol},
                                         {"type", r.property.type.name},
                                         {"element", r.property.type.element},
                                         {"label", r.property.label},
                                         {"group", r.property.group},
                                         {"tooltip", r.property.tooltip},
                                         {"unit", r.property.unit},
                                         {"space", uint32_t(r.scope)},
                                         {"slot", r.slot},
                                         {"srgb", r.property.srgb},
                                         {"format", r.property.format},
                                         {"default", r.property.resourceDefault}});
        output["passes"].push_back(std::move(pass));
    }
    return output.dump(2) + "\n";
}
} // namespace renderlab::shader
