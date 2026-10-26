#include "assets/json_io.hpp"
#include "core/log.hpp"
#include "material/asset.hpp"
#include "renderer/scene_renderer.hpp"
#include "renderer/shadow.hpp"
#include "rhi/commands.hpp"
#include "rhi/device.hpp"
#include "rhi/runtime.hpp"
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
void Require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}
void Cpu()
{
    scene::Snapshot snapshot;
    snapshot.light.direction = {0, 1, 0};
    scene::Instance instance;
    instance.bounds = {{-4, -1, -2}, {3, 5, 6}};
    snapshot.shadowCasters.push_back(instance);
    const auto matrix = renderer::FitDirectionalShadow(snapshot);
    for (int i = 0; i < 8; ++i)
    {
        const auto p =
            math::Point(matrix, {i & 1 ? 3.f : -4.f, i & 2 ? 5.f : -1.f, i & 4 ? 6.f : -2.f});
        Require(std::abs(p.x) < 1 && std::abs(p.y) < 1 && p.z > 0 && p.z < 1,
                "Vertical-light projection must enclose all caster bounds with padding");
    }
    Require(math::Point(matrix, {0, 4, 0}).z > math::Point(matrix, {0, 0, 0}).z,
            "Shadow depth must increase toward the light");
    snapshot.shadowCasters.front().bounds = {{1, 1, 1}, {1, 1, 1}};
    for (float value : renderer::FitDirectionalShadow(snapshot).values)
        Require(std::isfinite(value), "Degenerate shadow bounds must yield a finite projection");
}
uint8_t Display(float x)
{
    x = std::clamp(x * (2.51f * x + .03f) / (x * (2.43f * x + .59f) + .14f), 0.f, 1.f);
    x = x <= .0031308f ? x * 12.92f : 1.055f * std::pow(x, 1 / 2.4f) - .055f;
    return uint8_t(std::round(x * 255));
}
void Gpu(rhi::Runtime& runtime, shader::Target target, const std::filesystem::path& manifest,
         const std::filesystem::path& engine, const std::filesystem::path& output)
{
    const auto root = output / project::NewId();
    std::filesystem::create_directories(root);
    std::filesystem::copy(manifest.parent_path() / "Assets", root / "Assets",
                          std::filesystem::copy_options::recursive);
    std::filesystem::copy_file(manifest, root / "Shadow.rlproject");
    const auto project = project::Project::Open(root / "Shadow.rlproject", engine);
    assets::Database database(project);
    renderer::SceneRenderer renderer(runtime, target, root / "Shadow.rlproject", engine);
    auto color =
        runtime.CreateTexture({257, 193, rhi::Format::Rgba8Srgb, false, true, "Surface.Display"});
    auto initial = rhi::State::Undefined;
    renderer::SceneReadback lastFrame;
    const auto render = [&](const std::string& name, renderer::SceneRenderOptions settings = {})
    {
        const auto result =
            renderer.Render(color, initial, nullptr, true, output / (name + ".json"), settings);
        runtime.Wait(result.completion);
        lastFrame = result;
        initial = rhi::State::CopySource;
        const auto pixels = result.pixels->Map();
        samples::SaveImage(output / (name + ".bmp"), pixels, 257, 193, rhi::TextureRowPitch(257));
        std::vector<uint8_t> packed(257 * 193 * 4);
        for (size_t y = 0; y < 193; ++y)
            std::memcpy(packed.data() + y * 257 * 4, pixels.data() + y * rhi::TextureRowPitch(257),
                        257 * 4);
        return packed;
    };
    auto& document = renderer.Document();
    const auto object = [&](const std::string& name) -> scene::Object&
    { return *std::ranges::find(document.objects, name, &scene::Object::name); };
    const auto original = document;
    const auto shadowed = render("shadow-masked-transparent");
    Require(render("profile-enabled", {.profile = true}) == shadowed,
            "GPU profiling must preserve rendered output");
    const auto timings = lastFrame.profile.Read(runtime);
    Require(
        timings.size() == 5 &&
            std::ranges::all_of(
                timings, [](const auto& timing)
                { return std::isfinite(timing.nanoseconds) && timing.nanoseconds >= 0; }) &&
            std::ranges::any_of(timings, [](const auto& timing) { return timing.nanoseconds > 0; }),
        "Every live shadow/opaque/transparent/display/readback pass must have valid GPU "
        "timestamps");
    auto pending = lastFrame.profile;
    pending.completion = runtime.LastSubmitted() + 1;
    bool earlyRejected = false;
    try
    {
        pending.Read(runtime);
    }
    catch (const std::invalid_argument&)
    {
        earlyRejected = true;
    }
    Require(earlyRejected, "GPU profiling must reject mapping an incomplete submission");
    const auto depthView =
        render("viewer-shadow", {.view = renderer::SceneView::Shadow, .profile = true});
    Require(depthView != shadowed && lastFrame.profile.Read(runtime).size() == 3,
            "Shadow viewer must retain shadow/display/readback and cull unused Forward draws");
    for (size_t i = 0; i < depthView.size(); i += 4)
        Require(depthView[i] == depthView[i + 1] && depthView[i] == depthView[i + 2],
                "Shadow viewer must display grayscale stored depth");
    Require(render("viewer-hdr", {.view = renderer::SceneView::Hdr}) != shadowed,
            "HDR viewer must bypass the ACES display curve");
    Require(render("viewer-environment", {.view = renderer::SceneView::Environment}) != shadowed &&
                render("viewer-brdf", {.view = renderer::SceneView::Brdf}) != shadowed,
            "Environment and BRDF texture previews must render");
    renderer::SceneRenderOptions quality;
    quality.shadowResolution = 128;
    render("quality-low", quality);
    Require(renderer.LastSnapshot().light.shadowResolution == 128 &&
                document.Get(renderer.LastSnapshot().light.object).light->shadowResolution == 512,
            "Quality override must affect rendering without mutating scene settings");
    document.Save(output / "roundtrip.scene");
    document = scene::Scene::Load(output / "roundtrip.scene");
    Require(render("roundtrip") == shadowed,
            "Shadow settings and material roles must survive scene roundtrip");
    object("sun").light->shadows = false;
    const auto unshadowed = render("no-shadow");
    size_t darkened = 0, brightened = 0;
    for (size_t i = 0; i < shadowed.size(); i += 4)
    {
        const int a = shadowed[i] + shadowed[i + 1] + shadowed[i + 2];
        const int b = unshadowed[i] + unshadowed[i + 1] + unshadowed[i + 2];
        darkened += a + 20 < b;
        brightened += b + 5 < a;
    }
    Require(darkened > 100 && brightened == 0,
            "Shadowing must only reduce direct light and cover a visible receiver");
    object("sun").light->intensity = 0;
    const auto noLight = render("no-direct-light");
    object("sun").light->shadows = true;
    Require(render("no-direct-light-shadow-enabled") == noLight,
            "Shadows must not attenuate IBL or emission");
    document = original;
    object("sun").transform.rotation = math::Quaternion::EulerDegrees({-65, 45, 0});
    Require(render("rotated-light") != shadowed, "Rotating the light must move shadows");
    object("sun").light->shadowResolution = 128;
    Require(render("resolution-128") != shadowed, "Alternate shadow resolution must render");

    document = original;
    object("glass").enabled = false;
    object("masked").enabled = false;
    object("solid").transform.position = {-4, 1, 0};
    object("solid").transform.scale = {.6f, .6f, .6f};
    auto& camera = document.Get(document.activeCamera);
    camera.transform.position = {0, 3, 7};
    camera.transform.rotation = math::Quaternion::EulerDegrees({-20, 0, 0});
    camera.camera->fov = 30;
    object("sun").transform.rotation = math::Quaternion::EulerDegrees({-30, -80, 0});
    const auto offscreen = render("offscreen-caster");
    const auto solidId = object("solid").id;
    Require(!std::ranges::any_of(renderer.LastSnapshot().instances, [&](const auto& instance)
                                 { return instance.id.starts_with(solidId); }) &&
                std::ranges::any_of(renderer.LastSnapshot().shadowCasters, [&](const auto& instance)
                                    { return instance.id.starts_with(solidId); }),
            "Offscreen shadow fixture must be camera culled and retained for shadow rendering");
    object("sun").light->shadows = false;
    const auto offscreenUnshadowed = render("offscreen-no-shadow");
    size_t offscreenDarkened = 0;
    for (size_t i = 0; i < offscreen.size(); i += 4)
        offscreenDarkened += offscreen[i] + 15 < offscreenUnshadowed[i];
    Require(offscreenDarkened > 20, "Camera-culled caster must still shadow the visible receiver");

    // Two intersecting view rays through separate planar layers exercise real linear HDR blend.
    // UUID order deliberately places the near layer first; distance sorting must reverse it.
    shader::CompileOptions compile;
    compile.includeDirectories = {engine};
    compile.cacheDirectory = root / "Shaders";
    const auto build = shader::CompileShader(engine / "RenderLab/Unlit.shader", compile);
    Require(bool(build), build.diagnostics.c_str());
    const shader::MaterialTemplate schema(build.package);
    const auto lit = shader::CompileShader(engine / "RenderLab/Lit.shader", compile);
    Require(bool(lit), lit.diagnostics.c_str());
    const shader::MaterialTemplate litSchema(lit.package);
    int revision = 0;
    const auto save = [&](const std::string& id, std::array<double, 4> rgba,
                          const std::string& variant, bool useLit = false)
    {
        const auto& selected = useLit ? litSchema : schema;
        auto asset = material::Create(selected);
        material::Set(asset, selected, "base_color", rgba);
        material::Set(asset, selected, "surface",
                      std::array<double, 1>{variant == "Transparent" ? 1.0 : 0.0});
        material::Set(asset, selected, "alpha_clip",
                      std::array<double, 1>{variant == "Masked" ? 1.0 : 0.0});
        const auto& path = database.Get(id).path;
        material::Save(asset, path);
        std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() +
                                                   std::chrono::seconds(++revision));
    };
    document = original;
    auto background = object("floor"), nearLayer = object("masked"), farLayer = object("glass");
    const auto backMaterial = background.mesh->materials.begin()->second;
    const auto nearMaterial = nearLayer.mesh->materials.begin()->second;
    const auto farMaterial = farLayer.mesh->materials.begin()->second;
    save(backMaterial, {.1, .2, .3, 1}, "Default");
    save(nearMaterial, {1, 0, 0, .25}, "Transparent");
    save(farMaterial, {0, 0, 1, .5}, "Transparent");
    std::erase_if(document.objects, [](const auto& value) { return value.mesh.has_value(); });
    document.Get(document.activeCamera).transform = {{0, 0, 7}, {1, 1, 1}, {}};
    object("sun").light->shadows = false;
    background.transform = {{0, 0, 0}, {2, 2, .01f}, {}};
    nearLayer.transform = {{0, 0, 2}, {1, 1, .01f}, {}};
    farLayer.transform = {{0, 0, 1}, {1, 1, .01f}, {}};
    nearLayer.id = "00000000000000000000000000000041";
    farLayer.id = "00000000000000000000000000000042";
    document.objects.insert(document.objects.end(), {background, nearLayer, farLayer});
    const auto alpha = render("alpha-sorted-hdr");
    const auto center = (size_t(96) * 257 + 128) * 4;
    const auto expect = [&](const std::vector<uint8_t>& pixels, std::array<float, 3> linear)
    {
        for (size_t c = 0; c < 3; ++c)
            Require(std::abs(int(pixels[center + c]) - int(Display(linear[c]))) <= 1,
                    "Linear HDR alpha composition must match CPU before ACES/sRGB");
    };
    expect(alpha, {.2875f, .075f, .4875f});
    // Storage order and mesh identity must not change the alpha composition.
    std::reverse(document.objects.begin(), document.objects.end());
    Require(render("alpha-reordered") == alpha,
            "Transparent sorting must ignore document storage order");
    save(nearMaterial, {1, 0, 0, 0}, "Transparent");
    const auto zero = render("alpha-zero");
    document.Get(nearLayer.id).enabled = false;
    Require(render("alpha-hidden") == zero,
            "Zero alpha must neither obscure color nor write depth");
    expect(zero, {.05f, .1f, .65f});
    document.Get(nearLayer.id).enabled = true;
    save(nearMaterial, {1, 0, 0, .25}, "Masked");
    Require(render("masked-discard") == zero,
            "Masked alpha below cutoff must leave color and depth untouched");
    save(nearMaterial, {1, 0, 0, .25}, "Default");
    expect(render("opaque-ignores-alpha"), {1, 0, 0});
    document.Get(nearLayer.id).transform.position.z = .5f;
    const auto depthOccluded = render("transparent-depth-test");
    expect(depthOccluded, {.5f, 0, .5f});

    // With only a receiver and a fully clipped caster, enabling shadows must not darken it.
    document = original;
    object("solid").enabled = false;
    object("glass").enabled = false;
    const auto maskedId = object("masked").mesh->materials.begin()->second;
    save(backMaterial, {.65, .65, .65, 1}, "Default", true);
    save(maskedId, {1, 1, 1, 0}, "Masked", true);
    object("sun").light->shadows = true;
    const auto clippedShadow = render("masked-no-cast");
    object("sun").light->shadows = false;
    Require(render("masked-no-cast-disabled") == clippedShadow,
            "Fully clipped material must not cast a shadow");
    object("sun").light->shadows = true;
    {
        document = original;
        const auto baseline = render("policy-before-rename");
        const auto isolatedEngine = root / "engine";
        std::filesystem::create_directories(isolatedEngine);
        std::filesystem::copy(engine / "RenderLab", isolatedEngine / "RenderLab",
                              std::filesystem::copy_options::recursive);
        for (const auto name : {"Lit", "Unlit"})
        {
            const auto path = isolatedEngine / "RenderLab" / (std::string(name) + ".shader");
            auto source = shader::ReadText(path);
            const auto replace = [&](const std::string& from, const std::string& to)
            {
                size_t position = 0;
                while ((position = source.find(from, position)) != std::string::npos)
                {
                    source.replace(position, from.size(), to);
                    position += to.size();
                }
            };
            replace("pass Forward", "pass ArbitraryBeautyName");
            replace("pass Shadow", "pass ArbitraryCasterName");
            for (const auto module : {"Scene", "View", "Object", "Shadow"})
                replace(std::string("use \"") + module + ".params\";",
                        std::string("use \"") + module + ".params\" as Renamed;");
            replace("Texture2D<float4> EnvironmentAtlas;", "Texture2D<float4> SkyTexture;");
            replace("Texture2D<float4> BrdfLut;", "Texture2D<float4> LookupTexture;");
            replace("Texture2D<float4> MainLightShadow;", "Texture2D<float4> SunTexture;");
            replace("cull = $Cull;",
                    "cull = $Cull; stencil { enabled = true; compare = Always; writeMask = 0; }");
            WriteFileAtomically(path, std::as_bytes(std::span{source.data(), source.size()}));
        }
        for (const auto name : {"Lit", "Unlit", "Shadow"})
        {
            const auto path = isolatedEngine / "RenderLab" / (std::string(name) + ".hlsl");
            auto source = shader::ReadText(path);
            const auto replace = [&](const std::string& from, const std::string& to)
            {
                size_t position = 0;
                while ((position = source.find(from, position)) != std::string::npos)
                {
                    source.replace(position, from.size(), to);
                    position += to.size();
                }
            };
            for (const auto block : {"SceneView", "SceneObject", "SceneLighting", "SceneShadow"})
                replace(block, std::string("Renamed_") + block);
            replace(", MainLightShadow,", ", SunTexture,");
            replace(", EnvironmentAtlas, BrdfLut", ", SkyTexture, LookupTexture");
            WriteFileAtomically(path, std::as_bytes(std::span{source.data(), source.size()}));
        }
        renderer::SceneRenderer renamed(runtime, target, root / "Shadow.rlproject", isolatedEngine);
        renamed.Document() = document;
        auto destination = runtime.CreateTexture(
            {257, 193, rhi::Format::Rgba8Srgb, false, true, "Policy.Renamed"});
        const auto result = renamed.Render(destination, rhi::State::Undefined, nullptr, true,
                                           output / "policy-renamed-stencil.json");
        runtime.Wait(result.completion);
        const auto bytes = result.pixels->Map();
        for (size_t y = 0; y < 193; ++y)
            Require(std::memcmp(bytes.data() + y * rhi::TextureRowPitch(257),
                                baseline.data() + y * 257 * 4, 257 * 4) == 0,
                    "Pass/module/resource aliases and identity stencil must preserve scene pixels");
    }
    renderer::SceneRenderOptions largest;
    largest.shadowResolution = 2048;
    auto secondTarget = runtime.CreateTexture(
        {257, 193, rhi::Format::Rgba8Srgb, false, true, "Surface.SecondFrame"});
    const auto first = renderer.Render(color, initial, nullptr, false, {}, largest);
    const auto second =
        renderer.Render(secondTarget, rhi::State::Undefined, nullptr, false, {}, largest);
    runtime.Wait(second.completion);
    Require(second.completion > first.completion &&
                runtime.GetMemoryStats().used <= runtime.GetMemoryStats().budget,
            "Maximum shadow quality must support consecutive submissions within the native budget");
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
            Cpu();
        else
        {
            Logger logger(output / "surface.log");
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
                Require(runtime->GetMemoryStats().used == 0, "Surface resources leaked");
            }
            device->PollDiagnostics();
            device->Shutdown();
            Require(device->GetValidationErrorCount() == 0, "Surface validation errors");
        }
        std::cout << "Shadow/alpha contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
