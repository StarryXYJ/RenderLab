#pragma once

#include "project/project.hpp"

#include <filesystem>
#include <map>
#include <string>

namespace renderlab::assets
{
/// @brief Source identity and persisted dependency/subresource UUID mappings from its .meta.
struct Entry
{
    std::string id;
    std::filesystem::path path;
    std::map<std::string, std::string> dependencies, subresources;
};
/** @brief Lightweight UUID/path index. Sidecars are authoritative, Library is disposable. */
class Database final
{
public:
    /// @brief Scans immediately; project must outlive this index.
    explicit Database(const project::Project& project);
    /// @brief Rebuilds the index, creates missing sidecars and rejects duplicate identities.
    void Scan();
    const project::Project& GetProject() const
    {
        return project_;
    }
    const Entry& Get(const std::string& id) const;
    const Entry& AtPath(const std::filesystem::path& path) const;
    /// @brief Resolves an existing URI mapping by UUID, or records its current relative target.
    std::string Dependency(const std::string& owner, const std::string& uri);
    /// @brief Reuses or persists a stable child UUID for an importer-defined key.
    std::string Subresource(const std::string& owner, const std::string& key);
    /// @brief Moves source and sidecar together within Assets, preserving their UUID mappings.
    void Rename(const std::string& id, const std::filesystem::path& relative);

private:
    void Save(const Entry& entry) const;
    const project::Project& project_;
    std::map<std::string, Entry> entries_;
};
} // namespace renderlab::assets
