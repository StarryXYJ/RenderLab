#pragma once

#include "core/file_io.hpp"

#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace renderlab::assets::detail
{
inline std::filesystem::path Path(std::string_view text)
{
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}
inline std::string Text(const std::filesystem::path& path)
{
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
inline std::vector<std::byte> Read(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("Cannot open " + Text(path));
    const auto length = file.tellg();
    if (length < 0 || length > 128 * 1024 * 1024)
        throw std::runtime_error("Asset exceeds 128MiB: " + Text(path));
    std::vector<std::byte> data(static_cast<size_t>(length));
    file.seekg(0);
    if (!data.empty() && !file.read(reinterpret_cast<char*>(data.data()), length))
        throw std::runtime_error("Cannot read " + Text(path));
    return data;
}
} // namespace renderlab::assets::detail
