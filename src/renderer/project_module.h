#pragma once

#include <stdint.h>

/* Same-build interface: graph, frame and runtime are borrowed C++ objects from this build. */
#define RENDERLAB_PROJECT_MODULE_ABI 1u
#define RENDERLAB_PROJECT_GRAPH_ABI 1u
#define RENDERLAB_PROJECT_FORWARD 1u
#if defined(_DLL)
#define RENDERLAB_PROJECT_DYNAMIC_CRT 1u
#else
#define RENDERLAB_PROJECT_DYNAMIC_CRT 0u
#endif
#if defined(_DEBUG)
#define RENDERLAB_PROJECT_DEBUG_CRT 1u
#else
#define RENDERLAB_PROJECT_DEBUG_CRT 0u
#endif

#if defined(__cplusplus)
#include <iterator>
extern "C"
{
#endif

    /* Returns zero on success; reports failures in the UTF-8 buffer, without throwing. */
    typedef int32_t (*rl_project_node_build_fn)(void* user, void* graph, const void* frame,
                                                void* runtime, char* error,
                                                uint32_t error_capacity);
    typedef struct rl_project_node_v1
    {
        uint32_t size;
        const char* id;
        rl_project_node_build_fn build;
        void* user;
    } rl_project_node_v1;

    typedef struct rl_project_pipeline_v1
    {
        uint32_t size;
        const char* id;
        uint32_t base;
        uint32_t node_count;
        const char* const* nodes;
    } rl_project_pipeline_v1;

    typedef struct rl_project_module_v1
    {
        uint32_t size;
        uint32_t abi;
        uint32_t graph_abi;
        uint32_t compiler;
        uint32_t iterator_debug;
        uint32_t dynamic_crt;
        uint32_t debug_crt;
        const char* id;
        uint32_t node_count;
        const rl_project_node_v1* nodes;
        uint32_t pipeline_count;
        const rl_project_pipeline_v1* pipelines;
    } rl_project_module_v1;

    /* The returned descriptors and user data stay valid until the DLL is unloaded. */
    typedef const rl_project_module_v1* (*rl_project_module_entry_v1)(void);

#if defined(__cplusplus)
}
#endif
