#include "renderer/project_module.hpp"

#include <Windows.h>

#include <algorithm>
#include <bit>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace renderlab::renderer
{
namespace
{
struct Library
{
    HMODULE handle = nullptr;
    explicit Library(const std::filesystem::path& path)
    {
        if (!path.is_absolute())
            throw std::invalid_argument("Project module path must be absolute");
        handle =
            LoadLibraryExW(path.c_str(), nullptr,
                           LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!handle)
        {
            const auto utf8 = path.generic_u8string();
            throw std::runtime_error("Cannot load project module: " +
                                     std::string(utf8.begin(), utf8.end()));
        }
    }
    ~Library()
    {
        if (handle)
            FreeLibrary(handle);
    }
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
};
std::string Id(const char* value)
{
    if (!value)
        throw std::invalid_argument("Missing project module ID");
    const std::string_view id(value);
    if (id.empty() || id.size() > 128 ||
        !std::ranges::all_of(id,
                             [](char c)
                             {
                                 return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                                        c == '.' || c == '-' || c == '_';
                             }))
        throw std::invalid_argument("Invalid project module ID");
    return std::string(id);
}
} // namespace

struct ProjectModules::Impl
{
    std::vector<std::unique_ptr<Library>> libraries;
    std::set<std::string> modules;
    std::map<std::string, ProjectNode> nodes;
    std::map<std::string, ProjectPipeline> pipelines;

    explicit Impl(const std::vector<std::filesystem::path>& paths)
    {
        if (paths.size() > 64)
            throw std::invalid_argument("Too many project modules");
        for (const auto& path : paths)
        {
            auto library = std::make_unique<Library>(path);
            const auto entry = std::bit_cast<rl_project_module_entry_v1>(
                GetProcAddress(library->handle, "renderlab_project_module_v1"));
            if (!entry)
                throw std::invalid_argument("Project module entry is missing");
            const auto* module = entry();
            if (!module || module->size != sizeof(rl_project_module_v1) ||
                module->abi != RENDERLAB_PROJECT_MODULE_ABI ||
                module->graph_abi != RENDERLAB_PROJECT_GRAPH_ABI ||
                module->compiler != _MSC_FULL_VER ||
                module->iterator_debug != _ITERATOR_DEBUG_LEVEL ||
                module->dynamic_crt != RENDERLAB_PROJECT_DYNAMIC_CRT ||
                module->debug_crt != RENDERLAB_PROJECT_DEBUG_CRT)
                throw std::invalid_argument("Project module ABI or build configuration mismatch");
            if (!modules.insert(Id(module->id)).second)
                throw std::invalid_argument("Duplicate project module ID");
            if (module->node_count > 256 || (module->node_count && !module->nodes) ||
                module->pipeline_count > 256 || (module->pipeline_count && !module->pipelines))
                throw std::invalid_argument("Invalid project module descriptor counts");
            for (uint32_t i = 0; i < module->node_count; ++i)
            {
                const auto& source = module->nodes[i];
                if (source.size != sizeof(source) || !source.build)
                    throw std::invalid_argument("Invalid project node descriptor");
                ProjectNode node{Id(source.id), source.build, source.user};
                const auto key = node.id;
                if (!nodes.emplace(key, std::move(node)).second)
                    throw std::invalid_argument("Duplicate project node ID: " + key);
            }
            for (uint32_t i = 0; i < module->pipeline_count; ++i)
            {
                const auto& source = module->pipelines[i];
                if (source.size != sizeof(source) || source.base != RENDERLAB_PROJECT_FORWARD ||
                    source.node_count > 256 || (source.node_count && !source.nodes))
                    throw std::invalid_argument("Invalid project pipeline descriptor");
                ProjectPipeline pipeline{Id(source.id), source.base, {}};
                std::set<std::string> used;
                for (uint32_t n = 0; n < source.node_count; ++n)
                {
                    auto id = Id(source.nodes[n]);
                    if (!used.insert(id).second)
                        throw std::invalid_argument("Duplicate project node in pipeline: " + id);
                    pipeline.nodes.push_back(std::move(id));
                }
                const auto key = pipeline.id;
                if (!pipelines.emplace(key, std::move(pipeline)).second)
                    throw std::invalid_argument("Duplicate project pipeline ID: " + key);
            }
            libraries.push_back(std::move(library));
        }
        for (const auto& [pipelineId, pipeline] : pipelines)
            for (const auto& node : pipeline.nodes)
                if (!nodes.contains(node))
                    throw std::invalid_argument("Missing node " + node + " in pipeline " +
                                                pipelineId);
    }
};

ProjectModules::ProjectModules(const std::vector<std::filesystem::path>& paths)
    : impl_(std::make_unique<Impl>(paths))
{
}
ProjectModules::~ProjectModules() = default;
const ProjectNode& ProjectModules::Node(const std::string& id) const
{
    const auto found = impl_->nodes.find(id);
    if (found == impl_->nodes.end())
        throw std::invalid_argument("Unknown project node: " + id);
    return found->second;
}
const ProjectPipeline& ProjectModules::Pipeline(const std::string& id) const
{
    const auto found = impl_->pipelines.find(id);
    if (found == impl_->pipelines.end())
        throw std::invalid_argument("Unknown project pipeline: " + id);
    return found->second;
}
const std::map<std::string, ProjectPipeline>& ProjectModules::Pipelines() const
{
    return impl_->pipelines;
}
} // namespace renderlab::renderer
