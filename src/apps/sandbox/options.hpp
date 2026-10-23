#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace renderlab::sandbox
{
struct Options
{
    std::string backend = "dx12";
    std::string validation = "auto";
    std::optional<uint32_t> adapter;
    std::optional<uint32_t> exitAfterMs;
    std::string logPath = "logs/renderlab.log";
    std::string reportPath;
    bool headless = false;
    bool windowTest = false;
    bool help = false;
};

/**
 * @brief Parses arguments excluding argv[0]; the returned options own their string values.
 * @throws std::invalid_argument For unknown options, missing values or incompatible settings.
 */
Options ParseOptions(std::span<const std::string_view> arguments);
std::string_view Usage();
} // namespace renderlab::sandbox
