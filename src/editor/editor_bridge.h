#pragma once

#include <stdint.h>

#if defined(RENDERLAB_EDITOR_BRIDGE_EXPORTS)
#define RENDERLAB_EDITOR_API __declspec(dllexport)
#else
#define RENDERLAB_EDITOR_API __declspec(dllimport)
#endif
#define RENDERLAB_EDITOR_CALL __cdecl

#ifdef __cplusplus
extern "C"
{
#endif

    typedef uint64_t rl_viewport_handle;
    typedef int32_t rl_status;
    enum
    {
        RL_OK = 0,
        RL_INVALID_ARGUMENT = 1,
        RL_INVALID_HANDLE = 2,
        RL_WRONG_THREAD = 3,
        RL_NATIVE_ERROR = 4,
        RL_BUFFER_TOO_SMALL = 5
    };
    typedef struct rl_viewport_stats
    {
        uint64_t frames, resizes, paused_ticks, input_frames, native_bytes, peak_bytes;
        uint32_t width, height, dpi, reflection_samples, focused, captured, pressed_keys, pipeline;
    } rl_viewport_stats;
    typedef struct rl_viewport_close_report
    {
        uint64_t remaining_bytes, peak_bytes;
        uint32_t validation_errors, reserved;
    } rl_viewport_close_report;

    RENDERLAB_EDITOR_API uint32_t RENDERLAB_EDITOR_CALL rl_editor_abi_version(void);
    /* UTF-8 paths are borrowed for this call. backend: 0 DX12, 1 Vulkan. UI STA only.
     * The bridge owns the returned child HWND; the managed host must destroy the session first.
     */
    RENDERLAB_EDITOR_API rl_status RENDERLAB_EDITOR_CALL rl_viewport_create(
        void* parent_hwnd, uint32_t backend, const char* project_utf8, const char* shaders_utf8,
        const char* log_utf8, rl_viewport_handle* session, void** child_hwnd);
    /* All session calls run on its creating thread. No message loop is pumped by the bridge. */
    RENDERLAB_EDITOR_API rl_status RENDERLAB_EDITOR_CALL
    rl_viewport_tick(rl_viewport_handle session, float delta_seconds, uint32_t paused);
    /* pipeline: 1 Forward, 2 Deferred, 3 RayTracing. Failed selection retains the prior pipeline.
     */
    RENDERLAB_EDITOR_API rl_status RENDERLAB_EDITOR_CALL rl_viewport_configure(
        rl_viewport_handle session, uint32_t pipeline, float exposure, uint32_t reset_camera);
    RENDERLAB_EDITOR_API rl_status RENDERLAB_EDITOR_CALL
    rl_viewport_get_stats(rl_viewport_handle session, rl_viewport_stats* stats);
    /* Waits GPU/presentation, releases GPU ownership, then destroys the child window. */
    RENDERLAB_EDITOR_API rl_status RENDERLAB_EDITOR_CALL
    rl_viewport_destroy(rl_viewport_handle session, rl_viewport_close_report* report);
    /* Thread-local UTF-8 error. required includes NUL; null/zero queries size without truncation.
     */
    RENDERLAB_EDITOR_API rl_status RENDERLAB_EDITOR_CALL rl_editor_last_error(char* output,
                                                                              uint32_t capacity,
                                                                              uint32_t* required);

#ifdef __cplusplus
}
#endif
