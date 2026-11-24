#include "renderer/frame.hpp"
#include "renderer/project_module.h"
#include "renderer/project_settings.hpp"
#include "rendergraph/graph.hpp"

#include <cstdio>
#include <exception>
#include <stdexcept>

namespace
{
int32_t Blue(void*, void* opaqueGraph, const void* opaqueFrame, void*, char* error,
             uint32_t capacity)
{
    try
    {
        auto& graph = *static_cast<renderlab::rendergraph::Graph*>(opaqueGraph);
        const auto& frame = *static_cast<const renderlab::renderer::RenderFrame*>(opaqueFrame);
        double blue = .75;
        if (frame.projectSettings)
        {
            if (frame.projectSettings->version != 1)
                throw std::invalid_argument("Unsupported example settings version");
            const auto found = frame.projectSettings->values.find("example.blue");
            if (found != frame.projectSettings->values.end())
            {
                if (const auto* value = std::get_if<double>(&found->second))
                    blue = *value;
                else if (const auto* integerValue = std::get_if<int32_t>(&found->second))
                    blue = *integerValue;
                else
                    throw std::invalid_argument("example.blue must be numeric");
                if (blue < 0 || blue > 4)
                    throw std::invalid_argument("example.blue must be in [0,4]");
            }
        }
        auto pass = graph.AddRasterPass("Example.BlueClear");
        pass.ColorTarget("CameraColor", renderlab::rhi::LoadOp::Clear,
                         {0, .25f, static_cast<float>(blue), 1});
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
