#pragma once

#include "assets/io.hpp"
#include <nlohmann/json.hpp>

namespace renderlab::assets::detail
{
inline nlohmann::json LoadJson(const std::filesystem::path& path)
{
    const auto bytes = Read(path);
    return nlohmann::json::parse(bytes.begin(), bytes.end());
}
inline void SaveJson(const std::filesystem::path& path, const nlohmann::json& json)
{
    const auto text = json.dump(2) + "\n";
    WriteFileAtomically(path, std::as_bytes(std::span{text.data(), text.size()}));
}
} // namespace renderlab::assets::detail
