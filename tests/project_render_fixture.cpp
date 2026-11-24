#include "renderer/project_module.h"
#include "rendergraph/graph.hpp"

#include <cstdio>
#include <exception>
#include <memory>

namespace
{
int32_t Clear(void*, void* opaqueGraph, const void*, void*, char* error, uint32_t capacity)
{
    try
    {
        auto& graph = *static_cast<renderlab::rendergraph::Graph*>(opaqueGraph);
        auto pass = graph.AddRasterPass("Project.BlueClear");
        pass.ColorTarget("CameraColor", renderlab::rhi::LoadOp::Clear, {0, .25f, .75f, 1});
        pass.Execute([](renderlab::rendergraph::RasterContext&) {});
        return 0;
    }
    catch (const std::exception& failure)
    {
        if (error && capacity)
            std::snprintf(error, capacity, "%s", failure.what());
        return 1;
    }
}
int32_t Marker(void*, void* opaqueGraph, const void*, void*, char* error, uint32_t capacity)
{
    try
    {
        auto& graph = *static_cast<renderlab::rendergraph::Graph*>(opaqueGraph);
        auto pass = graph.AddCopyPass("Project.Marker");
        pass.SideEffect();
        pass.Execute([](renderlab::rendergraph::CopyContext&) {});
        return 0;
    }
    catch (const std::exception& failure)
    {
        if (error && capacity)
            std::snprintf(error, capacity, "%s", failure.what());
        return 1;
    }
}
int32_t Reject(void*, void* opaqueGraph, const void*, void*, char* error, uint32_t capacity)
{
    try
    {
        auto& graph = *static_cast<renderlab::rendergraph::Graph*>(opaqueGraph);
        auto pass = graph.AddCopyPass("Project.Incomplete");
        pass.Execute([value = std::make_shared<int>(42)](renderlab::rendergraph::CopyContext&)
                     { (void)value; });
        if (error && capacity)
            std::snprintf(error, capacity, "intentional node failure");
        return 1;
    }
    catch (const std::exception& failure)
    {
        if (error && capacity)
            std::snprintf(error, capacity, "%s", failure.what());
        return 1;
    }
}
const rl_project_node_v1 nodes[] = {{sizeof(rl_project_node_v1), "test.clear", Clear, nullptr},
                                    {sizeof(rl_project_node_v1), "test.marker", Marker, nullptr},
                                    {sizeof(rl_project_node_v1), "test.reject", Reject, nullptr}};
const char* const clearNodes[] = {"test.clear", "test.marker"};
const char* const rejectNodes[] = {"test.reject"};
const rl_project_pipeline_v1 pipelines[] = {
    {sizeof(rl_project_pipeline_v1), "test.blue", RENDERLAB_PROJECT_FORWARD, 2, clearNodes},
    {sizeof(rl_project_pipeline_v1), "test.failure", RENDERLAB_PROJECT_FORWARD, 1, rejectNodes}};
const rl_project_module_v1 module = {sizeof(rl_project_module_v1),
                                     RENDERLAB_PROJECT_MODULE_ABI,
                                     RENDERLAB_PROJECT_GRAPH_ABI,
                                     _MSC_FULL_VER,
                                     _ITERATOR_DEBUG_LEVEL,
                                     RENDERLAB_PROJECT_DYNAMIC_CRT,
                                     RENDERLAB_PROJECT_DEBUG_CRT,
                                     "test.renderer",
                                     3,
                                     nodes,
                                     2,
                                     pipelines};
} // namespace
extern "C" __declspec(dllexport) const rl_project_module_v1* renderlab_project_module_v1()
{
    return &module;
}
