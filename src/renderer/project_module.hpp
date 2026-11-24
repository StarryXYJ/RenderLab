#pragma once

#include "renderer/project_module.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace renderlab::rendergraph
{
class Graph;
}
namespace renderlab::rhi
{
class Runtime;
}
namespace renderlab::renderer
{
struct RenderFrame;
struct ProjectNode
{
    std::string id;
    rl_project_node_build_fn build = nullptr;
    void* user = nullptr;
};
struct ProjectPipeline
{
    std::string id;
    uint32_t base = 0;
    std::vector<std::string> nodes;
};
/** @brief Validated module set. Callbacks remain valid while this owner is alive. */
class ProjectModules final
{
public:
    explicit ProjectModules(const std::vector<std::filesystem::path>& paths);
    ~ProjectModules();
    ProjectModules(const ProjectModules&) = delete;
    ProjectModules& operator=(const ProjectModules&) = delete;
    const ProjectNode& Node(const std::string& id) const;
    const ProjectPipeline& Pipeline(const std::string& id) const;
    const std::map<std::string, ProjectPipeline>& Pipelines() const;
    /// Builds into the caller's graph; the caller must keep this owner through GPU retirement.
    void Build(const std::string& id, rendergraph::Graph& graph, const RenderFrame& frame,
               rhi::Runtime& runtime) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace renderlab::renderer
