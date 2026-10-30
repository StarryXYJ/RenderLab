#pragma once

#include "editor/native/editor_bridge.h"
#include "renderer/scene_renderer.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace renderlab::editor
{
inline void ValidateConfig(const rl_viewport_config& c)
{
    const auto require = [](bool value, const char* reason)
    {
        if (!value)
            throw std::invalid_argument(reason);
    };
    require(c.size == sizeof(c) && c.version == 1 && c.request_id > 0,
            "Invalid configuration size/version/request ID");
    require(c.pipeline >= 1 && c.pipeline <= 3 && c.submission <= 3 && c.view <= 7,
            "Unknown pipeline/submission/debug view");
    require(c.recording_threads >= 1 && c.recording_threads <= 16,
            "Recording threads must be in [1,16]");
    require(c.async_compute <= 1 && c.memory_alias <= 1 && c.profiling <= 1 && c.continuous <= 1 &&
                c.target_fps <= 240 && (c.flags & ~63u) == 0,
            "Invalid execution flags or target FPS");
    require(c.shadow_resolution >= 128 && c.shadow_resolution <= 2048 &&
                !(c.shadow_resolution & (c.shadow_resolution - 1)),
            "Shadow resolution must be a power of two in [128,2048]");
    const auto range = [&](float value, float minimum, float maximum)
    {
        require(std::isfinite(value) && value >= minimum && value <= maximum,
                "Configuration contains a non-finite or out-of-range scalar");
    };
    range(c.exposure, -10, 10);
    range(c.camera_speed, .01f, 1000);
    range(c.light_x, -1, 1);
    range(c.light_y, -1, 1);
    range(c.light_z, -1, 1);
    require(c.light_x * c.light_x + c.light_y * c.light_y + c.light_z * c.light_z > .000001f,
            "Light direction must be nonzero");
    range(c.light_r, 0, 64);
    range(c.light_g, 0, 64);
    range(c.light_b, 0, 64);
    range(c.light_intensity, 0, 10000);
    range(c.shadow_bias, 0, 1);
    range(c.shadow_normal_bias, 0, 10);
    range(c.environment_intensity, 0, 1000);
    range(c.environment_rotation, -360, 360);
    require(c.view < 5 || (c.pipeline == 3 && (c.flags & 1)),
            "Reflection debug views require RayTracing with reflections enabled");
}
inline renderer::SceneRenderOptions RenderOptions(const rl_viewport_config& c,
                                                  const scene::MainLight& source)
{
    renderer::SceneRenderOptions options;
    options.pipeline = static_cast<renderer::PipelineType>(c.pipeline);
    options.submission = static_cast<renderer::GeometrySubmission>(c.submission);
    options.recordingThreads = c.recording_threads;
    options.asyncCompute = c.async_compute != 0;
    options.memoryAlias = c.memory_alias != 0;
    options.profile = c.profiling != 0;
    options.cullingDiagnostics = (c.flags & 32) != 0;
    options.view = static_cast<renderer::SceneView>(c.view);
    options.reflections = (c.flags & 1) != 0;
    options.reflectionHistory = (c.flags & 2) != 0;
    options.reflectionDenoise = (c.flags & 4) != 0;
    options.reflectionTemporal = (c.flags & 8) != 0;
    auto light = source;
    light.direction = math::Normalize({c.light_x, c.light_y, c.light_z});
    light.color = {c.light_r, c.light_g, c.light_b};
    light.intensity = c.light_intensity;
    light.shadows = (c.flags & 16) != 0;
    light.shadowResolution = c.shadow_resolution;
    light.shadowBias = c.shadow_bias;
    light.shadowNormalBias = c.shadow_normal_bias;
    options.lightOverride = std::move(light);
    options.environmentIntensity = c.environment_intensity;
    options.environmentRotation = c.environment_rotation;
    options.exposure = c.exposure;
    return options;
}
inline rl_viewport_config DefaultConfig(const scene::Snapshot& source, uint32_t targetFps,
                                        bool continuous, bool profile)
{
    rl_viewport_config c{};
    c.size = sizeof(c);
    c.version = 1;
    c.request_id = 1;
    c.pipeline = 2;
    c.recording_threads = 1;
    c.profiling = profile;
    c.target_fps = targetFps;
    c.continuous = continuous;
    c.flags = 15u | (source.light.shadows ? 16u : 0u);
    c.shadow_resolution = 1024;
    c.exposure = source.exposure;
    c.camera_speed = 3;
    c.light_x = source.light.direction.x;
    c.light_y = source.light.direction.y;
    c.light_z = source.light.direction.z;
    c.light_r = source.light.color.x;
    c.light_g = source.light.color.y;
    c.light_b = source.light.color.z;
    c.light_intensity = source.light.intensity;
    c.shadow_bias = source.light.shadowBias;
    c.shadow_normal_bias = source.light.shadowNormalBias;
    c.environment_intensity = source.environmentIntensity;
    c.environment_rotation = source.environmentRotation;
    return c;
}
} // namespace renderlab::editor
