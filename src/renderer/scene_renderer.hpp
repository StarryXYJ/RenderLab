#pragma once

#include "renderer/indexed_scene.hpp"
#include "renderer/pipeline.hpp"
#include "rendergraph/types.hpp"
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
    Brdf,
    ReflectionRaw,
    ReflectionHistory,
    ReflectionDenoised
};
enum class GeometrySubmission
{
    Direct,
    IndexedDirect,
    CpuIndirect,
    GpuCulled
};
struct SceneRenderOptions
{
    PipelineType pipeline = PipelineType::Forward;
    GeometrySubmission submission = GeometrySubmission::Direct;
    uint32_t recordingThreads = 1;
    bool asyncCompute = false;
    /// Optional count/visible-index readback from this submission; never controls its draws.
    bool cullingDiagnostics = false;
    SceneView view = SceneView::Final;
    bool profile = false;
    /// Zero preserves the scene setting; otherwise a power of two in [128,2048].
    uint32_t shadowResolution = 0;
    bool reflections = true;
    /// Progressive stationary accumulation. Disable for deterministic single-sample comparisons.
    bool reflectionHistory = true;
    /// Display reconstruction; never feeds back into progressive history.
    bool reflectionDenoise = true;
    /// Camera reprojection for static geometry; independent from the unfiltered running mean.
    bool reflectionTemporal = true;
    /// Read back per-pixel history weight, history length, reprojection path and stable hit
    /// distance.
    bool reflectionDiagnostics = false;
};
/// @brief Submission completion point and optional pixels; wait before mapping the readback buffer.
struct SceneReadback
{
    uint64_t completion = 0;
    std::shared_ptr<rhi::Buffer> pixels;
    rendergraph::Submission::GpuProfile profile;
    uint32_t reflectionSamples = 0;
    bool reflectionHistoryReset = false;
    /// Consecutive compatible source frames, not a count of accepted pixels or reflection spp.
    uint32_t reflectionTemporalFrames = 0;
    /// RGBA32F diagnostics with TextureRowPitch(width, Rgba32Float); wait for completion.
    std::shared_ptr<rhi::Buffer> reflectionDiagnostics;
    /// Optional tightly packed float2 per pixel: centered luminance moment and filter control.
    /// The moment covers the same raw samples as progressive history; wait for completion.
    std::shared_ptr<rhi::Buffer> reflectionStatistics;
    uint64_t compileNs = 0, prepareNs = 0, recordNs = 0;
    /// CPU scene/material preparation and graph construction, before graph compilation.
    uint64_t scenePrepareNs = 0;
    IndexedSceneStatistics indexedScene;
    IndexedCullDiagnostics culling;
};
/// Cumulative ray-surface preparation work; no timing overhead.
struct RaySceneStatistics
{
    uint64_t geometryPacks = 0, uploadBatches = 0, uploadedBytes = 0;
    uint64_t historyEvictions = 0, cachedViews = 0, historyBytes = 0;
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
    /// Validate composition/material compatibility before changing an interactive selection.
    void ValidatePipeline(PipelineType pipeline);
    const scene::Snapshot& LastSnapshot() const;
    const std::map<std::string, assets::Model>& Models() const;
    rhi::UploadPool::Statistics UploadStatistics() const;
    RaySceneStatistics RayStatistics() const;
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
