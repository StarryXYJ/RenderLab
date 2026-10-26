#pragma once

#include "rendergraph/types.hpp"
#include "renderer/pipeline.hpp"
#include "rhi/resources.hpp"
#include "rhi/upload.hpp"
#include "scene/scene.hpp"
#include "shader/definition.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>

namespace renderlab::rhi
{
class Runtime;
class Swapchain;
} // namespace renderlab::rhi
namespace renderlab::renderer
{
enum class SceneView
{
    Final,
    Hdr,
    Shadow,
    Environment,
    Brdf
};
struct SceneRenderOptions
{
    PipelineType pipeline = PipelineType::Forward;
    SceneView view = SceneView::Final;
    bool profile = false;
    /// Zero preserves the scene setting; otherwise a power of two in [128,2048].
    uint32_t shadowResolution = 0;
};
/// @brief Submission completion point and optional pixels; wait before mapping the readback buffer.
struct SceneReadback
{
    uint64_t completion = 0;
    std::shared_ptr<rhi::Buffer> pixels;
    rendergraph::Submission::GpuProfile profile;
    uint64_t compileNs = 0, prepareNs = 0, recordNs = 0;
};
/** @brief Project/scene to the existing Forward composition. Runtime outlives the renderer. */
class SceneRenderer final
{
public:
    /// @brief Loads the project, compiles scene shaders and synchronously uploads static assets.
    SceneRenderer(rhi::Runtime&, shader::Target, const std::filesystem::path& project,
                  const std::filesystem::path& engineShaders);
    ~SceneRenderer();
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;
    /// @brief Owner-thread editable scene; apply changes between Render calls.
    scene::Scene& Document();
    const scene::Snapshot& LastSnapshot() const;
    const std::map<std::string, assets::Model>& Models() const;
    rhi::UploadPool::Statistics UploadStatistics() const;
    /** @brief Extracts scene values and submits the existing Forward/Graph composition.
     * initial must match the target state; output is Present for a swapchain, otherwise CopySource.
     * Normal frames do not wait for GPU completion. Optional readback uses a padded row pitch.
     */
    SceneReadback Render(std::shared_ptr<rhi::Texture> target, rhi::State initial,
                         rhi::Swapchain* swapchain = nullptr, bool readback = false,
                         const std::filesystem::path& dump = {}, SceneRenderOptions options = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace renderlab::renderer
