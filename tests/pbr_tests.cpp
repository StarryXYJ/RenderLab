#include "assets/json_io.hpp"
#include "core/log.hpp"
#include "material/runtime.hpp"
#include "renderer/environment.hpp"
#include "renderer/scene_renderer.hpp"
#include "rhi/commands.hpp"
#include "rhi/device.hpp"
#include "rhi/upload.hpp"
#include "samples/image_io.hpp"
#include "shader/compiler.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace renderlab;
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
void Cpu(const std::filesystem::path& engine, const std::filesystem::path& output)
{
    const auto uniform = renderer::PrefilterEnvironment(nullptr, {.25f, .5f, 2});
    const assets::Image white{1, 1, {255, 255, 255, 255}};
    const auto filtered = renderer::PrefilterEnvironment(&white, {});
    for (size_t i = 0; i < uniform.pixels.size(); i += 4)
    {
        Require(uniform.pixels[i] == .25f && uniform.pixels[i + 1] == .5f &&
                    uniform.pixels[i + 2] == 2,
                "Uniform environment must preserve HDR energy");
        Require(std::abs(filtered.pixels[i] - 1) < 1e-5f,
                "White environment convolution must preserve radiance");
    }
    const auto brdf = renderer::IntegrateEnvironmentBrdf();
    for (size_t i = 0; i < brdf.pixels.size(); i += 4)
        Require(std::isfinite(brdf.pixels[i]) && std::isfinite(brdf.pixels[i + 1]) &&
                    brdf.pixels[i] >= 0 && brdf.pixels[i + 1] >= 0 &&
                    brdf.pixels[i] + brdf.pixels[i + 1] <= 1.05f,
                "Split-sum BRDF must be finite and bounded at grazing/roughness limits");
    Require(rhi::TextureRowPitch(17, rhi::Format::Rgba32Float) == 512 &&
                rhi::TextureRowPitch(17) == 256,
            "Format-aware row pitch");
    shader::CompileOptions options;
    options.includeDirectories = {engine};
    options.cacheDirectory = output / "Shaders";
    for (const auto name : {"Lit", "Unlit", "ToneMapping"})
    {
        const auto build =
            shader::CompileShader(engine / "RenderLab" / (std::string(name) + ".shader"), options);
        Require(bool(build), build.diagnostics.c_str());
        Require(!build.package->programs.empty(), "Both-target shader compilation");
    }
}
uint8_t Display(float linear, float exposure)
{
    const auto x = std::max(0.f, linear) * std::exp2(exposure);
    const auto mapped =
        std::clamp(x * (2.51f * x + .03f) / (x * (2.43f * x + .59f) + .14f), 0.f, 1.f);
    const auto encoded =
        mapped <= .0031308f ? mapped * 12.92f : 1.055f * std::pow(mapped, 1 / 2.4f) - .055f;
    return uint8_t(std::round(encoded * 255));
}
void Gpu(rhi::Runtime& runtime, shader::Target target, const std::filesystem::path& manifest,
         const std::filesystem::path& engine, const std::filesystem::path& output)
{
    // Isolated authored assets: acceptance never edits the source fixture.
    const auto root = output / project::NewId();
    std::filesystem::create_directories(root);
    std::filesystem::copy(manifest.parent_path() / "Assets", root / "Assets",
                          std::filesystem::copy_options::recursive);
    std::filesystem::copy_file(manifest, root / "Pbr.rlproject");
    const auto project = project::Project::Open(root / "Pbr.rlproject", engine);
    assets::Database database(project);
    renderer::SceneRenderer renderer(runtime, target, root / "Pbr.rlproject", engine);
    auto color =
        runtime.CreateTexture({257, 131, rhi::Format::Rgba8Srgb, false, true, "PBR.Display"});
    auto initial = rhi::State::Undefined;
    const auto render = [&](const std::string& name)
    {
        const auto result =
            renderer.Render(color, initial, nullptr, true, output / (name + ".json"));
        runtime.Wait(result.completion);
        initial = rhi::State::CopySource;
        const auto pixels = result.pixels->Map();
        samples::SaveImage(output / (name + ".bmp"), pixels, 257, 131, rhi::TextureRowPitch(257));
        // Discard row padding before deterministic comparisons.
        std::vector<std::byte> packed(257 * 131 * 4);
        for (size_t y = 0; y < 131; ++y)
            std::memcpy(packed.data() + y * 257 * 4, pixels.data() + y * rhi::TextureRowPitch(257),
                        257 * 4);
        return packed;
    };
    auto& document = renderer.Document();
    const auto original = document;
    const auto grid = render("material-grid");
    document.Save(output / "roundtrip.scene");
    document = scene::Scene::Load(output / "roundtrip.scene");
    Require(render("roundtrip") == grid, "PBR environment/exposure scene roundtrip");
    auto sphere =
        *std::ranges::find(document.objects, std::string("metal-0"), &scene::Object::name);
    sphere.transform.position = {0, 0, 0};
    sphere.transform.scale = {1.4f, 1.4f, 1.4f};
    std::erase_if(document.objects, [](const auto& object) { return object.mesh.has_value(); });
    document.objects.push_back(sphere);
    const auto materialId = sphere.mesh->materials.begin()->second;
    const auto materialPath = database.Get(materialId).path;
    shader::CompileOptions options;
    options.includeDirectories = {engine};
    options.cacheDirectory = root / "ShaderCache";
    const auto lit = shader::CompileShader(engine / "RenderLab/Lit.shader", options);
    Require(bool(lit), lit.diagnostics.c_str());
    const shader::MaterialTemplate litTemplate(lit.package);
    auto material = material::Create(litTemplate);
    material::Set(material, litTemplate, "base_color", std::array<double, 4>{.6, .2, .05, 1});
    material::Set(material, litTemplate, "roughness", std::array<double, 1>{.2});
    int revision = 0;
    const auto save = [&]
    {
        material::Save(material, materialPath);
        std::filesystem::last_write_time(materialPath,
                                         std::filesystem::file_time_type::clock::now() +
                                             std::chrono::seconds(++revision));
    };
    save();
    const auto dielectric = render("dielectric");
    material::Set(material, litTemplate, "metallic", std::array<double, 1>{1});
    save();
    const auto metal = render("metal");
    Require(metal != dielectric, "Metallic factor must change reflectance");
    material::Set(material, litTemplate, "roughness", std::array<double, 1>{1});
    save();
    Require(render("rough") != metal, "Roughness must change specular lobe/IBL");
    material::SetResource(material, litTemplate, "NormalMap",
                          database.AtPath(project.assets / "PbrNormal.png").id);
    save();
    const auto normal = render("normal-map");
    material::Set(material, litTemplate, "normal_scale", std::array<double, 1>{0});
    save();
    Require(render("normal-disabled") != normal, "Linear normal map must change lighting");
    material::SetResource(material, litTemplate, "MetallicRoughnessMap",
                          database.AtPath(project.assets / "PbrMetallicRoughness.png").id);
    save();
    const auto mapped = render("metallic-roughness-map");
    material::SetResource(material, litTemplate, "MetallicRoughnessMap", "white");
    save();
    Require(render("metallic-roughness-white") != mapped, "Linear MR channels must change surface");
    for (auto& object : document.objects)
        if (object.light)
            object.light->enabled = false;
    const auto ibl = render("ibl-only");
    document.environmentIntensity = 0;
    Require(render("ibl-disabled") != ibl, "IBL must light metals without direct lights");
    document.environmentIntensity = 3;
    document.environmentRotation = 120;
    Require(render("ibl-rotated") != ibl, "Environment rotation must change reflections");
    material::Set(material, litTemplate, "emission", std::array<double, 3>{8, 2, .5});
    save();
    document.environmentIntensity = 0;
    const auto emission = render("emission");
    document.exposure = -2;
    Require(render("exposure-minus2") != emission,
            "HDR exposure must preserve overbright emission");
    const auto unlit = shader::CompileShader(engine / "RenderLab/Unlit.shader", options);
    Require(bool(unlit), unlit.diagnostics.c_str());
    const shader::MaterialTemplate unlitTemplate(unlit.package);
    // A retired frame must not evict every PSO while its material generation remains active.
    // This reproduces the scene host's lifetime: it retains the generation, not Prepared draws.
    {
        material::Runtime materialRuntime(runtime, target);
        material::Preparation preparation;
        preparation.pass = "Forward";
        preparation.color = rhi::Format::Rgba32Float;
        preparation.vertexStride = sizeof(assets::Vertex);
        preparation.attributes = {{"POSITION", 0, rhi::VertexFormat::Float3, 0},
                                  {"NORMAL", 1, rhi::VertexFormat::Float3, 12},
                                  {"TEXCOORD", 2, rhi::VertexFormat::Float2, 24}};
        auto white = runtime.CreateView(runtime.CreateTexture(
            {1, 1, rhi::Format::Rgba8Srgb, false, false, "PSO.Residency.White"}));
        auto sampler = runtime.CreateSampler();
        const material::ResourceResolver resolve =
            [&](const shader::Property& property, const std::string&)
        {
            rhi::Binding binding{};
            if (property.type.resource == rhi::BindingKind::Sampler)
                binding.sampler = sampler;
            else
                binding.view = white;
            return binding;
        };
        std::shared_ptr<const material::GpuGeneration> residentGeneration;
        std::weak_ptr<rhi::Pipeline> previousPipeline;
        for (int frameIndex = 0; frameIndex < 4; ++frameIndex)
        {
            auto draw = materialRuntime.Prepare(unlit.package, material::Create(unlitTemplate),
                                                preparation, resolve);
            if (frameIndex)
                Require(draw.pipeline == previousPipeline.lock(),
                        "Material generation must retain PSO between fully retired frames");
            residentGeneration = draw.generation;
            previousPipeline = draw.pipeline;
            runtime.WaitIdle();
        }
        Require(!previousPipeline.expired(), "Active material generation lost its PSO");
        residentGeneration.reset();
        Require(previousPipeline.expired(), "PSO residency must end with its material generation");
    }
    material = material::Create(unlitTemplate);
    material::Set(material, unlitTemplate, "base_color", std::array<double, 4>{4, 1, .25, 1});
    save();
    const auto constant = render("unlit-hdr");
    for (auto& object : document.objects)
        if (object.light)
        {
            object.light->enabled = true;
            object.light->intensity = 50;
        }
    document.environmentIntensity = 40;
    Require(render("unlit-lit-environment") == constant, "Unlit must be independent of lighting");
    const auto center = (size_t(65) * 257 + 128) * 4;
    for (size_t c = 0; c < 3; ++c)
        Require(std::abs(int(std::to_integer<uint8_t>(constant[center + c])) -
                         int(Display(std::array<float, 3>{4, 1, .25f}[c], -2))) <= 1,
                "GPU HDR/ACES/sRGB must match CPU within one display code");
    // A float upload/readback roundtrip with padded rows exercises the extended texel size.
    const auto pitch = rhi::TextureRowPitch(17, rhi::Format::Rgba32Float);
    auto input = runtime.CreateBuffer({pitch * 3, rhi::Memory::Upload, "HDR.Upload"});
    auto readback = runtime.CreateBuffer({pitch * 3, rhi::Memory::Readback, "HDR.Readback"});
    auto texture =
        runtime.CreateTexture({17, 3, rhi::Format::Rgba32Float, false, false, "HDR.Probe"});
    std::array<float, 17 * 3 * 4> values{};
    for (size_t i = 0; i < values.size(); ++i)
        values[i] = float(i) / 7 - 4;
    for (size_t y = 0; y < 3; ++y)
        std::memcpy(input->Map().data() + y * pitch, values.data() + y * 17 * 4, 17 * 16);
    auto commands = runtime.CreateCommands();
    commands->Barrier(input, rhi::State::HostWrite, rhi::State::CopySource);
    commands->Barrier(texture, rhi::State::Undefined, rhi::State::CopyDest);
    commands->CopyTexture(input, texture, pitch, true);
    commands->Barrier(texture, rhi::State::CopyDest, rhi::State::CopySource);
    commands->Barrier(readback, rhi::State::CopyDest, rhi::State::CopyDest);
    commands->CopyTexture(readback, texture, pitch, false);
    commands->Barrier(readback, rhi::State::CopyDest, rhi::State::HostRead);
    runtime.Wait(runtime.Submit(std::move(commands)));
    for (size_t y = 0; y < 3; ++y)
        Require(std::memcmp(readback->Map().data() + y * pitch, values.data() + y * 17 * 4,
                            17 * 16) == 0,
                "RGBA32F upload/readback footprint");
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 5, "Expected backend/cpu, project, engine, output");
        const std::filesystem::path manifest(argv[2]), engine(argv[3]), output(argv[4]);
        std::filesystem::create_directories(output);
        if (std::string(argv[1]) == "cpu")
            Cpu(engine, output);
        else
        {
            Logger logger(output / "pbr.log");
            rhi::DeviceOptions options;
            options.backend =
                std::string(argv[1]) == "dx12" ? rhi::Backend::Dx12 : rhi::Backend::Vulkan;
            options.validation = rhi::ValidationMode::On;
            auto device = rhi::CreateDevice(options, logger);
            {
                auto runtime = device->CreateRuntime();
                Gpu(*runtime,
                    options.backend == rhi::Backend::Dx12 ? shader::Target::Dxil
                                                          : shader::Target::Spirv,
                    manifest, engine, output);
                runtime->WaitIdle();
                Require(runtime->GetMemoryStats().used == 0, "PBR resources leaked");
            }
            device->PollDiagnostics();
            device->Shutdown();
            Require(device->GetValidationErrorCount() == 0, "PBR validation errors");
        }
        std::cout << "PBR contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
