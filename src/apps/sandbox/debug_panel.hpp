#pragma once

#include "platform/window.hpp"
#include "renderer/scene_renderer.hpp"

#include <memory>
#include <string_view>

namespace renderlab::sandbox
{
struct DebugControls
{
    renderer::SceneRenderOptions render;
    float exposure = 0;
    bool shadows = false, freezeCamera = false, resetCamera = false;
};
/** @brief Owned modeless Win32 controls; the creating thread pumps and destroys the panel. */
class DebugPanel final
{
public:
    DebugPanel(platform::NativeSurface owner, DebugControls controls, bool rayTracingAvailable);
    ~DebugPanel();
    DebugPanel(const DebugPanel&) = delete;
    DebugPanel& operator=(const DebugPanel&) = delete;
    DebugControls& Controls();
    void Toggle();
    void SelectPipeline(renderer::PipelineType pipeline);
    void Update(std::string_view statistics);
    /// Exercises the native command route and restores all controls; used by window acceptance.
    void VerifyControls();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace renderlab::sandbox
