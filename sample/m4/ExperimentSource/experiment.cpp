#include "renderer/project_module.h"
#include "rendergraph/graph.hpp"

#include <cstdio>
#include <exception>

namespace
{
int32_t Blue(void*, void* opaqueGraph, const void*, void*, char* error, uint32_t capacity)
{
    try
    {
        auto& graph = *static_cast<renderlab::rendergraph::Graph*>(opaqueGraph);
        auto pass = graph.AddRasterPass("Example.BlueClear");
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
const rl_project_node_v1 nodes[] = {{sizeof(rl_project_node_v1), "example.clear", Blue, nullptr}};
const char* const blueNodes[] = {"example.clear"};
const rl_project_pipeline_v1 pipelines[] = {
    {sizeof(rl_project_pipeline_v1), "example.blue", RENDERLAB_PROJECT_FORWARD, 1, blueNodes}};
const rl_project_module_v1 module = {sizeof(rl_project_module_v1),
                                     RENDERLAB_PROJECT_MODULE_ABI,
                                     RENDERLAB_PROJECT_GRAPH_ABI,
                                     _MSC_FULL_VER,
                                     _ITERATOR_DEBUG_LEVEL,
                                     RENDERLAB_PROJECT_DYNAMIC_CRT,
                                     RENDERLAB_PROJECT_DEBUG_CRT,
                                     "example.renderer",
                                     1,
                                     nodes,
                                     1,
                                     pipelines};
} // namespace

extern "C" __declspec(dllexport) const rl_project_module_v1* renderlab_project_module_v1()
{
    return &module;
}
