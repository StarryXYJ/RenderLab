#include "assets/database.hpp"
#include "assets/json_io.hpp"

#include <set>
#include <stdexcept>

namespace renderlab::assets
{
namespace
{
std::filesystem::path Metadata(const std::filesystem::path& path)
{
    auto result = path;
    result += ".meta";
    return result;
}
} // namespace
Database::Database(const project::Project& project) : project_(project)
{
    Scan();
}
void Database::Save(const Entry& e) const
{
    detail::SaveJson(Metadata(e.path), {{"version", 1},
                                        {"id", e.id},
                                        {"dependencies", e.dependencies},
                                        {"subresources", e.subresources}});
}
void Database::Scan()
{
    std::map<std::string, Entry> next;
    for (const auto& file : std::filesystem::recursive_directory_iterator(project_.assets))
    {
        if (!file.is_regular_file() || file.path().extension() == ".meta")
            continue;
        const auto path =
            project::Inside(project_.assets, file.path().lexically_relative(project_.assets));
        Entry e;
        e.path = path;
        if (std::filesystem::exists(Metadata(path)))
        {
            const auto data = detail::LoadJson(Metadata(path));
            if (data.at("version") != 1)
                throw std::invalid_argument("Unsupported asset metadata");
            e.id = data.at("id");
            e.dependencies = data.value("dependencies", std::map<std::string, std::string>{});
            e.subresources = data.value("subresources", std::map<std::string, std::string>{});
        }
        else
        {
            e.id = project::NewId();
            Save(e);
        }
        if (!project::ValidId(e.id))
            throw std::invalid_argument("Invalid asset UUID: " + detail::Text(path));
        std::set<std::string> childIds;
        for (const auto& [key, value] : e.subresources)
            if (key.empty() || !project::ValidId(value) || !childIds.insert(value).second)
                throw std::invalid_argument("Invalid stable subresource ID");
        if (!next.emplace(e.id, std::move(e)).second)
            throw std::invalid_argument("Duplicate asset UUID");
    }
    entries_ = std::move(next);
}
const Entry& Database::Get(const std::string& id) const
{
    const auto found = entries_.find(id);
    if (found == entries_.end())
        throw std::invalid_argument("Missing asset UUID: " + id);
    return found->second;
}
const Entry& Database::AtPath(const std::filesystem::path& path) const
{
    const auto resolved = std::filesystem::weakly_canonical(path);
    for (const auto& [id, e] : entries_)
        if (e.path == resolved)
            return e;
    throw std::invalid_argument("Unindexed asset path: " + detail::Text(path));
}
std::string Database::Dependency(const std::string& owner, const std::string& uri)
{
    auto& entry = entries_.at(owner);
    if (auto found = entry.dependencies.find(uri); found != entry.dependencies.end())
    {
        Get(found->second);
        return found->second;
    }
    if (uri.find(':') != std::string::npos || uri.find('%') != std::string::npos)
        throw std::invalid_argument("External glTF URI must be a plain relative UTF-8 path");
    const auto relative =
        (entry.path.parent_path() / detail::Path(uri)).lexically_relative(project_.assets);
    const auto id = AtPath(project::Inside(project_.assets, relative)).id;
    entry.dependencies.emplace(uri, id);
    Save(entry);
    return id;
}
std::string Database::Subresource(const std::string& owner, const std::string& key)
{
    auto& entry = entries_.at(owner);
    auto found = entry.subresources.find(key);
    if (found != entry.subresources.end())
        return found->second;
    const auto id = project::NewId();
    entry.subresources.emplace(key, id);
    Save(entry);
    return id;
}
void Database::Rename(const std::string& id, const std::filesystem::path& relative)
{
    auto& entry = entries_.at(id);
    const auto target = project::Inside(project_.assets, relative);
    if (target.extension() == ".meta" || std::filesystem::exists(target) ||
        std::filesystem::exists(Metadata(target)))
        throw std::invalid_argument("Asset rename destination exists or is metadata");
    std::filesystem::create_directories(target.parent_path());
    std::filesystem::rename(entry.path, target);
    try
    {
        std::filesystem::rename(Metadata(entry.path), Metadata(target));
    }
    catch (...)
    {
        std::filesystem::rename(target, entry.path);
        throw;
    }
    entry.path = target;
}
} // namespace renderlab::assets
