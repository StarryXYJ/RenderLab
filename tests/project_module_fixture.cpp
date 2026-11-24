#include "renderer/project_module.h"

#if PROJECT_TEST_VARIANT == 6
extern "C" __declspec(dllexport) int unrelated_entry()
{
    return 0;
}
#else
namespace
{
int32_t Build(void*, void* graph, const void*, void*, char*, uint32_t)
{
    if (graph)
        *static_cast<int*>(graph) = 42;
    return 0;
}
constexpr const char* moduleId = PROJECT_TEST_VARIANT >= 5 ? "test.other" : "test.demo";
constexpr const char* nodeId =
    PROJECT_TEST_VARIANT == 7 || PROJECT_TEST_VARIANT == 8 ? "test.other.node" : "test.demo.node";
constexpr const char* pipelineId =
    PROJECT_TEST_VARIANT == 8 ? "test.other.pipeline" : "test.demo.pipeline";
const rl_project_node_v1 nodes[] = {
    {sizeof(rl_project_node_v1), nodeId, PROJECT_TEST_VARIANT == 4 ? nullptr : Build, nullptr},
    {sizeof(rl_project_node_v1), nodeId, Build, nullptr}};
const char* const usedNodes[] = {PROJECT_TEST_VARIANT == 3 ? "test.missing" : "test.demo.node"};
const rl_project_pipeline_v1 pipelines[] = {
    {sizeof(rl_project_pipeline_v1), pipelineId, RENDERLAB_PROJECT_FORWARD, 1, usedNodes}};
const rl_project_module_v1 module = {
    sizeof(rl_project_module_v1),
    RENDERLAB_PROJECT_MODULE_ABI + (PROJECT_TEST_VARIANT == 1 ? 1u : 0u),
    RENDERLAB_PROJECT_GRAPH_ABI + (PROJECT_TEST_VARIANT == 10 ? 1u : 0u),
    _MSC_FULL_VER,
    _ITERATOR_DEBUG_LEVEL,
    RENDERLAB_PROJECT_DYNAMIC_CRT ^ (PROJECT_TEST_VARIANT == 9 ? 1u : 0u),
    RENDERLAB_PROJECT_DEBUG_CRT,
    moduleId,
    PROJECT_TEST_VARIANT == 2 ? 2u : 1u,
    nodes,
    1,
    pipelines};
} // namespace

extern "C" __declspec(dllexport) const rl_project_module_v1* renderlab_project_module_v1()
{
    return &module;
}
#endif
