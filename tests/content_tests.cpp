#include "assets/json_io.hpp"
#include "assets/model.hpp"
#include "math/math.hpp"
#include "scene/scene.hpp"
#include "shader/compiler.hpp"

#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
using namespace renderlab;
using Json = nlohmann::json;
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void Fails(F function, const std::string& message)
{
    try
    {
        function();
    }
    catch (const std::exception& error)
    {
        Require(std::string(error.what()).find(message) != std::string::npos, error.what());
        return;
    }
    throw std::runtime_error("Expected failure: " + message);
}
bool Near(float a, float b)
{
    return std::abs(a - b) < 1e-4f;
}
void ImageApartmentContracts(const std::vector<std::byte>& encoded)
{
    APTTYPE apartment;
    APTTYPEQUALIFIER qualifier;
    Require(CoGetApartmentType(&apartment, &qualifier) == CO_E_NOTINITIALIZED,
            "Image fallback test requires an uninitialized import thread");
    assets::DecodeImage(encoded);
    Require(CoGetApartmentType(&apartment, &qualifier) == CO_E_NOTINITIALIZED,
            "Standalone image import leaked its temporary COM apartment");
    std::exception_ptr failure;
    std::thread uiThread(
        [&]
        {
            const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            try
            {
                Require(SUCCEEDED(initialized), "Initialize test UI STA");
                assets::DecodeImage(encoded);
                Fails([] { assets::DecodeImage(std::vector<std::byte>(4)); }, "WIC");
                APTTYPE type;
                APTTYPEQUALIFIER typeQualifier;
                Require(SUCCEEDED(CoGetApartmentType(&type, &typeQualifier)) &&
                            (type == APTTYPE_STA || type == APTTYPE_MAINSTA),
                        "Successful or failed image decode must preserve its caller's STA");
            }
            catch (...)
            {
                failure = std::current_exception();
            }
            if (SUCCEEDED(initialized))
                CoUninitialize();
        });
    uiThread.join();
    if (failure)
        std::rethrow_exception(failure);
}
void MathContracts()
{
    const auto projection = math::Perspective(60, 1, .1f, 100);
    const auto depth = [&](float z)
    { return (projection.At(2, 2) * z + projection.At(2, 3)) / -z; };
    Require(Near(depth(-.1f), 1) && Near(depth(-100), 0), "Reversed-Z endpoints");
    const auto world =
        math::Compose({1, 2, 3}, math::Quaternion::EulerDegrees({10, 35, 5}), {2, 3, 4});
    const auto local = math::Point(math::Inverse(world), math::Point(world, {.3f, .5f, .7f}));
    Require(Near(local.x, .3f) && Near(local.y, .5f) && Near(local.z, .7f),
            "Matrix inverse/column-vector convention");
    const auto tangent = math::Direction(world, {1, 0, 0});
    const auto normal = math::Direction(math::Transpose(math::Inverse(world)), {0, 1, 0});
    Require(std::abs(math::Dot(tangent, normal)) < 1e-4f,
            "Inverse-transpose normal under nonuniform scale");
    Require(math::Visible(projection, {{-.5f, -.5f, -3}, {.5f, .5f, -2}}), "Visible AABB");
    Require(!math::Visible(projection, {{100, 0, -3}, {101, 1, -2}}), "Outside AABB");
    Fails([] { math::Compose({}, {}, {0, 1, 1}); }, "Singular");
    Fails([] { math::Perspective(60, 0, .1f, 100); }, "camera");
}
void ContentContracts(const std::filesystem::path& manifest, const std::filesystem::path& output,
                      const std::filesystem::path& engine)
{
    const auto root = output / project::NewId();
    std::filesystem::create_directories(root);
    std::filesystem::copy(manifest.parent_path() / "Assets", root / "Assets",
                          std::filesystem::copy_options::recursive);
    std::filesystem::copy_file(manifest, root / "RenderLab.rlproject");
    const auto project = project::Project::Open(root / "RenderLab.rlproject", engine);
    assets::Database db(project);
    Fails([&] { project.ResolveAssetPath("../escape"); }, "escapes");
    auto scene = scene::Scene::Load(db.Get(project.startScene).path);
    auto& renderer = *std::ranges::find_if(scene.objects, [](const auto& object)
                                           { return object.mesh.has_value(); })
                          ->mesh;
    const auto modelId = renderer.model;
    const auto materialFile =
        std::ranges::find_if(scene.objects, [](const auto& object)
                             { return object.mesh && !object.mesh->materials.empty(); })
            ->mesh->materials.begin()
            ->second;
    const auto userMaterial = assets::detail::Read(db.Get(materialFile).path);
    ImageApartmentContracts(assets::detail::Read(project.assets / "Checker.png"));
    auto model = assets::LoadModel(db, modelId);
    Require(!model.cacheHit && !model.meshes.empty() && !model.instances.empty(),
            "Initial model import");
    const auto stableMesh = model.meshes.front().id;
    Require(assets::LoadModel(db, modelId).cacheHit, "Model cache reuse");
    const auto originalGltf = assets::detail::Read(db.Get(modelId).path);
    auto pbrSource = assets::detail::LoadJson(db.Get(modelId).path);
    auto& pbrMaterial = pbrSource["materials"][0];
    pbrMaterial["pbrMetallicRoughness"]["metallicFactor"] = .75f;
    pbrMaterial["pbrMetallicRoughness"]["roughnessFactor"] = .25f;
    pbrMaterial["pbrMetallicRoughness"]["metallicRoughnessTexture"] = {{"index", 0}};
    pbrMaterial["normalTexture"] = {{"index", 0}, {"scale", .5f}};
    pbrMaterial["occlusionTexture"] = {{"index", 0}, {"strength", .8f}};
    pbrMaterial["emissiveTexture"] = {{"index", 0}};
    pbrMaterial["emissiveFactor"] = {.1f, .2f, .3f};
    assets::detail::SaveJson(db.Get(modelId).path, pbrSource);
    auto importedPbr = assets::LoadModel(db, modelId);
    Require(!importedPbr.cacheHit && importedPbr.key != model.key, "PBR factors invalidate cache");
    importedPbr = assets::LoadModel(db, modelId);
    const auto& surface = importedPbr.materials.front();
    Require(importedPbr.cacheHit && Near(surface.metallic, .75f) && Near(surface.roughness, .25f) &&
                Near(surface.normalScale, .5f) && Near(surface.occlusionStrength, .8f) &&
                Near(surface.emission[2], .3f) && surface.metallicRoughnessImage == 0 &&
                surface.normalImage == 0 && surface.occlusionImage == 0 &&
                surface.emissionImage == 0,
            "PBR factors and image references survive cached import");
    pbrMaterial["pbrMetallicRoughness"]["roughnessFactor"] = -1;
    assets::detail::SaveJson(db.Get(modelId).path, pbrSource);
    Fails([&] { assets::LoadModel(db, modelId); }, "factor");
    WriteFileAtomically(db.Get(modelId).path, originalGltf);
    const auto cache = project.library / "Models" / (modelId + "-" + model.key + ".cbor");
    const std::string bad = "corrupt";
    WriteFileAtomically(cache, std::as_bytes(std::span{bad.data(), bad.size()}));
    Require(!assets::LoadModel(db, modelId).cacheHit, "Corrupt cache rebuild");
    const auto dependency = db.Get(modelId).dependencies.at("Cube.bin");
    const auto imageId = db.Get(modelId).dependencies.at("Checker.png");
    db.Rename(modelId, "Models/Moved.gltf");
    db.Rename(dependency, "Buffers/Moved.bin");
    db.Rename(imageId, "Textures/Moved.png");
    std::filesystem::remove(cache);
    model = assets::LoadModel(db, modelId);
    Require(!model.cacheHit && model.meshes.front().id == stableMesh,
            "Moved dependencies and stable subresources survive cache rebuild");
    Require(assets::detail::Read(db.Get(materialFile).path) == userMaterial,
            "Import preserves user material overrides");
    auto buffer = assets::detail::Read(db.Get(dependency).path);
    const float changed = -.9f;
    std::memcpy(buffer.data(), &changed, sizeof(changed));
    WriteFileAtomically(db.Get(dependency).path, buffer);
    const auto modified = assets::LoadModel(db, modelId);
    Require(modified.key != model.key && !modified.cacheHit,
            "External dependency invalidates import cache");
    // Build a GLB from the same indexed/interleaved model.
    auto gltf = assets::detail::LoadJson(db.Get(modelId).path);
    gltf["buffers"][0].erase("uri");
    gltf["images"][0]["uri"] =
        assets::detail::Text(db.Get(imageId).path.lexically_relative(project.assets));
    std::string json = gltf.dump();
    while (json.size() % 4)
        json += ' ';
    auto binary = buffer;
    while (binary.size() % 4)
        binary.push_back(std::byte{0});
    std::vector<std::byte> glb;
    const auto word = [&](uint32_t value)
    {
        const auto bytes = std::as_bytes(std::span{&value, size_t{1}});
        glb.insert(glb.end(), bytes.begin(), bytes.end());
    };
    word(0x46546c67);
    word(2);
    word(static_cast<uint32_t>(12 + 8 + json.size() + 8 + binary.size()));
    word(static_cast<uint32_t>(json.size()));
    word(0x4e4f534a);
    const auto jsonBytes = std::as_bytes(std::span{json.data(), json.size()});
    glb.insert(glb.end(), jsonBytes.begin(), jsonBytes.end());
    word(static_cast<uint32_t>(binary.size()));
    word(0x004e4942);
    glb.insert(glb.end(), binary.begin(), binary.end());
    WriteFileAtomically(project.assets / "Binary.glb", glb);
    db.Scan();
    const auto binaryModel = assets::LoadModel(db, db.AtPath(project.assets / "Binary.glb").id);
    Require(binaryModel.meshes.front().indices == model.meshes.front().indices, "GLB decode");
    const auto source = assets::detail::Read(db.Get(modelId).path);
    auto invalid = assets::detail::LoadJson(db.Get(modelId).path);
    invalid["meshes"][0]["primitives"][0]["mode"] = 1;
    assets::detail::SaveJson(db.Get(modelId).path, invalid);
    Fails([&] { assets::LoadModel(db, modelId); }, "triangle");
    WriteFileAtomically(db.Get(modelId).path, source);
    auto metadataPath = db.Get(modelId).path;
    metadataPath += ".meta";
    auto metadata = assets::detail::LoadJson(metadataPath);
    WriteFileAtomically(project.assets / "Duplicate.gltf", source);
    assets::detail::SaveJson(project.assets / "Duplicate.gltf.meta", metadata);
    Fails([&] { db.Scan(); }, "Duplicate");
    std::filesystem::remove(project.assets / "Duplicate.gltf");
    std::filesystem::remove(project.assets / "Duplicate.gltf.meta");
    db.Scan();
    std::map<std::string, assets::Model> models{{modelId, assets::LoadModel(db, modelId)}};
    for (const auto& object : scene.objects)
        if (object.mesh && !models.contains(object.mesh->model))
            models.emplace(object.mesh->model, assets::LoadModel(db, object.mesh->model));
    scene::Extractor extractor;
    const auto snapshot = extractor.Extract(scene, 16.f / 9, models);
    Require(!snapshot.instances.empty() && snapshot.culledInstances > 0,
            "Scene extraction and CPU culling");
    const auto selected = snapshot.light.object;
    std::reverse(scene.objects.begin(), scene.objects.end());
    Require(extractor.Extract(scene, 16.f / 9, models).light.object == selected,
            "Light selection independent of storage order");
    scene.Get(selected).light->enabled = false;
    Require(extractor.Extract(scene, 16.f / 9, models).light.object != selected,
            "Disabled directional light is skipped");
    for (auto& object : scene.objects)
        if (object.light)
            object.light->enabled = false;
    Require(extractor.Extract(scene, 16.f / 9, models).light.intensity == 0,
            "Absent main light is black");
    scene.Save(root / "Roundtrip.scene");
    const auto roundtrip = scene::Scene::Load(root / "Roundtrip.scene");
    Require(roundtrip.objects.size() == scene.objects.size(), "Scene serialization");
    const auto originalParent = scene.objects.front().parent;
    scene.objects.front().parent = scene.objects.front().id;
    Fails([&] { scene.WorldTransforms(); }, "cycle");
    scene.objects.front().parent = originalParent;
    auto duplicate = scene.objects.front();
    scene.objects.push_back(duplicate);
    Fails([&] { scene.WorldTransforms(); }, "Duplicate");
    scene.objects.pop_back();
    // A project-authored shader can use the engine parameter module and include root.
    const auto shaderDir = root / "Shader";
    std::filesystem::create_directories(shaderDir);
    std::string declaration = shader::ReadText(engine / "RenderLab/BlinnPhong.shader");
    auto position = declaration.find("use \"Scene.params\"");
    declaration.replace(position, std::string("use \"Scene.params\"").size(),
                        "use \"RenderLab/Scene.params\"");
    std::filesystem::copy_file(engine / "RenderLab/BlinnPhong.hlsl", shaderDir / "BlinnPhong.hlsl");
    WriteFileAtomically(shaderDir / "Custom.shader",
                        std::as_bytes(std::span{declaration.data(), declaration.size()}));
    shader::CompileOptions options;
    options.includeDirectories = {engine, engine / "RenderLab"};
    options.cacheDirectory = root / "ShaderCache";
    const auto compiled = shader::CompileShader(shaderDir / "Custom.shader", options);
    Require(bool(compiled), compiled.diagnostics.c_str());
}
} // namespace
int wmain(int argc, wchar_t** argv)
{
    try
    {
        Require(argc == 4, "Expected fixture manifest, output root and engine shaders");
        MathContracts();
        ContentContracts(argv[1], argv[2], argv[3]);
        std::cout << "Content contracts passed: "
                     "UUID/cache/rename/glTF/GLB/scene/camera/light/math/shader library\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
