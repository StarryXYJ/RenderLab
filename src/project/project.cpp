#include "project/project.hpp"
#include "assets/json_io.hpp"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <stdexcept>

namespace renderlab::project
{
std::string NewId()
{
    std::array<unsigned char, 16> bytes{};
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("Cannot generate UUID");
    bytes[6] = (bytes[6] & 15) | 64;
    bytes[8] = (bytes[8] & 63) | 128;
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (auto b : bytes)
    {
        result += digits[b >> 4];
        result += digits[b & 15];
    }
    return result;
}
bool ValidId(std::string_view id)
{
    return id.size() == 32 &&
           std::ranges::all_of(id, [](char c)
                               { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
std::filesystem::path Inside(const std::filesystem::path& root,
                             const std::filesystem::path& relative)
{
    if (relative.empty() || relative.is_absolute())
        throw std::invalid_argument("Expected mounted relative asset path");
    const auto base = std::filesystem::weakly_canonical(root);
    const auto result = std::filesystem::weakly_canonical(base / relative);
    auto a = base.begin(), b = result.begin();
    for (; a != base.end(); ++a, ++b)
        if (b == result.end() || *a != *b)
            throw std::invalid_argument("Path escapes mount root");
    return result;
}
Project Project::Open(const std::filesystem::path& path, const std::filesystem::path& engineRoot)
{
    const auto data = assets::detail::LoadJson(path);
    if (data.at("version") != 1)
        throw std::invalid_argument("Unsupported project version");
    Project result;
    result.manifest = std::filesystem::canonical(path);
    result.root = result.manifest.parent_path();
    result.id = data.at("id");
    result.startScene = data.at("startScene");
    if (!ValidId(result.id) || !ValidId(result.startScene))
        throw std::invalid_argument("Invalid project/scene UUID");
    result.assets = Inside(result.root, assets::detail::Path(data.value("assets", "Assets")));
    result.library = Inside(result.root, assets::detail::Path(data.value("library", "Library")));
    result.engine = std::filesystem::canonical(engineRoot);
    if (!std::filesystem::is_directory(result.assets) || result.assets == result.library ||
        result.library == result.root || result.assets == result.root)
        throw std::invalid_argument("Invalid project asset/library roots");
    const auto relation = result.library.lexically_relative(result.assets);
    if (!relation.empty() && *relation.begin() != "..")
        throw std::invalid_argument("Library cannot be inside Assets");
    const auto reverse = result.assets.lexically_relative(result.library);
    if (!reverse.empty() && *reverse.begin() != "..")
        throw std::invalid_argument("Assets cannot be inside Library");
    return result;
}
std::filesystem::path Project::ResolveEngine(std::string_view relative) const
{
    return Inside(engine, assets::detail::Path(relative));
}
std::filesystem::path Project::ResolveAssetPath(std::string_view relative) const
{
    return Inside(assets, assets::detail::Path(relative));
}
} // namespace renderlab::project
