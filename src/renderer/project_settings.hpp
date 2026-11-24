#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

namespace renderlab::renderer
{
using ProjectSettingValue = std::variant<bool, int32_t, double, std::string>;
/** @brief Immutable, validated project values borrowed by node build callbacks. */
struct ProjectSettings
{
    uint32_t version = 0;
    std::map<std::string, ProjectSettingValue> values;
};
/// Parses a UTF-8 schema snapshot; rejects duplicates, unsupported values and over-budget data.
std::shared_ptr<const ProjectSettings> ParseProjectSettings(std::string_view json);
} // namespace renderlab::renderer
