#include "renderer/project_settings.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace renderlab::renderer
{
std::shared_ptr<const ProjectSettings> ParseProjectSettings(std::string_view text)
{
    if (text.size() > 65536)
        throw std::invalid_argument("Project settings exceed budget");
    std::vector<std::set<std::string>> names;
    const auto callback = [&](int, nlohmann::json::parse_event_t event, nlohmann::json& value)
    {
        if (event == nlohmann::json::parse_event_t::object_start)
            names.emplace_back();
        else if (event == nlohmann::json::parse_event_t::key &&
                 !names.back().insert(value.get<std::string>()).second)
            throw std::invalid_argument("Duplicate project settings field");
        else if (event == nlohmann::json::parse_event_t::object_end)
            names.pop_back();
        return true;
    };
    const auto root = nlohmann::json::parse(text.begin(), text.end(), callback);
    if (!root.is_object() || root.size() != 2 || !root.contains("version") ||
        !root.at("version").is_number_integer() || !root.contains("values") ||
        !root.at("values").is_object() || root.at("values").size() > 256)
        throw std::invalid_argument("Invalid project settings document");
    if (root.at("version").is_number_unsigned() &&
        root.at("version").get<uint64_t>() >
            static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))
        throw std::invalid_argument("Invalid project settings version");
    const auto version = root.at("version").get<int64_t>();
    if (version < 1 || version > std::numeric_limits<int32_t>::max())
        throw std::invalid_argument("Invalid project settings version");
    auto result = std::make_shared<ProjectSettings>();
    result->version = static_cast<uint32_t>(version);
    for (const auto& [id, value] : root.at("values").items())
    {
        if (id.empty() || id.size() > 128 ||
            !std::ranges::all_of(id,
                                 [](char c)
                                 {
                                     return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                                            c == '.' || c == '-' || c == '_';
                                 }))
            throw std::invalid_argument("Invalid project setting ID");
        ProjectSettingValue parsed;
        if (value.is_boolean())
            parsed = value.get<bool>();
        else if (value.is_number_integer())
        {
            if (value.is_number_unsigned() &&
                value.get<uint64_t>() > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))
                parsed = value.get<double>();
            else
            {
                const auto number = value.get<int64_t>();
                if (number < std::numeric_limits<int32_t>::min() ||
                    number > std::numeric_limits<int32_t>::max())
                    parsed = value.get<double>();
                else
                    parsed = static_cast<int32_t>(number);
            }
        }
        else if (value.is_number_float())
        {
            const auto number = value.get<double>();
            if (!std::isfinite(number))
                throw std::invalid_argument("Project setting must be finite");
            parsed = number;
        }
        else if (value.is_string())
            parsed = value.get<std::string>();
        else
            throw std::invalid_argument("Unsupported project setting value");
        result->values.emplace(id, std::move(parsed));
    }
    return result;
}
} // namespace renderlab::renderer
