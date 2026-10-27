#include "renderer/scene_renderer.hpp"
#include "material/render_state.hpp"
#include "renderer/acceleration_scene.hpp"
#include "renderer/builtin_shaders.hpp"
#include "renderer/ray_surface.hpp"
#include "renderer/raytracing.hpp"

#include "assets/io.hpp"
#include "assets/json_io.hpp"
#include "material/runtime.hpp"
#include "renderer/environment.hpp"
#include "renderer/forward.hpp"
#include "renderer/pipeline.hpp"
#include "renderer/shadow.hpp"
#include "rendergraph/graph.hpp"
#include "rendergraph/pool.hpp"
#include "rhi/commands.hpp"
#include "rhi/upload.hpp"
#include "scene_parameters.hpp"
#include "shader/compiler.hpp"
#include "shader/dxc_compiler.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>

namespace renderlab::renderer
{
namespace
{
std::array<float, 4> Vector(math::Vec3 value, float w = 0)
{
    return {value.x, value.y, value.z, w};
}
} // namespace
struct SceneRenderer::Impl
{
    rhi::Runtime& runtime;
    shader::Target shaderTarget;
    project::Project project;
    assets::Database database;
    scene::Scene document;
    std::map<std::string, assets::Model> models;
    // Models own the images; map nodes and their image vectors stay stable after insertion.
    std::map<std::string, const assets::Image*> modelImages;
    struct Geometry
    {
        std::shared_ptr<rhi::Buffer> vertices, indices;
    };
    std::map<std::string, Geometry> geometry;
    std::map<std::string, std::shared_ptr<rhi::TextureView>> textures;
    std::shared_ptr<rhi::Sampler> sampler;
    std::shared_ptr<const shader::Package> package;
    std::map<std::string, std::shared_ptr<const shader::Package>> packages;
    std::map<std::string, std::filesystem::path> engineShaders;
    std::map<const shader::Package*, std::shared_ptr<const material::GpuGeneration>> generations;
    std::shared_ptr<rhi::TextureView> environmentAtlas, brdfLut;
    std::string environmentKey;
    std::array<float, 3> environmentAmbient{};
    std::shared_ptr<const shader::Package> toneMapping;
    std::shared_ptr<const shader::Package> deferredLighting;
    std::shared_ptr<rhi::Buffer> fullscreen;
    std::shared_ptr<rhi::TextureView> offscreenDisplayView;
    struct MaterialInput
    {
        std::shared_ptr<const shader::Package> package;
        material::Asset asset;
    };
    struct CachedMaterial
    {
        std::filesystem::file_time_type version;
        MaterialInput input;
    };
    std::map<std::string, CachedMaterial> materialAssets;
    material::Runtime materials;
    rendergraph::Pool pool;
    rhi::UploadPool uploadPool;
    AccelerationScene accelerationScene;
    std::shared_ptr<rhi::Pipeline> rayQueryShadow;
    std::shared_ptr<rhi::ShaderTable> reflectionTable;
    std::shared_ptr<rhi::Pipeline> reflectionHistory, reflectionCombine;
    std::array<std::shared_ptr<rhi::Buffer>, 5> raySurfaceBuffers;
    std::array<std::vector<std::byte>, 5> raySurfaceBytes;
    uint64_t raySurfaceVersion = 0, rayAtlasVersion = 0;
    EnvironmentImage rayAtlas;
    std::shared_ptr<rhi::TextureView> rayAtlasView;
    std::map<std::string, uint32_t> rayTiles;
    bool rayAtlasDirty = false;
    struct ReflectionHistory
    {
        std::string signature;
        std::array<std::shared_ptr<rhi::Texture>, 2> radiance;
        std::array<bool, 2> initialized{};
        uint32_t index = 0;
        uint32_t samples = 0, sequence = 0;
        uint64_t completion = 0;
    };
    std::map<std::string, ReflectionHistory> reflectionViews;
    RenderPipeline forwardPipeline =
        CreatePipeline(PipelineType::Forward)
            .AddFeature(std::make_shared<ShadowFeature>(), RenderStage::BeforeOpaque)
            .Create();
    RenderPipeline deferredPipeline =
        CreatePipeline(PipelineType::Deferred)
            .AddFeature(std::make_shared<ShadowFeature>(), RenderStage::BeforeOpaque)
            .Create();
    RenderPipeline rayTracingPipeline =
        CreatePipeline(PipelineType::RayTracing)
            .AddFeature(std::make_shared<ShadowFeature>(), RenderStage::BeforeOpaque)
            .Create();
    math::Mat4 shadowMatrix = math::Mat4::Identity();
    scene::Extractor extractor;
    scene::Snapshot snapshot;
    Impl(rhi::Runtime& r, shader::Target target, const std::filesystem::path& manifest,
         const std::filesystem::path& engine)
        : runtime(r), shaderTarget(target), project(project::Project::Open(manifest, engine)),
          database(project), document(scene::Scene::Load(database.Get(project.startScene).path)),
          materials(r, target),
          // Two 2048 shadow color/depth pairs plus 720p HDR/depth fit below the RHI budget.
          pool(r, 64, 224ull * 1024 * 1024), uploadPool(r), accelerationScene(r, pool)
    {
        for (const auto& file : std::filesystem::recursive_directory_iterator(project.engine))
        {
            if (!file.is_regular_file() || file.path().extension() != ".shader")
                continue;
            auto meta = file.path();
            meta += ".meta";
            if (!std::filesystem::is_regular_file(meta))
                continue;
            const auto metadata = assets::detail::LoadJson(meta);
            const auto id = metadata.at("id").get<std::string>();
            if (metadata.at("version") != 1 || !project::ValidId(id) || database.Contains(id))
                throw std::invalid_argument("Invalid or conflicting engine Shader asset identity");
            const auto path =
                project::Inside(project.engine, file.path().lexically_relative(project.engine));
            if (!engineShaders.emplace(id, path).second)
                throw std::invalid_argument("Duplicate engine Shader asset ID " + id);
        }
        shader::CompileOptions options;
        options.cacheDirectory = project.library / "Shaders";
        options.includeDirectories = {project.engine};
        const auto builtinPath = [&](std::string_view id) -> const std::filesystem::path&
        {
            const auto found = engineShaders.find(std::string(id));
            if (found == engineShaders.end())
                throw std::invalid_argument("Missing built-in Shader asset ID " + std::string(id));
            return found->second;
        };
        const auto compiled = shader::CompileShader(builtinPath(builtin::Lit), options);
        if (!compiled)
            throw std::runtime_error(compiled.diagnostics);
        package = compiled.package;
        packages.emplace(package->definition.assetId, package);
        const auto display = shader::CompileShader(builtinPath(builtin::ToneMapping), options);
        if (!display)
            throw std::runtime_error(display.diagnostics);
        toneMapping = display.package;
        const auto lighting =
            shader::CompileShader(builtinPath(builtin::DeferredLighting), options);
        if (!lighting)
            throw std::runtime_error(lighting.diagnostics);
        deferredLighting = lighting.package;
        const std::array<float, 6> triangle{-1, -1, 3, -1, -1, 3};
        fullscreen =
            Upload(std::as_bytes(std::span{triangle}), "Display.Triangle", rhi::State::Vertex);
        sampler = runtime.CreateSampler();
        UploadImage("white/Srgb", {1, 1, {255, 255, 255, 255}}, true);
        UploadImage("white/Linear", {1, 1, {255, 255, 255, 255}}, false);
        textures.emplace("normal/Linear",
                         UploadFloat("Builtin.FlatNormal", {1, 1, {.5f, .5f, 1, 1}}));
        brdfLut = UploadFloat("Environment.Brdf", IntegrateEnvironmentBrdf());
        EnsureModels();
    }
    std::shared_ptr<rhi::Buffer> Upload(std::span<const std::byte> bytes, const std::string& name,
                                        rhi::State state, bool accelerationInput = false)
    {
        auto staging = runtime.CreateBuffer({bytes.size(), rhi::Memory::Upload, name + ".Upload"});
        std::memcpy(staging->Map().data(), bytes.data(), bytes.size());
        const bool addressable = accelerationInput && runtime.SupportsAccelerationStructures();
        auto destination = runtime.CreateBuffer(
            {bytes.size(), rhi::Memory::Device, name, false, addressable, addressable});
        auto commands = runtime.CreateCommands();
        commands->Barrier(staging, rhi::State::HostWrite, rhi::State::CopySource);
        commands->Barrier(destination, rhi::State::Undefined, rhi::State::CopyDest);
        commands->Copy(staging, destination, bytes.size());
        commands->Barrier(destination, rhi::State::CopyDest, state);
        runtime.Wait(runtime.Submit(std::move(commands)));
        return destination;
    }
    std::shared_ptr<rhi::TextureView> UploadFloat(const std::string& name,
                                                  const EnvironmentImage& image)
    {
        const auto pitch = rhi::TextureRowPitch(image.width, rhi::Format::Rgba32Float);
        auto staging = runtime.CreateBuffer(
            {uint64_t(pitch) * image.height, rhi::Memory::Upload, name + ".Upload"});
        const auto mapped = staging->Map();
        std::memset(mapped.data(), 0, mapped.size());
        for (uint32_t y = 0; y < image.height; ++y)
            std::memcpy(mapped.data() + size_t(y) * pitch,
                        image.pixels.data() + size_t(y) * image.width * 4,
                        size_t(image.width) * 16);
        auto texture = runtime.CreateTexture(
            {image.width, image.height, rhi::Format::Rgba32Float, false, false, name});
        auto commands = runtime.CreateCommands();
        commands->Barrier(staging, rhi::State::HostWrite, rhi::State::CopySource);
        commands->Barrier(texture, rhi::State::Undefined, rhi::State::CopyDest);
        commands->CopyTexture(staging, texture, pitch, true);
        commands->Barrier(texture, rhi::State::CopyDest, rhi::State::Sampled);
        runtime.Wait(runtime.Submit(std::move(commands)));
        return runtime.CreateView(texture);
    }
    void UpdateEnvironment()
    {
        const auto& ambient = snapshot.ambient;
        const std::array<float, 3> color{ambient.x, ambient.y, ambient.z};
        if (environmentAtlas && snapshot.environment == environmentKey &&
            (!snapshot.environment.empty() || color == environmentAmbient))
            return;
        assets::Image source;
        if (!snapshot.environment.empty())
            source =
                assets::DecodeImage(assets::detail::Read(database.Get(snapshot.environment).path));
        environmentAtlas = UploadFloat(
            "Environment.Atlas", PrefilterEnvironment(source.width ? &source : nullptr, ambient));
        environmentKey = snapshot.environment;
        environmentAmbient = color;
    }
    void UploadImage(const std::string& name, const assets::Image& image, bool srgb)
    {
        const auto pitch = rhi::TextureRowPitch(image.width);
        auto staging = runtime.CreateBuffer(
            {uint64_t(pitch) * image.height, rhi::Memory::Upload, name + ".Upload"});
        const auto mapped = staging->Map();
        std::memset(mapped.data(), 0, mapped.size());
        for (uint32_t y = 0; y < image.height; ++y)
            std::memcpy(mapped.data() + size_t(y) * pitch,
                        image.rgba.data() + size_t(y) * image.width * 4, size_t(image.width) * 4);
        auto texture = runtime.CreateTexture({image.width, image.height,
                                              srgb ? rhi::Format::Rgba8Srgb : rhi::Format::Rgba8,
                                              false, false, name});
        auto commands = runtime.CreateCommands();
        commands->Barrier(staging, rhi::State::HostWrite, rhi::State::CopySource);
        commands->Barrier(texture, rhi::State::Undefined, rhi::State::CopyDest);
        commands->CopyTexture(staging, texture, pitch, true);
        commands->Barrier(texture, rhi::State::CopyDest, rhi::State::Sampled);
        runtime.Wait(runtime.Submit(std::move(commands)));
        textures.emplace(name, runtime.CreateView(texture));
    }
    void EnsureModels()
    {
        for (const auto& object : document.objects)
            if (object.mesh && !models.contains(object.mesh->model))
            {
                auto model = assets::LoadModel(database, object.mesh->model);
                for (const auto& mesh : model.meshes)
                {
                    const auto name = model.id + "/" + mesh.id;
                    geometry.emplace(name,
                                     Geometry{Upload(std::as_bytes(std::span{mesh.vertices}),
                                                     name + ".Vertices", rhi::State::Vertex, true),
                                              Upload(std::as_bytes(std::span{mesh.indices}),
                                                     name + ".Indices", rhi::State::Index, true)});
                }
                const auto id = model.id;
                const auto& stored = models.emplace(id, std::move(model)).first->second;
                for (size_t i = 0; i < stored.images.size(); ++i)
                    modelImages.emplace(id + "/Image/" + std::to_string(i), &stored.images[i]);
            }
    }
    void EnsureRayQueryProgram()
    {
        if (rayQueryShadow)
            return;
        if (!runtime.SupportsAccelerationStructures() || !runtime.SupportsRayQuery())
            throw std::invalid_argument(
                "Ray Tracing requires accelerationStructure and rayQuery device capabilities");
        shader::ProgramRequest request;
        request.entry = {rhi::ShaderStage::Compute,
                         project.engine / "RenderLab" / "RayQueryShadow.hlsl", "CSMain"};
        request.target = shaderTarget;
        request.targetProfile = "cs_6_5";
        if (shaderTarget == shader::Target::Spirv)
            request.defines["SPIRV"] = "1";
        shader::DxcCompiler compiler;
        rhi::PipelineDesc description;
        description.layout = runtime.CreateLayout({{0, rhi::BindingKind::AccelerationStructure},
                                                   {1, rhi::BindingKind::SampledTexture},
                                                   {2, rhi::BindingKind::SampledTexture},
                                                   {3, rhi::BindingKind::ConstantBuffer},
                                                   {4, rhi::BindingKind::StorageTexture}});
        description.compute =
            runtime.CreateShader(rhi::ShaderStage::Compute, compiler.Compile(request), "CSMain");
        rayQueryShadow = runtime.CreatePipeline(std::move(description));
    }
    void EnsureReflectionPrograms()
    {
        if (reflectionTable)
            return;
        if (!runtime.SupportsRayTracingPipeline())
            throw std::invalid_argument("Reflections require rayTracingPipeline capability");
        shader::DxcCompiler compiler;
        const auto module = [&](const char* file, rhi::ShaderStage stage, const char* entry)
        {
            shader::ProgramRequest request;
            request.entry = {stage, project.engine / "RenderLab" / file, entry};
            request.target = shaderTarget;
            if (shaderTarget == shader::Target::Spirv)
                request.defines["SPIRV"] = "1";
            return runtime.CreateShader(stage, compiler.Compile(request), entry);
        };
        rhi::PipelineDesc desc;
        desc.layout = runtime.CreateLayout({{0, rhi::BindingKind::AccelerationStructure},
                                            {1, rhi::BindingKind::StorageTexture},
                                            {2, rhi::BindingKind::SampledTexture},
                                            {3, rhi::BindingKind::SampledTexture},
                                            {4, rhi::BindingKind::ConstantBuffer},
                                            {5, rhi::BindingKind::ReadOnlyBuffer},
                                            {6, rhi::BindingKind::ReadOnlyBuffer},
                                            {7, rhi::BindingKind::ReadOnlyBuffer},
                                            {8, rhi::BindingKind::ReadOnlyBuffer},
                                            {9, rhi::BindingKind::ReadOnlyBuffer},
                                            {10, rhi::BindingKind::SampledTexture},
                                            {11, rhi::BindingKind::Sampler},
                                            {12, rhi::BindingKind::SampledTexture},
                                            {13, rhi::BindingKind::SampledTexture},
                                            {15, rhi::BindingKind::SampledTexture}});
        desc.rayGeneration =
            module("RayReflection.hlsl", rhi::ShaderStage::RayGeneration, "RayGeneration");
        desc.miss = module("RayReflection.hlsl", rhi::ShaderStage::Miss, "Miss");
        desc.closestHit = module("RayReflection.hlsl", rhi::ShaderStage::ClosestHit, "ClosestHit");
        reflectionTable = runtime.CreateShaderTable(runtime.CreatePipeline(desc));
        desc = {};
        desc.layout = runtime.CreateLayout({{0, rhi::BindingKind::SampledTexture},
                                            {1, rhi::BindingKind::SampledTexture},
                                            {2, rhi::BindingKind::StorageTexture},
                                            {3, rhi::BindingKind::ConstantBuffer}});
        desc.compute = module("ReflectionPost.hlsl", rhi::ShaderStage::Compute, "Accumulate");
        reflectionHistory = runtime.CreatePipeline(desc);
        desc = {};
        desc.layout = runtime.CreateLayout({{0, rhi::BindingKind::SampledTexture},
                                            {2, rhi::BindingKind::StorageTexture},
                                            {3, rhi::BindingKind::ConstantBuffer},
                                            {4, rhi::BindingKind::SampledTexture},
                                            {5, rhi::BindingKind::SampledTexture},
                                            {6, rhi::BindingKind::SampledTexture},
                                            {7, rhi::BindingKind::SampledTexture},
                                            {8, rhi::BindingKind::SampledTexture},
                                            {9, rhi::BindingKind::SampledTexture},
                                            {10, rhi::BindingKind::SampledTexture}});
        desc.compute = module("ReflectionPost.hlsl", rhi::ShaderStage::Compute, "Combine");
        reflectionCombine = runtime.CreatePipeline(desc);
    }
    uint32_t RayTile(const std::string& reference, bool srgb)
    {
        if (rayAtlas.pixels.empty())
        {
            rayAtlas = {RayAtlasSize, RayAtlasSize,
                        std::vector<float>(size_t(RayAtlasSize) * RayAtlasSize * 4, 1)};
            for (uint32_t y = 0; y < RayAtlasTilePitch; ++y)
                for (uint32_t x = RayAtlasTilePitch; x < RayAtlasTilePitch * 2; ++x)
                {
                    const auto pixel = (size_t(y) * RayAtlasSize + x) * 4;
                    rayAtlas.pixels[pixel] = rayAtlas.pixels[pixel + 1] = .5f;
                }
            rayAtlasDirty = true;
        }
        if (reference == "white")
            return 0;
        if (reference == "normal")
            return 1;
        const auto embedded = modelImages.find(reference);
        std::string key = reference + (srgb ? "/Srgb" : "/Linear");
        if (embedded == modelImages.end())
            key +=
                "/" + std::to_string(std::filesystem::last_write_time(database.Get(reference).path)
                                         .time_since_epoch()
                                         .count());
        if (const auto found = rayTiles.find(key); found != rayTiles.end())
            return found->second;
        if (rayTiles.size() >= 62)
            throw std::invalid_argument(
                "Ray material atlas exceeds 62 image versions; restart or reduce texture count");
        assets::Image decoded;
        if (embedded == modelImages.end())
            decoded = assets::DecodeImage(assets::detail::Read(database.Get(reference).path));
        const auto& image = embedded != modelImages.end() ? *embedded->second : decoded;
        const auto tile = static_cast<uint32_t>(rayTiles.size()) + 2;
        rayTiles.emplace(key, tile);
        const auto linear = [&](float value)
        {
            return srgb ? (value <= .04045f ? value / 12.92f
                                            : std::pow((value + .055f) / 1.055f, 2.4f))
                        : value;
        };
        for (uint32_t y = 0; y < RayAtlasTilePitch; ++y)
            for (uint32_t x = 0; x < RayAtlasTilePitch; ++x)
            {
                const auto sx = std::min(image.width - 1, uint32_t(std::clamp(int(x) - 1, 0, 63)) *
                                                              image.width / 64);
                const auto sy = std::min(image.height - 1, uint32_t(std::clamp(int(y) - 1, 0, 63)) *
                                                               image.height / 64);
                const auto source = (size_t(sy) * image.width + sx) * 4;
                const auto destination =
                    (size_t(tile / 8 * 66 + y) * RayAtlasSize + tile % 8 * 66 + x) * 4;
                for (uint32_t c = 0; c < 4; ++c)
                    rayAtlas.pixels[destination + c] = c < 3
                                                           ? linear(image.rgba[source + c] / 255.f)
                                                           : image.rgba[source + c] / 255.f;
            }
        rayAtlasDirty = true;
        return tile;
    }
    RayMaterialData RayMaterial(const MaterialInput& material)
    {
        if (material.package->definition.assetId != builtin::Lit &&
            material.package->definition.assetId != "9120dfaac72c5c1c88f3a41e3ac22447")
            throw std::invalid_argument(
                "Ray reflections support built-in Lit/Unlit materials only");
        RayMaterialData output;
        output.normalFlags[1] = material.package->definition.assetId == builtin::Lit ? 0 : 1;
        RayTile("white", false);
        const shader::MaterialTemplate definition(material.package);
        for (const auto& property : definition.Properties())
        {
            using S = shader::MaterialSemantic;
            if (property.type.resource)
            {
                const auto found = material.asset.resources.find(property.id);
                const auto& reference = found == material.asset.resources.end()
                                            ? property.resourceDefault
                                            : found->second.reference;
                switch (property.semantic)
                {
                case S::BaseMap:
                    output.maps[0] = RayTile(reference, true);
                    break;
                case S::MetallicRoughnessMap:
                    output.maps[1] = RayTile(reference, false);
                    break;
                case S::OcclusionMap:
                    output.maps[2] = RayTile(reference, false);
                    break;
                case S::EmissionMap:
                    output.maps[3] = RayTile(reference, true);
                    break;
                case S::NormalMap:
                    output.normalFlags[0] = RayTile(reference, false);
                    break;
                default:
                    break;
                }
                continue;
            }
            const auto found = material.asset.values.find(property.id);
            const auto& values =
                found == material.asset.values.end() ? property.defaults : found->second.numbers;
            const auto value = [&](size_t i) { return static_cast<float>(values.at(i)); };
            switch (property.semantic)
            {
            case S::BaseColor:
                for (uint32_t i = 0; i < 3; ++i)
                    output.baseMetallic[i] = value(i);
                break;
            case S::Metallic:
                output.baseMetallic[3] = value(0);
                break;
            case S::Roughness:
                output.emissionRoughness[3] = value(0);
                break;
            case S::Emission:
                for (uint32_t i = 0; i < 3; ++i)
                    output.emissionRoughness[i] = value(i);
                break;
            case S::NormalScale:
                output.options[0] = value(0);
                break;
            case S::OcclusionStrength:
                output.options[1] = value(0);
                break;
            default:
                break;
            }
        }
        return output;
    }
    MaterialInput MaterialFor(const scene::Instance& instance, const assets::Surface& surface)
    {
        const shader::MaterialTemplate materialTemplate(package);
        if (!instance.material.empty())
        {
            const auto& entry = database.Get(instance.material);
            const auto modified = std::filesystem::last_write_time(entry.path);
            if (auto found = materialAssets.find(instance.material);
                found != materialAssets.end() && found->second.version == modified)
                return found->second.input;
            const auto materialDocument = assets::detail::LoadJson(entry.path);
            if (materialDocument.at("version") != 2)
                throw std::invalid_argument(
                    "Material v1 is obsolete; migrate shader IDs and feature properties to v2");
            const auto shaderId = materialDocument.at("shader").get<std::string>();
            if (!project::ValidId(shaderId))
                throw std::invalid_argument("Material requires a Shader asset ID");
            if (const auto dependency = entry.dependencies.find("shader");
                dependency != entry.dependencies.end() && dependency->second != shaderId)
                throw std::invalid_argument(
                    "Material Shader dependency disagrees with its asset reference");
            if (!packages.contains(shaderId))
            {
                const auto path = engineShaders.contains(shaderId) ? engineShaders.at(shaderId)
                                                                   : database.Get(shaderId).path;
                if (path.extension() != ".shader")
                    throw std::invalid_argument("Material Shader ID must reference .shader");
                shader::CompileOptions options;
                options.cacheDirectory = project.library / "Shaders";
                options.includeDirectories = {project.engine, project.assets};
                options.assetId = shaderId;
                const auto build = shader::CompileShader(path, options);
                if (!build)
                    throw std::runtime_error(build.diagnostics);
                packages.emplace(shaderId, build.package);
            }
            const auto selected = packages.at(shaderId);
            MaterialInput input{selected,
                                material::Load(entry.path, shader::MaterialTemplate(selected))};
            materialAssets.insert_or_assign(instance.material, CachedMaterial{modified, input});
            return input;
        }
        auto result = material::Create(materialTemplate);
        material::Set(result, materialTemplate, shader::MaterialSemantic::Surface,
                      std::array<double, 1>{static_cast<double>(
                          surface.alphaMode == "BLEND" ? shader::SurfaceType::Transparent
                                                       : shader::SurfaceType::Opaque)});
        material::Set(result, materialTemplate, shader::MaterialSemantic::AlphaClip,
                      std::array<double, 1>{surface.alphaMode == "MASK" ? 1.0 : 0.0});
        material::Set(result, materialTemplate, shader::MaterialSemantic::Cull,
                      std::array<double, 1>{static_cast<double>(
                          surface.doubleSided ? rhi::CullMode::None : rhi::CullMode::Back)});
        material::Set(result, materialTemplate, shader::MaterialSemantic::AlphaCutoff,
                      std::array<double, 1>{surface.alphaCutoff});
        material::Set(result, materialTemplate, shader::MaterialSemantic::BaseColor,
                      std::array<double, 4>{surface.color[0], surface.color[1], surface.color[2],
                                            surface.color[3]});
        material::Set(result, materialTemplate, shader::MaterialSemantic::Metallic,
                      std::array<double, 1>{surface.metallic});
        material::Set(result, materialTemplate, shader::MaterialSemantic::Roughness,
                      std::array<double, 1>{surface.roughness});
        material::Set(result, materialTemplate, shader::MaterialSemantic::NormalScale,
                      std::array<double, 1>{surface.normalScale});
        material::Set(result, materialTemplate, shader::MaterialSemantic::OcclusionStrength,
                      std::array<double, 1>{surface.occlusionStrength});
        material::Set(
            result, materialTemplate, shader::MaterialSemantic::Emission,
            std::array<double, 3>{surface.emission[0], surface.emission[1], surface.emission[2]});
        if (surface.image >= 0)
            material::SetResource(result, materialTemplate, shader::MaterialSemantic::BaseMap,
                                  instance.model + "/Image/" + std::to_string(surface.image));
        for (const auto& [property, image] :
             std::array<std::pair<shader::MaterialSemantic, int>, 4>{
                 {{shader::MaterialSemantic::MetallicRoughnessMap, surface.metallicRoughnessImage},
                  {shader::MaterialSemantic::NormalMap, surface.normalImage},
                  {shader::MaterialSemantic::OcclusionMap, surface.occlusionImage},
                  {shader::MaterialSemantic::EmissionMap, surface.emissionImage}}})
            if (image >= 0)
                material::SetResource(result, materialTemplate, property,
                                      instance.model + "/Image/" + std::to_string(image));
        return {package, std::move(result)};
    }
    struct SceneBindings
    {
        std::optional<shader::BindingHandle> view, object, lighting, shadow;
        std::optional<shader::BindingHandle> environment, brdf, shadowMap;
    };
    std::map<std::pair<const shader::Package*, uint32_t>, SceneBindings> sceneBindings;
    const SceneBindings& Bindings(const std::shared_ptr<const shader::Package>& selected,
                                  shader::PassRole role)
    {
        const auto pass = selected->ResolvePass(role);
        const auto key = std::pair{selected.get(), pass.index};
        if (const auto old = sceneBindings.find(key); old != sceneBindings.end())
            return old->second;
        using namespace shader::generated;
        SceneBindings bindings;
        bindings.view = selected->SchemaBinding(pass, SceneViewDataContract);
        bindings.object = selected->SchemaBinding(pass, SceneObjectDataContract);
        bindings.lighting = selected->SchemaBinding(pass, SceneLightingDataContract);
        bindings.shadow = selected->SchemaBinding(pass, SceneShadowDataContract);
        const auto& layout = selected->interfaces.at(pass.index);
        for (uint32_t i = 0; i < layout.blocks.size(); ++i)
        {
            if (layout.blocks[i].scope == shader::Scope::Material)
                continue;
            const shader::BindingHandle handle{pass, i, true};
            if (bindings.view != handle && bindings.object != handle &&
                bindings.lighting != handle && bindings.shadow != handle)
                throw std::invalid_argument("Unsupported required scene parameter schema: " +
                                            layout.blocks[i].schemaId);
        }
        using Semantic = shader::Property::BindingSemantic;
        bindings.environment = selected->SemanticBinding(pass, Semantic::EnvironmentAtlas);
        bindings.brdf = selected->SemanticBinding(pass, Semantic::BrdfLut);
        bindings.shadowMap = selected->SemanticBinding(pass, Semantic::MainLightShadow);
        for (const auto& resource : layout.resources)
            if (resource.scope != shader::Scope::Material &&
                resource.property.binding == Semantic::None)
                throw std::invalid_argument(
                    "Scene resource requires an explicit standard binding semantic");
        return sceneBindings.emplace(key, std::move(bindings)).first->second;
    }
    template <class Data>
    rhi::UploadArena::Slice ParameterBuffer(const Data& data, rhi::UploadPool::Batch& uploads)
    {
        const auto bytes = shader::generated::Pack(data);
        return uploads.Write(bytes);
    }
    struct FrameBuffers
    {
        rhi::UploadArena::Slice view, lighting, shadow;
    };
    FrameBuffers Parameters(rhi::UploadPool::Batch& uploads)
    {
        using namespace shader::generated;
        SceneViewData view;
        view.View = snapshot.view.values;
        view.Projection = snapshot.projection.values;
        view.ViewProjection = snapshot.viewProjection.values;
        view.CameraPosition = Vector(snapshot.cameraPosition, 1);
        SceneLightingData lighting;
        lighting.MainLightDirection = Vector(snapshot.light.direction);
        lighting.MainLightColor = Vector(snapshot.light.color, snapshot.light.intensity);
        lighting.AmbientColor = Vector(snapshot.ambient);
        lighting.EnvironmentParameters = {
            snapshot.environmentIntensity,
            static_cast<float>(snapshot.environmentRotation * 3.141592653589793 / 180), 0, 0};
        SceneShadowData shadow;
        shadow.MainLightShadowMatrix = shadowMatrix.values;
        shadow.MainLightShadowParameters = {
            1.f / snapshot.light.shadowResolution, snapshot.light.shadowBias,
            snapshot.light.shadowNormalBias,
            snapshot.light.shadows && snapshot.light.intensity > 0 &&
                    !snapshot.shadowCasters.empty()
                ? 1.f
                : 0.f};
        return {ParameterBuffer(view, uploads), ParameterBuffer(lighting, uploads),
                ParameterBuffer(shadow, uploads)};
    }
};
SceneRenderer::SceneRenderer(rhi::Runtime& runtime, shader::Target target,
                             const std::filesystem::path& project,
                             const std::filesystem::path& engine)
    : impl_(std::make_unique<Impl>(runtime, target, project, engine))
{
}
SceneRenderer::~SceneRenderer() = default;
scene::Scene& SceneRenderer::Document()
{
    return impl_->document;
}
void SceneRenderer::ValidatePipeline(PipelineType pipeline)
{
    auto& self = *impl_;
    if (pipeline == PipelineType::Forward)
        return;
    if (pipeline != PipelineType::Deferred && pipeline != PipelineType::RayTracing)
        throw std::invalid_argument("Unknown rendering pipeline");
    if (pipeline == PipelineType::RayTracing &&
        (!self.runtime.SupportsAccelerationStructures() || !self.runtime.SupportsRayQuery() ||
         !self.runtime.SupportsRayTracingPipeline()))
        throw std::invalid_argument(
            "Ray Tracing requires acceleration structures, Ray Query and TraceRays");
    self.EnsureModels();
    scene::Extractor extractor;
    const auto snapshot = extractor.Extract(self.document, 1, self.models);
    for (const auto& instance : snapshot.allInstances)
    {
        const auto& model = self.models.at(instance.model);
        const auto mesh = std::ranges::find(model.meshes, instance.mesh, &assets::Mesh::id);
        const auto material = self.MaterialFor(instance, model.materials.at(mesh->material));
        const auto policy = material::ResolvePolicy(material.package->definition, material.asset);
        if (policy.surface == shader::SurfaceType::Transparent)
            continue;
        if (!material.package->HasRole(shader::PassRole::GBuffer))
            throw std::invalid_argument("Deferred rendering requires a GBuffer role in " +
                                        material.package->definition.name);
        if (pipeline == PipelineType::RayTracing)
        {
            if (policy.alphaClip)
                throw std::invalid_argument("Ray Tracing currently supports opaque geometry; "
                                            "masked materials need any-hit support");
            if (material.package->definition.assetId != builtin::Lit &&
                material.package->definition.assetId != "9120dfaac72c5c1c88f3a41e3ac22447")
                throw std::invalid_argument(
                    "Ray reflections currently support built-in Lit/Unlit materials");
        }
    }
}
const scene::Snapshot& SceneRenderer::LastSnapshot() const
{
    return impl_->snapshot;
}
const std::map<std::string, assets::Model>& SceneRenderer::Models() const
{
    return impl_->models;
}
rhi::UploadPool::Statistics SceneRenderer::UploadStatistics() const
{
    return impl_->uploadPool.Stats();
}
SceneReadback SceneRenderer::Render(std::shared_ptr<rhi::Texture> target, rhi::State initial,
                                    rhi::Swapchain* swapchain, bool readback,
                                    const std::filesystem::path& dump, SceneRenderOptions options)
{
    auto& self = *impl_;
    const bool rayTracing = options.pipeline == PipelineType::RayTracing;
    if (options.pipeline != PipelineType::Forward && options.pipeline != PipelineType::Deferred &&
        !rayTracing)
        throw std::invalid_argument("Unknown or unavailable scene render pipeline");
    const bool deferred = options.pipeline != PipelineType::Forward;
    auto uploads = self.uploadPool.Begin();
    self.EnsureModels();
    self.snapshot = self.extractor.Extract(
        self.document, float(target->desc.width) / float(target->desc.height), self.models);
    if (options.shadowResolution)
    {
        const auto size = options.shadowResolution;
        if (size < 128 || size > 2048 || (size & (size - 1)))
            throw std::invalid_argument("Invalid render shadow resolution");
        self.snapshot.light.shadowResolution = size;
    }
    self.UpdateEnvironment();
    self.shadowMatrix = FitDirectionalShadow(self.snapshot);
    const bool reflections =
        rayTracing && options.reflections &&
        (options.view == SceneView::Final || options.view == SceneView::Hdr ||
         options.view == SceneView::ReflectionRaw || options.view == SceneView::ReflectionHistory);
    if (!reflections)
        self.reflectionViews.clear();
    if ((options.view == SceneView::ReflectionRaw ||
         options.view == SceneView::ReflectionHistory) &&
        !reflections)
        throw std::invalid_argument(
            "Reflection texture views require the Ray Tracing pipeline and reflections enabled");
    std::vector<assets::Vertex> rayVertices;
    std::vector<uint32_t> rayIndices;
    std::vector<RayMeshData> rayMeshesData;
    std::vector<RayInstanceData> rayInstanceData;
    std::vector<RayMaterialData> rayMaterials;
    std::map<std::string, uint32_t> rayMeshIndices;
    std::vector<AccelerationScene::Instance> rayInstances;
    if (rayTracing)
    {
        self.EnsureRayQueryProgram();
        std::vector<AccelerationScene::Mesh> rayMeshes;
        for (const auto& instance : self.snapshot.allInstances)
        {
            const auto& model = self.models.at(instance.model);
            const auto mesh = std::ranges::find(model.meshes, instance.mesh, &assets::Mesh::id);
            if (mesh == model.meshes.end())
                throw std::logic_error("Ray Tracing snapshot references a missing mesh");
            const auto material = self.MaterialFor(instance, model.materials.at(mesh->material));
            const auto policy =
                material::ResolvePolicy(material.package->definition, material.asset);
            if (policy.surface == shader::SurfaceType::Transparent)
                continue;
            if (policy.alphaClip)
                throw std::invalid_argument("Ray Tracing currently supports opaque geometry; "
                                            "masked materials require any-hit alpha evaluation");
            const auto key = model.id + "/" + mesh->id;
            const auto& geometry = self.geometry.at(key);
            rayMeshes.push_back({key, geometry.vertices, geometry.indices,
                                 static_cast<uint32_t>(mesh->vertices.size()),
                                 static_cast<uint32_t>(mesh->indices.size()),
                                 sizeof(assets::Vertex)});
            AccelerationScene::Instance rayInstance{
                key, {}, static_cast<uint32_t>(rayInstances.size()), 2};
            std::copy_n(instance.world.values.begin(), 12, rayInstance.transform.begin());
            if (material.package->HasRole(shader::PassRole::ShadowCaster))
            {
                const auto& pass = material.package->GetPass(
                    material.package->ResolvePass(shader::PassRole::ShadowCaster));
                if (material::PassEnabled(material.package->definition, pass, material.asset))
                    rayInstance.mask |= 1;
            }
            rayInstances.push_back(std::move(rayInstance));
            if (reflections)
            {
                if (!rayMeshIndices.contains(key))
                {
                    rayMeshIndices.emplace(key, static_cast<uint32_t>(rayMeshesData.size()));
                    rayMeshesData.push_back({static_cast<uint32_t>(rayVertices.size()),
                                             static_cast<uint32_t>(rayIndices.size())});
                    rayVertices.insert(rayVertices.end(), mesh->vertices.begin(),
                                       mesh->vertices.end());
                    rayIndices.insert(rayIndices.end(), mesh->indices.begin(), mesh->indices.end());
                }
                rayInstanceData.push_back(
                    {rayMeshIndices.at(key), static_cast<uint32_t>(rayMaterials.size())});
                rayMaterials.push_back(self.RayMaterial(material));
            }
        }
        self.accelerationScene.EnsureMeshes(rayMeshes);
        if (reflections)
        {
            self.EnsureReflectionPrograms();
            const auto cache = [&](auto& data, size_t index)
            {
                if (data.empty())
                    data.resize(1);
                const auto bytes = std::as_bytes(std::span{data});
                if (self.raySurfaceBuffers[index] &&
                    std::ranges::equal(bytes, self.raySurfaceBytes[index]))
                    return;
                self.raySurfaceBytes[index].assign(bytes.begin(), bytes.end());
                self.raySurfaceBuffers[index] = self.Upload(
                    bytes, "RayTracing.Surface" + std::to_string(index), rhi::State::ShaderRead);
                ++self.raySurfaceVersion;
            };
            cache(rayVertices, 0);
            cache(rayIndices, 1);
            cache(rayMeshesData, 2);
            cache(rayInstanceData, 3);
            cache(rayMaterials, 4);
            self.RayTile("white", false);
            if (self.rayAtlasDirty)
            {
                self.rayAtlasView = self.UploadFloat("RayTracing.MaterialAtlas", self.rayAtlas);
                self.rayAtlasDirty = false;
                ++self.rayAtlasVersion;
            }
        }
    }
    rendergraph::Graph graph;
    if (target->desc.format != rhi::Format::Rgba8Srgb &&
        target->desc.format != rhi::Format::Bgra8Srgb)
        throw std::invalid_argument("Scene display target requires an sRGB format");
    // Swapchain owns its views and clears them before resize; only keep offscreen targets here.
    if (!swapchain)
        self.offscreenDisplayView = self.runtime.CreateView(target);
    graph.ImportTexture("DisplayColor", target, {initial, initial != rhi::State::Undefined});
    graph.CreateTexture("CameraColor", {target->desc.width, target->desc.height,
                                        rhi::Format::Rgba32Float, false, true});
    auto cameraDepthFormat = rhi::Format::D32;
    RenderFrame frame;
    const auto scan = [&](const std::vector<scene::Instance>& instances, shader::PassRole role,
                          rhi::Format& format)
    {
        for (const auto& instance : instances)
        {
            const auto& model = self.models.at(instance.model);
            const auto mesh = std::ranges::find(model.meshes, instance.mesh, &assets::Mesh::id);
            if (mesh == model.meshes.end())
                throw std::logic_error("Snapshot references missing mesh");
            const auto material = self.MaterialFor(instance, model.materials.at(mesh->material));
            const auto policy =
                material::ResolvePolicy(material.package->definition, material.asset);
            if (role == shader::PassRole::GBuffer &&
                policy.surface == shader::SurfaceType::Transparent)
                continue;
            if (role == shader::PassRole::Forward && deferred &&
                policy.surface != shader::SurfaceType::Transparent)
                continue;
            if (role == shader::PassRole::ShadowCaster && !material.package->HasRole(role))
                continue;
            if (role == shader::PassRole::GBuffer && !material.package->HasRole(role))
                throw std::invalid_argument("Deferred pipeline requires a GBuffer role in shader " +
                                            material.package->definition.name);
            const auto& pass = material.package->GetPass(material.package->ResolvePass(role));
            if (material::PassEnabled(material.package->definition, pass, material.asset) &&
                material::ResolveState(material.package->definition, pass, material.asset)
                    .stencil.enabled)
                format = rhi::Format::D32S8;
        }
    };
    scan(self.snapshot.instances, deferred ? shader::PassRole::GBuffer : shader::PassRole::Forward,
         cameraDepthFormat);
    if (deferred)
        scan(self.snapshot.instances, shader::PassRole::Forward, cameraDepthFormat);
    scan(self.snapshot.shadowCasters, shader::PassRole::ShadowCaster, frame.shadowDepthFormat);
    graph.CreateTexture("CameraDepth",
                        {target->desc.width, target->desc.height, cameraDepthFormat, false, true});
    frame.clearColor = {.02f, .02f, .02f, 1};
    frame.shadowResolution =
        self.snapshot.shadowCasters.empty() ? 1 : self.snapshot.light.shadowResolution;
    const auto frameConstants = self.Parameters(uploads);
    std::map<std::string, rhi::UploadArena::Slice> objectConstants;
    std::map<const rhi::Object*, std::string> imported;
    const auto importBuffer =
        [&](const std::shared_ptr<rhi::Buffer>& buffer, const std::string& name, rhi::State state)
    {
        if (const auto found = imported.find(buffer.get()); found != imported.end())
            return found->second;
        graph.ImportBuffer(name, buffer, {state, true});
        imported.emplace(buffer.get(), name);
        return name;
    };
    const auto importTexture = [&](const std::shared_ptr<rhi::TextureView>& view)
    {
        if (const auto found = imported.find(view->texture.get()); found != imported.end())
            return found->second;
        const auto name = view->texture->desc.name;
        graph.ImportTexture(name, view->texture, {rhi::State::Sampled, true});
        imported.emplace(view->texture.get(), name);
        return name;
    };
    std::optional<AccelerationScene::Prepared> preparedAcceleration;
    std::string reflectionSignature;
    Impl::ReflectionHistory* reflectionHistory = nullptr;
    uint32_t reflectionOutput = 0, reflectionSamples = 0, reflectionSequence = 0;
    if (rayTracing)
    {
        preparedAcceleration = self.accelerationScene.Build(graph, std::move(rayInstances));
        frame.rayTracing.scene = preparedAcceleration->topLevel;
        frame.rayTracing.storage = "RayTracing.SceneAS";
        frame.rayTracing.shadowPipeline = self.rayQueryShadow;
        RayQueryParameters parameters;
        const auto inverse = math::Inverse(self.snapshot.viewProjection);
        parameters.inverseViewProjection = inverse.values;
        parameters.directionToLight = Vector(self.snapshot.light.direction);
        parameters.shadowOptions = {
            .002f, .001f, 10000,
            self.snapshot.light.shadows && self.snapshot.light.intensity > 0 ? 1.f : 0.f};
        const auto slice = uploads.Write(std::as_bytes(std::span{&parameters, size_t{1}}));
        const auto name =
            importBuffer(slice.buffer, "RayTracing.Parameters", rhi::State::HostWrite);
        frame.rayTracing.parameters = {
            3, name, rendergraph::Use::Uniform, {slice.offset, slice.size}};
    }
    if (reflections)
    {
        ReflectionParameters parameters;
        parameters.inverseViewProjection = math::Inverse(self.snapshot.viewProjection).values;
        parameters.cameraPosition = Vector(self.snapshot.cameraPosition);
        parameters.directionToLight = Vector(self.snapshot.light.direction);
        parameters.lightColor = Vector(self.snapshot.light.color, self.snapshot.light.intensity);
        parameters.environment = {
            self.snapshot.environmentIntensity,
            self.snapshot.environmentRotation * float(3.141592653589793 / 180), 0, 0};
        parameters.options[2] = self.snapshot.light.shadows ? 1.f : 0.f;
        parameters.options[3] = static_cast<float>(MaxReflectionSamples);
        const auto append = [&](const auto& value)
        {
            const auto bytes = std::as_bytes(std::span{&value, size_t{1}});
            reflectionSignature.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        };
        append(parameters);
        append(target->desc.width);
        append(target->desc.height);
        append(self.raySurfaceVersion);
        append(self.rayAtlasVersion);
        append(self.environmentAmbient);
        reflectionSignature += self.environmentKey;
        for (const auto& instance : self.snapshot.allInstances)
        {
            reflectionSignature += instance.id;
            append(instance.world.values);
        }
        const auto& viewId = self.document.activeCamera;
        if (!self.reflectionViews.contains(viewId) && self.reflectionViews.size() >= 4)
            self.reflectionViews.erase(self.reflectionViews.begin());
        auto& history = self.reflectionViews[viewId];
        reflectionHistory = &history;
        const bool accumulate =
            options.reflectionHistory && options.view != SceneView::ReflectionRaw;
        const bool valid =
            accumulate && history.signature == reflectionSignature && history.samples > 0;
        reflectionSamples = valid ? history.samples : 0;
        reflectionSequence = valid ? history.sequence : 0;
        parameters.options[0] = static_cast<float>(reflectionSequence);
        parameters.options[1] = static_cast<float>(reflectionSamples);
        reflectionOutput = 1 - history.index;
        auto& output = history.radiance[reflectionOutput];
        if (!output || output->desc.width != target->desc.width ||
            output->desc.height != target->desc.height)
        {
            output = self.runtime.CreateTexture({target->desc.width, target->desc.height,
                                                 rhi::Format::Rgba32Float, true, false,
                                                 "RayTracing.History." + viewId});
            history.initialized[reflectionOutput] = false;
        }
        const auto name = "RayTracing.History." + viewId;
        graph.ImportTexture(
            name, output,
            {history.initialized[reflectionOutput] ? rhi::State::Sampled : rhi::State::Undefined,
             history.initialized[reflectionOutput], history.completion});
        frame.rayTracing.historyRadiance = name;
        if (valid)
        {
            graph.ImportTexture("RayTracing.Previous." + viewId, history.radiance[history.index],
                                {rhi::State::Sampled, true, history.completion});
            frame.rayTracing.previousRadiance = "RayTracing.Previous." + viewId;
        }
        const auto slice = uploads.Write(std::as_bytes(std::span{&parameters, size_t{1}}));
        frame.rayTracing.reflectionParameters = {
            4,
            importBuffer(slice.buffer, "RayTracing.ReflectionParameters", rhi::State::HostWrite),
            rendergraph::Use::Uniform,
            {slice.offset, slice.size}};
        frame.rayTracing.reflectionTable = self.reflectionTable;
        frame.rayTracing.historyPipeline = self.reflectionHistory;
        frame.rayTracing.combinePipeline = self.reflectionCombine;
        frame.rayTracing.sampler = self.sampler;
        constexpr std::array<uint32_t, 5> strides{32, 4, 16, 16, 80};
        for (uint32_t i = 0; i < 5; ++i)
            frame.rayTracing.surfaces.push_back(
                {i + 5,
                 importBuffer(self.raySurfaceBuffers[i], "RayTracing.Surface" + std::to_string(i),
                              rhi::State::ShaderRead),
                 rendergraph::Use::ShaderRead,
                 {},
                 strides[i]});
        frame.rayTracing.reflectionTextures = {{10, importTexture(self.rayAtlasView)},
                                               {13, importTexture(self.environmentAtlas)},
                                               {15, importTexture(self.brdfLut)}};
    }
    const material::ResourceResolver resolve =
        [&](const shader::Property& property, const std::string& reference)
    {
        rhi::Binding binding{};
        if (property.type.resource == rhi::BindingKind::Sampler)
        {
            if (reference != "point_clamp")
                throw std::invalid_argument("Unsupported sampler preset");
            binding.sampler = self.sampler;
        }
        else
        {
            const auto key = reference + (property.srgb ? "/Srgb" : "/Linear");
            if (!self.textures.contains(key))
            {
                if (const auto image = self.modelImages.find(reference);
                    image != self.modelImages.end())
                    self.UploadImage(key, *image->second, property.srgb);
                else
                    self.UploadImage(key,
                                     assets::DecodeImage(
                                         assets::detail::Read(self.database.Get(reference).path)),
                                     property.srgb);
            }
            binding.view = self.textures.at(key);
        }
        return binding;
    };
    const auto prepareDraw = [&](const scene::Instance& instance, shader::PassRole requestedRole)
    {
        const auto& model = self.models.at(instance.model);
        const auto mesh = std::ranges::find(model.meshes, instance.mesh, &assets::Mesh::id);
        if (mesh == model.meshes.end())
            throw std::logic_error("Snapshot references missing mesh");
        const auto& surface = model.materials.at(mesh->material);
        const auto material = self.MaterialFor(instance, surface);
        const auto policy = material::ResolvePolicy(material.package->definition, material.asset);
        auto role = requestedRole;
        if (role == shader::PassRole::GBuffer && policy.surface == shader::SurfaceType::Transparent)
            role = shader::PassRole::Forward;
        if (role == shader::PassRole::ShadowCaster && !material.package->HasRole(role))
            return;
        if (role == shader::PassRole::GBuffer && !material.package->HasRole(role))
            throw std::invalid_argument("Deferred pipeline requires a GBuffer role in shader " +
                                        material.package->definition.name);
        const auto& pass = material.package->GetPass(material.package->ResolvePass(role));
        if (!material::PassEnabled(material.package->definition, pass, material.asset))
            return;
        const bool transparent = policy.surface == shader::SurfaceType::Transparent;
        material::Preparation prepare;
        prepare.pass = role;
        prepare.colorFormats =
            role == shader::PassRole::GBuffer
                ? std::vector<rhi::Format>{rhi::Format::Rgba32Float, rhi::Format::Rgba32Float,
                                           rhi::Format::Rgba32Float}
                : std::vector<rhi::Format>{rhi::Format::Rgba32Float};
        prepare.depthStencilFormat =
            role == shader::PassRole::ShadowCaster ? frame.shadowDepthFormat : cameraDepthFormat;
        const auto& w = instance.world;
        const math::Vec3 x{w.At(0, 0), w.At(1, 0), w.At(2, 0)},
            y{w.At(0, 1), w.At(1, 1), w.At(2, 1)}, z{w.At(0, 2), w.At(1, 2), w.At(2, 2)};
        prepare.mirrored = math::Dot(math::Cross(x, y), z) < 0;
        prepare.vertexStride = sizeof(assets::Vertex);
        prepare.attributes = {{"POSITION", 0, rhi::VertexFormat::Float3, 0},
                              {"NORMAL", 1, rhi::VertexFormat::Float3, 12},
                              {"TEXCOORD", 2, rhi::VertexFormat::Float2, 24}};
        const auto& bindings = self.Bindings(material.package, role);
        const auto bind = [&](const std::optional<shader::BindingHandle>& handle,
                              const rhi::UploadArena::Slice& slice)
        {
            if (handle)
                prepare.bindings[*handle] = {0, {}, {}, slice.buffer, slice.offset, slice.size};
        };
        bind(bindings.view, frameConstants.view);
        bind(bindings.lighting, frameConstants.lighting);
        bind(bindings.shadow, frameConstants.shadow);
        if (bindings.object)
        {
            if (!objectConstants.contains(instance.id))
            {
                shader::generated::SceneObjectData object;
                object.ObjectToWorld = instance.world.values;
                object.NormalToWorld = instance.normal.values;
                object.PreviousObjectToWorld = instance.previousWorld.values;
                objectConstants.emplace(instance.id, self.ParameterBuffer(object, uploads));
            }
            bind(bindings.object, objectConstants.at(instance.id));
        }
        if (bindings.environment)
            prepare.bindings[*bindings.environment] = {0, self.environmentAtlas};
        if (bindings.brdf)
            prepare.bindings[*bindings.brdf] = {0, self.brdfLut};
        if (bindings.shadowMap)
            prepare.deferredResources.insert(*bindings.shadowMap);
        auto prepared = self.materials.Prepare(material.package, material.asset, prepare, resolve);
        self.generations[material.package.get()] = prepared.generation;
        DrawPacket draw;
        draw.pipeline = prepared.pipeline;
        draw.stencilReference = prepared.stencilReference;
        draw.queue = policy.queue;
        draw.kind = DrawKind::Indexed;
        draw.count = static_cast<uint32_t>(mesh->indices.size());
        const auto center = (instance.bounds.minimum + instance.bounds.maximum) * .5f;
        draw.distance = std::max(0.f, -math::Point(self.snapshot.view, center).z);
        const auto& geometry = self.geometry.at(model.id + "/" + mesh->id);
        draw.vertices =
            importBuffer(geometry.vertices, geometry.vertices->desc.name, rhi::State::Vertex);
        draw.indices =
            importBuffer(geometry.indices, geometry.indices->desc.name, rhi::State::Index);
        for (const auto& binding : prepared.resources)
        {
            const auto entry = std::ranges::find_if(
                prepared.pipeline->desc.layout->entries, [&](const auto& value)
                { return value.slot == binding.slot && value.space == binding.space; });
            if (entry == prepared.pipeline->desc.layout->entries.end())
                throw std::logic_error("Material binding layout mismatch");
            if (binding.buffer)
            {
                const auto name = importBuffer(
                    binding.buffer,
                    instance.id + "/" + pass.name + "/" + std::to_string(binding.space) + "/" +
                        std::to_string(binding.slot),
                    binding.buffer->desc.memory == rhi::Memory::Upload ? rhi::State::HostWrite
                                                                       : rhi::State::ShaderRead);
                draw.buffers.push_back({binding.slot,
                                        name,
                                        entry->kind == rhi::BindingKind::ConstantBuffer
                                            ? rendergraph::Use::Uniform
                                            : rendergraph::Use::ShaderRead,
                                        {binding.offset, binding.size},
                                        binding.stride,
                                        binding.space});
            }
            else if (binding.view)
                draw.textures.push_back({binding.slot, importTexture(binding.view), binding.space});
            else if (binding.sampler)
                draw.samplers.push_back({binding.slot, binding.sampler, binding.space});
        }
        if (bindings.shadowMap)
        {
            const auto& resource = material.package->GetResource(*bindings.shadowMap);
            draw.textures.push_back({resource.slot, "MainLightShadow", uint32_t(resource.scope)});
        }
        if (role == shader::PassRole::ShadowCaster)
            frame.shadowCasters.push_back(std::move(draw));
        else if (role == shader::PassRole::GBuffer)
            frame.gbuffer.push_back(std::move(draw));
        else if (transparent)
            frame.transparent.push_back(std::move(draw));
        else
            frame.opaque.push_back(std::move(draw));
    };
    for (const auto& instance : self.snapshot.shadowCasters)
        prepareDraw(instance, shader::PassRole::ShadowCaster);
    for (const auto& instance : self.snapshot.instances)
        prepareDraw(instance, deferred ? shader::PassRole::GBuffer : shader::PassRole::Forward);
    if (deferred)
    {
        material::Preparation prepare;
        prepare.pass = "DeferredLighting";
        prepare.colorFormats = {rhi::Format::Rgba32Float};
        prepare.vertexStride = 8;
        prepare.attributes = {{"POSITION", 0, rhi::VertexFormat::Float2, 0}};
        const auto pass = self.deferredLighting->ResolvePass(prepare.pass);
        const auto bind =
            [&](const shader::BlockContract& contract, const rhi::UploadArena::Slice& slice)
        {
            const auto handle = self.deferredLighting->SchemaBinding(pass, contract);
            if (!handle)
                throw std::logic_error(
                    "Deferred lighting shader is missing a scene parameter block");
            rhi::Binding value{};
            value.buffer = slice.buffer;
            value.offset = slice.offset;
            value.size = slice.size;
            prepare.bindings[*handle] = std::move(value);
        };
        using namespace shader::generated;
        bind(SceneViewDataContract, frameConstants.view);
        bind(SceneLightingDataContract, frameConstants.lighting);
        bind(SceneShadowDataContract, frameConstants.shadow);
        const auto inverse = math::Inverse(self.snapshot.viewProjection);
        const std::vector<double> inverseValues(inverse.values.begin(), inverse.values.end());
        prepare.constants[self.deferredLighting->BlockBinding(pass, "DeferredLightingParameters")] =
            {{"InverseViewProjection", {"float4x4", 1, inverseValues}},
             {"RayTracedShadows", {"uint", 1, {rayTracing ? 1.0 : 0.0}}}};
        for (const auto name :
             {"GBufferAlbedoMetallic", "GBufferNormalRoughness", "GBufferEmissionOcclusion",
              "CameraDepth", "EnvironmentAtlas", "BrdfLut", "MainLightShadow", "ShadowVisibility"})
            prepare.deferredResources.insert(self.deferredLighting->ResourceBinding(pass, name));
        auto prepared = self.materials.Prepare(
            self.deferredLighting,
            material::Create(shader::MaterialTemplate(self.deferredLighting)), prepare);
        self.generations[self.deferredLighting.get()] = prepared.generation;
        auto& draw = frame.deferredLighting;
        draw.pipeline = prepared.pipeline;
        draw.kind = DrawKind::Vertices;
        draw.count = 3;
        draw.vertices = importBuffer(self.fullscreen, "Display.Triangle", rhi::State::Vertex);
        for (const auto& binding : prepared.resources)
        {
            const auto entry = std::ranges::find_if(
                prepared.pipeline->desc.layout->entries, [&](const auto& value)
                { return value.slot == binding.slot && value.space == binding.space; });
            if (entry == prepared.pipeline->desc.layout->entries.end() || !binding.buffer)
                throw std::logic_error("Deferred lighting binding layout mismatch");
            const auto name =
                importBuffer(binding.buffer,
                             "Deferred.Lighting.Constants." + std::to_string(binding.space) + "." +
                                 std::to_string(binding.slot),
                             rhi::State::HostWrite);
            draw.buffers.push_back({binding.slot,
                                    name,
                                    rendergraph::Use::Uniform,
                                    {binding.offset, binding.size},
                                    binding.stride,
                                    binding.space});
        }
        const std::map<std::string, std::string> textureNames{
            {"GBufferAlbedoMetallic", "GBuffer.AlbedoMetallic"},
            {"GBufferNormalRoughness", "GBuffer.NormalRoughness"},
            {"GBufferEmissionOcclusion", "GBuffer.EmissionOcclusion"},
            {"CameraDepth", "CameraDepth"},
            {"EnvironmentAtlas", importTexture(self.environmentAtlas)},
            {"BrdfLut", importTexture(self.brdfLut)},
            {"MainLightShadow", "MainLightShadow"},
            {"ShadowVisibility", rayTracing ? "RayTracing.ShadowVisibility"
                                            : importTexture(self.textures.at("white/Linear"))}};
        for (const auto& handle : prepared.deferredResources)
        {
            const auto& resource = self.deferredLighting->GetResource(handle);
            const auto found = textureNames.find(resource.property.symbol);
            if (found == textureNames.end())
                throw std::logic_error("Unknown deferred lighting resource");
            draw.textures.push_back(
                {resource.slot, found->second, static_cast<uint32_t>(resource.scope)});
        }
    }
    if (rayTracing)
        self.rayTracingPipeline.Build(graph, frame);
    else if (deferred)
        self.deferredPipeline.Build(graph, frame);
    else
        self.forwardPipeline.Build(graph, frame);
    if (reflections)
        graph.Export(graph.GetTexture(frame.rayTracing.historyRadiance), rhi::State::Sampled);
    std::string displaySource = "CameraColor";
    if (options.view == SceneView::ReflectionRaw)
        displaySource = "RayTracing.RawRadiance";
    else if (options.view == SceneView::ReflectionHistory)
        displaySource = frame.rayTracing.historyRadiance;
    if (options.view == SceneView::Shadow)
        displaySource = rayTracing ? "RayTracing.ShadowVisibility" : "MainLightShadow";
    else if (options.view == SceneView::Environment)
        displaySource = importTexture(self.environmentAtlas);
    else if (options.view == SceneView::Brdf)
        displaySource = importTexture(self.brdfLut);
    material::Preparation displayPreparation;
    displayPreparation.pass = "Display";
    displayPreparation.colorFormats = {target->desc.format};
    displayPreparation.vertexStride = 8;
    displayPreparation.attributes = {{"POSITION", 0, rhi::VertexFormat::Float2, 0}};
    const auto displayHandle = self.toneMapping->ResolvePass(displayPreparation.pass);
    displayPreparation
        .constants[self.toneMapping->BlockBinding(displayHandle, "DisplayParameters")] = {
        {"Exposure", {"float", 1, {self.snapshot.exposure}}},
        {"RawView",
         {"uint",
          1,
          {options.view != SceneView::Final && options.view != SceneView::Environment ? 1.0
                                                                                      : 0.0}}},
        {"ViewportSize", {"float2", 1, {double(target->desc.width), double(target->desc.height)}}}};
    for (const auto& [handle, values] : displayPreparation.constants)
    {
        const auto slice =
            uploads.Write(material::Pack(self.toneMapping->GetBlock(handle), values));
        displayPreparation.bindings[handle] = {0, {}, {}, slice.buffer, slice.offset, slice.size};
    }
    displayPreparation.constants.clear();
    displayPreparation.deferredResources.insert(
        self.toneMapping->ResourceBinding(displayHandle, "HdrColor"));
    const auto display = self.materials.Prepare(
        self.toneMapping, material::Create(shader::MaterialTemplate(self.toneMapping)),
        displayPreparation);
    self.generations[self.toneMapping.get()] = display.generation;
    const auto fullscreen = importBuffer(self.fullscreen, "Display.Triangle", rhi::State::Vertex);
    std::vector<std::pair<rhi::Binding, std::string>> displayBuffers;
    for (const auto& binding : display.resources)
        if (binding.buffer)
            displayBuffers.emplace_back(
                binding, importBuffer(binding.buffer, "Display.Constants", rhi::State::HostWrite));
    auto displayPass = graph.AddRasterPass(
        options.view == SceneView::Final ? "Display.ToneMapping" : "Debug.TextureViewer");
    displayPass.ColorTarget("DisplayColor", rhi::LoadOp::Clear, {.02f, .02f, .02f, 1});
    const auto hdr = displayPass.ReadTexture(displaySource);
    const auto vertices = displayPass.ReadBuffer(fullscreen, rendergraph::Use::Vertex);
    std::vector<std::pair<rhi::Binding, rendergraph::BufferHandle>> constants;
    for (const auto& [binding, name] : displayBuffers)
        constants.emplace_back(binding, displayPass.ReadBuffer(name, rendergraph::Use::Uniform,
                                                               {binding.offset, binding.size}));
    const auto hdrBinding =
        *std::ranges::find_if(display.pipeline->desc.layout->entries, [](const auto& entry)
                              { return entry.kind == rhi::BindingKind::SampledTexture; });
    displayPass.Execute(
        [pipeline = display.pipeline, hdr, vertices, constants,
         hdrBinding](rendergraph::RasterContext& context)
        {
            context.Bind(pipeline);
            context.BindTexture(hdrBinding.slot, hdr, hdrBinding.space);
            for (const auto& [binding, handle] : constants)
                context.BindBuffer(binding.slot, handle, binding.offset, binding.size, 0,
                                   binding.space);
            context.Geometry(vertices);
            context.Draw(3);
        });
    SceneReadback result;
    if (readback)
    {
        const auto pitch = rhi::TextureRowPitch(target->desc.width);
        result.pixels = self.runtime.CreateBuffer(
            {uint64_t(pitch) * target->desc.height, rhi::Memory::Readback, "Scene.Readback"});
        auto buffer =
            graph.ImportBuffer("Scene.Readback", result.pixels, {rhi::State::CopyDest, false});
        auto pass = graph.AddCopyPass("Scene.Readback");
        const auto color = pass.ReadTexture("DisplayColor");
        buffer = pass.WriteBuffer(buffer);
        pass.Execute([buffer, color, pitch](rendergraph::CopyContext& context)
                     { context.CopyTexture(buffer, color, pitch, false); });
        graph.Export(buffer, rhi::State::HostRead);
    }
    graph.Export("DisplayColor", swapchain ? rhi::State::Present : rhi::State::CopySource);
    graph.Compile({.profile = options.profile});
    auto submitted = graph.Execute(self.pool, swapchain);
    if (reflectionHistory)
    {
        reflectionHistory->signature = std::move(reflectionSignature);
        reflectionHistory->index = reflectionOutput;
        reflectionHistory->initialized[reflectionOutput] = true;
        reflectionHistory->samples = std::min(reflectionSamples + 1, MaxReflectionSamples);
        reflectionHistory->sequence = std::min(reflectionSequence + 1, MaxReflectionSamples);
        reflectionHistory->completion = submitted.completion;
    }
    if (preparedAcceleration)
        self.accelerationScene.Commit(std::move(*preparedAcceleration), submitted.completion);
    uploads.Seal(submitted.completion);
    result.reflectionSamples = reflectionHistory ? reflectionHistory->samples : 0;
    result.reflectionHistoryReset = reflectionHistory && reflectionSamples == 0;
    result.completion = submitted.completion;
    result.profile = std::move(submitted.profile);
    result.compileNs = graph.GetPlan().compileNs;
    result.prepareNs = graph.GetPlan().prepareNs;
    result.recordNs = graph.GetPlan().recordNs;
    if (!dump.empty())
    {
        const auto json = graph.GetPlan().ToJson();
        WriteFileAtomically(dump, std::as_bytes(std::span{json.data(), json.size()}));
    }
    return result;
}
} // namespace renderlab::renderer
