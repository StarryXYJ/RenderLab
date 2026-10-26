#include "renderer/scene_renderer.hpp"
#include "material/render_state.hpp"

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
#include "shader/compiler.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <stdexcept>
#include <utility>

namespace renderlab::renderer
{
namespace
{
material::Value Matrix(const math::Mat4& value)
{
    return {"float4x4", 1, {value.values.begin(), value.values.end()}};
}
material::Value Vector(math::Vec3 value, float w = 0)
{
    return {"float4", 1, {value.x, value.y, value.z, w}};
}
} // namespace
struct SceneRenderer::Impl
{
    rhi::Runtime& runtime;
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
    std::map<const shader::Package*, std::shared_ptr<const material::GpuGeneration>> generations;
    std::shared_ptr<rhi::TextureView> environmentAtlas, brdfLut;
    std::string environmentKey;
    std::array<float, 3> environmentAmbient{};
    std::shared_ptr<const shader::Package> toneMapping;
    std::shared_ptr<rhi::Buffer> fullscreen;
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
    RenderPipeline pipeline =
        CreatePipeline(PipelineType::Forward)
            .AddFeature(std::make_shared<ShadowFeature>(), RenderStage::BeforeOpaque)
            .Create();
    math::Mat4 shadowMatrix = math::Mat4::Identity();
    scene::Extractor extractor;
    scene::Snapshot snapshot;
    Impl(rhi::Runtime& r, shader::Target target, const std::filesystem::path& manifest,
         const std::filesystem::path& engine)
        : runtime(r), project(project::Project::Open(manifest, engine)), database(project),
          document(scene::Scene::Load(database.Get(project.startScene).path)), materials(r, target),
          // Two 2048 shadow color/depth pairs plus 720p HDR/depth fit below the RHI budget.
          pool(r, 64, 224ull * 1024 * 1024)
    {
        shader::CompileOptions options;
        options.cacheDirectory = project.library / "Shaders";
        options.includeDirectories = {project.engine};
        const auto compiled =
            shader::CompileShader(project.ResolveEngine("RenderLab/Lit.shader"), options);
        if (!compiled)
            throw std::runtime_error(compiled.diagnostics);
        package = compiled.package;
        packages.emplace("RenderLab/Lit", package);
        const auto display =
            shader::CompileShader(project.ResolveEngine("RenderLab/ToneMapping.shader"), options);
        if (!display)
            throw std::runtime_error(display.diagnostics);
        toneMapping = display.package;
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
                                        rhi::State state)
    {
        auto staging = runtime.CreateBuffer({bytes.size(), rhi::Memory::Upload, name + ".Upload"});
        std::memcpy(staging->Map().data(), bytes.data(), bytes.size());
        auto destination = runtime.CreateBuffer({bytes.size(), rhi::Memory::Device, name});
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
                    geometry.emplace(name, Geometry{Upload(std::as_bytes(std::span{mesh.vertices}),
                                                           name + ".Vertices", rhi::State::Vertex),
                                                    Upload(std::as_bytes(std::span{mesh.indices}),
                                                           name + ".Indices", rhi::State::Index)});
                }
                const auto id = model.id;
                const auto& stored = models.emplace(id, std::move(model)).first->second;
                for (size_t i = 0; i < stored.images.size(); ++i)
                    modelImages.emplace(id + "/Image/" + std::to_string(i), &stored.images[i]);
            }
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
            auto selected = package;
            if (const auto shaderId = entry.dependencies.find("shader");
                shaderId != entry.dependencies.end())
            {
                if (!packages.contains(shaderId->second))
                {
                    const auto& shaderEntry = database.Get(shaderId->second);
                    if (shaderEntry.path.extension() != ".shader")
                        throw std::invalid_argument(
                            "Material shader dependency must reference .shader");
                    shader::CompileOptions options;
                    options.cacheDirectory = project.library / "Shaders";
                    options.includeDirectories = {project.engine, project.assets};
                    const auto build = shader::CompileShader(shaderEntry.path, options);
                    if (!build)
                        throw std::runtime_error(build.diagnostics);
                    const auto& actual =
                        build.package
                            ->Layout(
                                build.package
                                    ->GetPass(build.package->ResolvePass(shader::PassRole::Forward))
                                    .name)
                            .blocks;
                    const auto frame =
                        std::ranges::find(actual, std::string("Frame"), &shader::BlockLayout::name);
                    const auto& standard =
                        package
                            ->Layout(
                                package->GetPass(package->ResolvePass(shader::PassRole::Forward))
                                    .name)
                            .blocks;
                    const auto expected = std::ranges::find(standard, std::string("Frame"),
                                                            &shader::BlockLayout::name);
                    if (frame == actual.end() || frame->size != expected->size ||
                        frame->fields.size() != expected->fields.size())
                        throw std::invalid_argument(
                            "Scene shaders must use RenderLab/Scene.params Frame layout");
                    for (size_t i = 0; i < frame->fields.size(); ++i)
                        if (frame->fields[i].offset != expected->fields[i].offset ||
                            frame->fields[i].property.symbol !=
                                expected->fields[i].property.symbol ||
                            frame->fields[i].property.type != expected->fields[i].property.type)
                            throw std::invalid_argument(
                                "Scene shader global Frame layout mismatch");
                    packages.emplace(shaderId->second, build.package);
                }
                selected = packages.at(shaderId->second);
            }
            else
            {
                const auto name =
                    assets::detail::LoadJson(entry.path).at("shader").get<std::string>();
                if (name != "RenderLab/Lit" && name != "RenderLab/Unlit" &&
                    name != "RenderLab/BlinnPhong")
                    throw std::invalid_argument(
                        "Project shaders require a .meta shader dependency");
                if (!packages.contains(name))
                {
                    shader::CompileOptions options;
                    options.cacheDirectory = project.library / "Shaders";
                    options.includeDirectories = {project.engine};
                    const auto build =
                        shader::CompileShader(project.ResolveEngine(name + ".shader"), options);
                    if (!build)
                        throw std::runtime_error(build.diagnostics);
                    packages.emplace(name, build.package);
                }
                selected = packages.at(name);
            }
            MaterialInput input{selected,
                                material::Load(entry.path, shader::MaterialTemplate(selected))};
            materialAssets.insert_or_assign(instance.material, CachedMaterial{modified, input});
            return input;
        }
        auto result = material::Create(materialTemplate);
        material::Set(result, materialTemplate, "surface",
                      std::array<double, 1>{surface.alphaMode == "BLEND" ? 1.0 : 0.0});
        material::Set(result, materialTemplate, "alpha_clip",
                      std::array<double, 1>{surface.alphaMode == "MASK" ? 1.0 : 0.0});
        material::Set(result, materialTemplate, "cull",
                      std::array<double, 1>{surface.doubleSided ? 0.0 : 2.0});
        material::Set(result, materialTemplate, "alpha_cutoff",
                      std::array<double, 1>{surface.alphaCutoff});
        material::Set(result, materialTemplate, "base_color",
                      std::array<double, 4>{surface.color[0], surface.color[1], surface.color[2],
                                            surface.color[3]});
        material::Set(result, materialTemplate, "metallic",
                      std::array<double, 1>{surface.metallic});
        material::Set(result, materialTemplate, "roughness",
                      std::array<double, 1>{surface.roughness});
        material::Set(result, materialTemplate, "normal_scale",
                      std::array<double, 1>{surface.normalScale});
        material::Set(result, materialTemplate, "occlusion_strength",
                      std::array<double, 1>{surface.occlusionStrength});
        material::Set(
            result, materialTemplate, "emission",
            std::array<double, 3>{surface.emission[0], surface.emission[1], surface.emission[2]});
        if (surface.image >= 0)
            material::SetResource(result, materialTemplate, "base_map",
                                  instance.model + "/Image/" + std::to_string(surface.image));
        for (const auto& [property, image] : std::array<std::pair<const char*, int>, 4>{
                 {{"MetallicRoughnessMap", surface.metallicRoughnessImage},
                  {"NormalMap", surface.normalImage},
                  {"OcclusionMap", surface.occlusionImage},
                  {"EmissionMap", surface.emissionImage}}})
            if (image >= 0)
                material::SetResource(result, materialTemplate, property,
                                      instance.model + "/Image/" + std::to_string(image));
        return {package, std::move(result)};
    }
    std::shared_ptr<rhi::Buffer> FrameBuffer()
    {
        const auto& layout =
            package->Layout(package->GetPass(package->ResolvePass(shader::PassRole::Forward)).name);
        const auto& block =
            *std::ranges::find(layout.blocks, std::string("Frame"), &shader::BlockLayout::name);
        const auto bytes = material::Pack(
            block, {{"View", Matrix(snapshot.view)},
                    {"Projection", Matrix(snapshot.projection)},
                    {"ViewProjection", Matrix(snapshot.viewProjection)},
                    {"CameraPosition", Vector(snapshot.cameraPosition, 1)},
                    {"MainLightDirection", Vector(snapshot.light.direction)},
                    {"MainLightColor", Vector(snapshot.light.color, snapshot.light.intensity)},
                    {"AmbientColor", Vector(snapshot.ambient)},
                    {"MainLightShadowMatrix", Matrix(shadowMatrix)},
                    {"MainLightShadowParameters",
                     {"float4",
                      1,
                      {1.0 / snapshot.light.shadowResolution, snapshot.light.shadowBias,
                       snapshot.light.shadowNormalBias,
                       snapshot.light.shadows && snapshot.light.intensity > 0 &&
                               !snapshot.shadowCasters.empty()
                           ? 1.0
                           : 0.0}}},
                    {"EnvironmentParameters",
                     {"float4",
                      1,
                      {snapshot.environmentIntensity,
                       snapshot.environmentRotation * 3.141592653589793 / 180, 0, 0}}}});
        auto buffer = runtime.CreateBuffer({bytes.size(), rhi::Memory::Upload, "Frame.Constants"});
        std::memcpy(buffer->Map().data(), bytes.data(), bytes.size());
        return buffer;
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
const scene::Snapshot& SceneRenderer::LastSnapshot() const
{
    return impl_->snapshot;
}
const std::map<std::string, assets::Model>& SceneRenderer::Models() const
{
    return impl_->models;
}
SceneReadback SceneRenderer::Render(std::shared_ptr<rhi::Texture> target, rhi::State initial,
                                    rhi::Swapchain* swapchain, bool readback,
                                    const std::filesystem::path& dump, SceneRenderOptions options)
{
    auto& self = *impl_;
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
    rendergraph::Graph graph;
    if (target->desc.format != rhi::Format::Rgba8Srgb &&
        target->desc.format != rhi::Format::Bgra8Srgb)
        throw std::invalid_argument("Scene display target requires an sRGB format");
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
            if (role == shader::PassRole::ShadowCaster && !material.package->HasRole(role))
                continue;
            const auto& pass = material.package->GetPass(material.package->ResolvePass(role));
            if (material::PassEnabled(material.package->definition, pass, material.asset) &&
                material::ResolveState(material.package->definition, pass, material.asset)
                    .stencil.enabled)
                format = rhi::Format::D32S8;
        }
    };
    scan(self.snapshot.instances, shader::PassRole::Forward, cameraDepthFormat);
    scan(self.snapshot.shadowCasters, shader::PassRole::ShadowCaster, frame.shadowDepthFormat);
    graph.CreateTexture("CameraDepth",
                        {target->desc.width, target->desc.height, cameraDepthFormat, false, true});
    frame.clearColor = {.02f, .02f, .02f, 1};
    frame.shadowResolution =
        self.snapshot.shadowCasters.empty() ? 1 : self.snapshot.light.shadowResolution;
    const auto frameConstants = self.FrameBuffer();
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
    const auto prepareDraw = [&](const scene::Instance& instance, bool shadow)
    {
        const auto& model = self.models.at(instance.model);
        const auto mesh = std::ranges::find(model.meshes, instance.mesh, &assets::Mesh::id);
        if (mesh == model.meshes.end())
            throw std::logic_error("Snapshot references missing mesh");
        const auto& surface = model.materials.at(mesh->material);
        const auto material = self.MaterialFor(instance, surface);
        const auto role = shadow ? shader::PassRole::ShadowCaster : shader::PassRole::Forward;
        if (shadow && !material.package->HasRole(role))
            return;
        const auto& pass = material.package->GetPass(material.package->ResolvePass(role));
        if (!material::PassEnabled(material.package->definition, pass, material.asset))
            return;
        const auto policy = material::ResolvePolicy(material.package->definition, material.asset);
        const bool transparent = policy.surface == shader::SurfaceType::Transparent;
        material::Preparation prepare;
        prepare.pass = role;
        prepare.color = rhi::Format::Rgba32Float;
        prepare.depthStencilFormat = shadow ? frame.shadowDepthFormat : cameraDepthFormat;
        const auto& w = instance.world;
        const math::Vec3 x{w.At(0, 0), w.At(1, 0), w.At(2, 0)},
            y{w.At(0, 1), w.At(1, 1), w.At(2, 1)}, z{w.At(0, 2), w.At(1, 2), w.At(2, 2)};
        prepare.mirrored = math::Dot(math::Cross(x, y), z) < 0;
        prepare.vertexStride = sizeof(assets::Vertex);
        prepare.attributes = {{"POSITION", 0, rhi::VertexFormat::Float3, 0},
                              {"NORMAL", 1, rhi::VertexFormat::Float3, 12},
                              {"TEXCOORD", 2, rhi::VertexFormat::Float2, 24}};
        prepare.bindings["Frame"] = {0, {}, {}, frameConstants, 0, frameConstants->desc.size};
        prepare.constants["Draw"] = {{"ObjectToWorld", Matrix(instance.world)},
                                     {"NormalToWorld", Matrix(instance.normal)},
                                     {"PreviousObjectToWorld", Matrix(instance.previousWorld)}};
        for (const auto& resource : material.package->Layout(pass.name).resources)
            if (resource.property.symbol == "EnvironmentAtlas")
                prepare.bindings["EnvironmentAtlas"] = {0, self.environmentAtlas};
            else if (resource.property.symbol == "BrdfLut")
                prepare.bindings["BrdfLut"] = {0, self.brdfLut};
            else if (resource.property.symbol == "MainLightShadow")
                prepare.deferredResources.insert("MainLightShadow");
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
        if (prepare.deferredResources.contains("MainLightShadow"))
        {
            const auto& resources = material.package->Layout(pass.name).resources;
            const auto& resource =
                *std::ranges::find_if(resources, [](const auto& value)
                                      { return value.property.symbol == "MainLightShadow"; });
            draw.textures.push_back({resource.slot, "MainLightShadow", uint32_t(resource.scope)});
        }
        if (shadow)
            frame.shadowCasters.push_back(std::move(draw));
        else if (transparent)
            frame.transparent.push_back(std::move(draw));
        else
            frame.opaque.push_back(std::move(draw));
    };
    for (const auto& instance : self.snapshot.shadowCasters)
        prepareDraw(instance, true);
    for (const auto& instance : self.snapshot.instances)
        prepareDraw(instance, false);
    self.pipeline.Build(graph, frame);
    std::string displaySource = "CameraColor";
    if (options.view == SceneView::Shadow)
        displaySource = "MainLightShadow";
    else if (options.view == SceneView::Environment)
        displaySource = importTexture(self.environmentAtlas);
    else if (options.view == SceneView::Brdf)
        displaySource = importTexture(self.brdfLut);
    material::Preparation displayPreparation;
    displayPreparation.pass = "Display";
    displayPreparation.color = target->desc.format;
    displayPreparation.vertexStride = 8;
    displayPreparation.attributes = {{"POSITION", 0, rhi::VertexFormat::Float2, 0}};
    displayPreparation.constants["DisplayParameters"] = {
        {"Exposure", {"float", 1, {self.snapshot.exposure}}},
        {"RawView",
         {"uint",
          1,
          {options.view != SceneView::Final && options.view != SceneView::Environment ? 1.0
                                                                                      : 0.0}}},
        {"ViewportSize", {"float2", 1, {double(target->desc.width), double(target->desc.height)}}}};
    displayPreparation.deferredResources.insert("HdrColor");
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
        constants.emplace_back(binding, displayPass.ReadBuffer(name, rendergraph::Use::Uniform));
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
                context.BindBuffer(binding.slot, handle, 0, binding.size, 0, binding.space);
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
