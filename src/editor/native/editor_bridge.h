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
        uint64_t tick_ns, fence_wait_ns, acquire_ns, render_ns, present_ns, resize_ns;
        uint64_t diagnostics_ns, graph_compile_ns, graph_prepare_ns, graph_record_ns;
        uint64_t gpu_ns, gpu_frame;
        uint32_t validation_enabled, profiling_enabled;
        uint64_t scene_prepare_ns;
        uint64_t frame_interval_ns, frame_time_ns, dropped_samples;
        uint32_t activity, ready, ui_thread_id, render_thread_id;
        uint64_t config_revision, gpu_config_revision, session_generation;
    } rl_viewport_stats;
    typedef struct rl_viewport_close_report
    {
        uint64_t remaining_bytes, peak_bytes;
        uint32_t validation_errors, worker_failed;
    } rl_viewport_close_report;
    /* Version 1, 112 bytes. Flags: reflections/history/denoise/temporal/shadows = 1/2/4/8/16. */
    typedef struct rl_viewport_config
    {
        uint32_t size, version;
        uint64_t request_id;
        uint32_t pipeline, submission, recording_threads, async_compute, memory_alias, view;
        uint32_t profiling, target_fps, continuous, flags, shadow_resolution;
        float exposure, camera_speed;
        float light_x, light_y, light_z, light_r, light_g, light_b, light_intensity;
        float shadow_bias, shadow_normal_bias, environment_intensity, environment_rotation;
    } rl_viewport_config;
    /* status: 0 initializing, 1 pending, 2 applied, 3 rejected. IDs are session-local. */
    typedef struct rl_viewport_config_state
    {
        uint32_t size, version;
        uint64_t requested_id, applied_id, rejected_id, superseded_id;
        uint32_t status, ready;
        rl_viewport_config requested, applied;
    } rl_viewport_config_state;
    RENDERLAB_EDITOR_API rl_status RENDERLAB_EDITOR_CALL
    rl_viewport_configure_v2(rl_viewport_handle session, const rl_viewport_config* config);
    RENDERLAB_EDITOR_API rl_status RENDERLAB_EDITOR_CALL rl_viewport_get_config(
        rl_viewport_handle session, rl_viewport_config_state* state, rl_viewport_config* defaults);
    /* UTF-8 immutable capabilities / latest config rejection text. kind: 0 capabilities, 1 error.
     */
    RENDERLAB_EDITOR_API rl_status RENDERLAB_EDITOR_CALL
    rl_viewport_config_text(rl_viewport_handle session, uint32_t kind, char* output,
                            uint32_t capacity, uint32_t* required);
    RENDERLAB_EDITOR_API rl_status RENDERLAB_EDITOR_CALL
    rl_viewport_step(rl_viewport_handle session);

    RENDERLAB_EDITOR_API uint32_t RENDERLAB_EDITOR_CALL rl_editor_abi_version(void);
    /* Request a metadata snapshot. Acquire pins CPU values; Copy never consumes; Release unpins. */
    RENDERLAB_EDITOR_API rl_status RENDERLAB_EDITOR_CALL
    rl_viewport_request_snapshot(rl_viewport_handle session);
    RENDERLAB_EDITOR_API rl_status RENDERLAB_EDITOR_CALL rl_viewport_acquire_snapshot(
        rl_viewport_handle session, uint64_t* snapshot, uint32_t* required);
    RENDERLAB_EDITOR_API rl_status RENDERLAB_EDITOR_CALL rl_viewport_copy_snapshot(
        rl_viewport_handle session, uint64_t snapshot, char* output, uint32_t capacity);
    RENDERLAB_EDITOR_API rl_status RENDERLAB_EDITOR_CALL
    rl_viewport_release_snapshot(rl_viewport_handle session, uint64_t snapshot);
    /* Nonblocking retirement handshake. UI continues dispatching window messages while waiting. */
    RENDERLAB_EDITOR_API rl_status RENDERLAB_EDITOR_CALL
    rl_viewport_begin_stop(rl_viewport_handle session);
    RENDERLAB_EDITOR_API rl_status RENDERLAB_EDITOR_CALL
    rl_viewport_is_retired(rl_viewport_handle session, uint32_t* retired);
    /* UI-thread APIs. Stop retires GPU work but retains the session for final log drain. */
    RENDERLAB_EDITOR_API rl_status RENDERLAB_EDITOR_CALL
    rl_viewport_stop(rl_viewport_handle session, rl_viewport_close_report* report);
    /* UTF-8 JSON batch; capacity queries pin the batch, a successful copy consumes it. */
    RENDERLAB_EDITOR_API rl_status RENDERLAB_EDITOR_CALL rl_viewport_read_logs(
        rl_viewport_handle session, char* output, uint32_t capacity, uint32_t* required);
    /* Position XYZ + quaternion XYZW; no scene object identity crosses the bridge. */
    RENDERLAB_EDITOR_API rl_status RENDERLAB_EDITOR_CALL
    rl_viewport_camera(rl_viewport_handle session, float* pose, uint32_t write);
    /* UTF-8 paths are borrowed for this call. backend: 0 DX12, 1 Vulkan. UI STA only.
     * target_fps: 0 disables CPU pacing and forced VSync, 1..240 caps rendering with VSync.
     * The bridge owns the returned child HWND; the managed host must destroy the session first.
     */
    RENDERLAB_EDITOR_API rl_status RENDERLAB_EDITOR_CALL
    rl_viewport_create(void* parent_hwnd, uint32_t backend, uint32_t validation, uint32_t profiling,
                       uint32_t target_fps, uint32_t continuous, const char* project_utf8,
                       const char* shaders_utf8, const char* log_utf8, rl_viewport_handle* session,
                       void** child_hwnd);
    /* All session calls run on its creating thread. No message loop is pumped by the bridge. */
    RENDERLAB_EDITOR_API rl_status RENDERLAB_EDITOR_CALL
    rl_viewport_update(rl_viewport_handle session, uint32_t paused);
    /* pipeline: 1 Forward, 2 Deferred, 3 RayTracing. Failed selection retains the prior pipeline.
     */
    RENDERLAB_EDITOR_API rl_status RENDERLAB_EDITOR_CALL rl_viewport_configure(
        rl_viewport_handle session, uint32_t pipeline, float exposure, uint32_t reset_camera);
    RENDERLAB_EDITOR_API rl_status RENDERLAB_EDITOR_CALL
    rl_viewport_get_stats(rl_viewport_handle session, rl_viewport_stats* stats);
    /* Drain completed frame snapshots in order; bounded to 512 samples. activity:
     * 0 initializing, 1 active, 2 idle, 3 paused, 4 resize debounce. Polling never waits for GPU.
     */
    RENDERLAB_EDITOR_API rl_status RENDERLAB_EDITOR_CALL rl_viewport_read_frames(
        rl_viewport_handle session, rl_viewport_stats* frames, uint32_t capacity, uint32_t* count);
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
