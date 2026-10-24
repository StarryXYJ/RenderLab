#include "core/file_io.hpp"
#include "material/asset.hpp"
#include "platform/text.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace renderlab::material
{
void ValidateResourceReference(const std::string& value)
{
    if (value.empty() || std::filesystem::path(platform::Utf8ToWide(value)).is_absolute())
        throw std::invalid_argument("Resource references must be logical IDs or relative paths");
}
void Save(const Asset& asset, const std::filesystem::path& path)
{
    using Json = nlohmann::json;
    Json document{{"version", 1},
                  {"shader", asset.shader},
                  {"variant", asset.variant},
                  {"values", Json::object()},
                  {"resources", Json::object()}};
    for (const auto& [id, value] : asset.values)
        document["values"][id] = {{"type", value.type},
                                  {"count", value.count},
                                  {"array", value.array},
                                  {"value", value.numbers}};
    for (const auto& [id, resource] : asset.resources)
    {
        ValidateResourceReference(resource.reference);
        document["resources"][id] = {{"type", resource.type}, {"reference", resource.reference}};
    }
    const auto text = document.dump(2) + "\n";
    WriteFileAtomically(path, std::as_bytes(std::span{text.data(), text.size()}));
}
Asset Load(const std::filesystem::path& path, const shader::MaterialTemplate& materialTemplate)
{
    const auto document = nlohmann::json::parse(shader::ReadText(path));
    if (document.at("version") != 1)
        throw std::runtime_error("Unsupported material format version");
    Asset result;
    result.shader = document.at("shader");
    result.variant = document.at("variant");
    for (const auto& [id, value] : document.at("values").items())
        result.values.emplace(id, Value{value.at("type"), value.at("count"),
                                        value.at("value").get<std::vector<double>>(),
                                        value.at("array").get<bool>()});
    for (const auto& [id, value] : document.at("resources").items())
        result.resources.emplace(id, ResourceReference{value.at("type"), value.at("reference")});
    return Migrate(result, materialTemplate);
}
} // namespace renderlab::material
