#pragma once

#include "assets/model.hpp"
#include "renderer/frame.hpp"
#include "rhi/runtime.hpp"
#include "scene/scene.hpp"
#include "shader/package.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace renderlab::rendergraph
{
class Graph;
}
namespace renderlab::renderer
{
inline constexpr uint32_t IndexedTextureCapacity = 64, IndexedInstanceCapacity = 16384;
struct IndexedInstanceData
{
    std::array<float, 16> world{}, normal{}, previousWorld{};
    std::array<float, 4> minimum{}, maximum{};
    std::array<uint32_t, 4> indices{}, reserved{};
};
struct IndexedMaterialData
{
    std::array<float, 4> baseColor{1, 1, 1, 1}, emissionMetallic{};
    std::array<float, 4> factors{.5f, 1, 1, .5f};
    std::array<uint32_t, 4> maps{}, flags{};
    std::array<uint32_t, 12> reserved{};
};
static_assert(sizeof(IndexedInstanceData) == 256 && sizeof(IndexedMaterialData) == 128);
struct IndexedDrawInput
{
    scene::Instance instance;
    const assets::Mesh* mesh = nullptr;
    std::string meshKey;
    IndexedMaterialData material;
    // Base, metallic/roughness, normal, occlusion, emission; original image resolutions.
    std::array<std::shared_ptr<rhi::TextureView>, 5> textures;
    std::array<std::string, 5> textureNames;
    std::shared_ptr<rhi::Pipeline> reference;
    int32_t queue = 2000;
    uint32_t stencilReference = 0;
    bool gbuffer = false;
};
struct IndexedFrameBindings
{
    BufferBinding view, lighting, shadow;
    std::string environment, brdf;
    math::Mat4 viewProjection;
};
struct IndexedSceneStatistics
{
    uint32_t instances = 0, visible = 0, materials = 0, textures = 0, batches = 0;
    uint64_t geometryUploads = 0;
};
/** @brief Immutable per-frame indexed raster projection. Runtime outlives this owner. */
class IndexedScene final
{
public:
    IndexedScene(rhi::Runtime&, shader::Target, const std::filesystem::path& shaders,
                 const shader::Interface& types);
    ~IndexedScene();
    /// Prepare before recording; resources and tables are pinned by graph commands.
    void Build(rendergraph::Graph&, const std::vector<IndexedDrawInput>&,
               const IndexedFrameBindings&, bool indirect, std::vector<DrawPacket>& opaque,
               std::vector<DrawPacket>& gbuffer);
    /// Seal upload pages only after successful submission. A failed build may be retried.
    void Commit(uint64_t completion);
    IndexedSceneStatistics Statistics() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace renderlab::renderer
