#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace renderlab::project
{
/// @brief Generates a UUID v4 encoded as 32 lowercase hexadecimal characters.
std::string NewId();
bool ValidId(std::string_view id);
/// Stable rendering IDs use lowercase ASCII and are independent of transient UI indices.
bool ValidRenderingId(std::string_view id);
struct Rendering
{
    std::vector<std::filesystem::path> modules;
    std::string defaultPipeline;
    std::optional<std::filesystem::path> pipelines;
};
/** @brief Manifest paths stay inside the project; built-in engine resources are mounted by host. */
struct Project
{
    std::filesystem::path manifest, root, assets, library, engine;
    std::string id, startScene;
    std::optional<Rendering> rendering;
    /// @brief Loads version 1 and validates mount containment; engineRoot is supplied by the host.
    static Project Open(const std::filesystem::path& manifest,
                        const std::filesystem::path& engineRoot);
    std::filesystem::path ResolveEngine(std::string_view relative) const;
    std::filesystem::path ResolveAssetPath(std::string_view relative) const;
};
/// @brief Resolves a relative path inside root, rejecting absolute paths and symlink escapes.
std::filesystem::path Inside(const std::filesystem::path& root,
                             const std::filesystem::path& relative);
} // namespace renderlab::project
