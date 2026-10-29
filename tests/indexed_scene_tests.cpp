#include "core/log.hpp"
#include "project/project.hpp"
#include "renderer/scene_renderer.hpp"
#include "rhi/device.hpp"
#include "rhi/runtime.hpp"
#include "rhi/upload.hpp"
#include "samples/common/image_io.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>

namespace
{
using namespace renderlab;
using namespace renderer;
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
std::vector<std::byte> Pixels(rhi::Runtime& runtime, const SceneReadback& frame, uint32_t width,
                              uint32_t height)
{
    runtime.Wait(frame.completion);
    const auto bytes = frame.pixels->Map();
    std::vector<std::byte> result(size_t(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y)
        std::memcpy(result.data() + size_t(y) * width * 4,
                    bytes.data() + size_t(y) * rhi::TextureRowPitch(width), width * 4);
    return result;
}
void Compare(const std::vector<std::byte>& reference, const std::vector<std::byte>& indexed)
{
    Require(reference.size() == indexed.size(), "Indexed image size mismatch");
    uint32_t maximum = 0;
    double squared = 0;
    for (size_t i = 0; i < reference.size(); ++i)
    {
        const auto difference = std::abs(int(std::to_integer<uint8_t>(reference[i])) -
                                         int(std::to_integer<uint8_t>(indexed[i])));
        maximum = std::max(maximum, static_cast<uint32_t>(difference));
        squared += difference * difference;
    }
    const auto mse = squared / reference.size();
    std::cout << "Classic/indexed max=" << maximum << " MSE=" << mse << '\n';
    Require(maximum <= 2 && mse < .05, "Indexed scene differs from the classic material path");
}
void Scene(rhi::Runtime& runtime, shader::Target target, const std::filesystem::path& project,
           const std::filesystem::path& shaders, const std::filesystem::path& output)
{
    SceneRenderer renderer(runtime, target, project, shaders);
    auto& document = renderer.Document();
    const auto original = document;
    uint32_t sequence = 0;
    const auto check = [&](PipelineType pipeline, uint32_t width = 257, uint32_t height = 131)
    {
        auto color = runtime.CreateTexture(
            {width, height, rhi::Format::Rgba8Srgb, false, true, "Indexed.Scene"});
        SceneRenderOptions options;
        options.pipeline = pipeline;
        options.reflectionHistory = false;
        options.reflectionDenoise = false;
        options.reflectionTemporal = false;
        options.reflections = false;
        const auto reference =
            renderer.Render(color, rhi::State::Undefined, nullptr, true, {}, options);
        const auto direct = Pixels(runtime, reference, width, height);
        options.submission = GeometrySubmission::IndexedDirect;
        const auto indexedFrame =
            renderer.Render(color, rhi::State::CopySource, nullptr, true, {}, options);
        const auto indexed = Pixels(runtime, indexedFrame, width, height);
        options.submission = GeometrySubmission::CpuIndirect;
        const auto indirectFrame =
            renderer.Render(color, rhi::State::CopySource, nullptr, true,
                            output / (std::to_string(sequence++) + ".json"), options);
        const auto indirect = Pixels(runtime, indirectFrame, width, height);
        Require(indexed == indirect, "Indexed direct and CPU indirect pixels differ");
        Compare(direct, indexed);
        Require(indirectFrame.indexedScene.visible <= indirectFrame.indexedScene.instances &&
                    indirectFrame.indexedScene.textures <= IndexedTextureCapacity,
                "Indexed scene statistics violate capacity bounds");
        samples::SaveImage(output / (std::to_string(sequence) + ".bmp"),
                           indirectFrame.pixels->Map(), width, height, rhi::TextureRowPitch(width));
        return indirectFrame.indexedScene;
    };
    for (const auto pipeline : {PipelineType::Forward, PipelineType::Deferred})
        check(pipeline);
    if (project.stem() == "Pbr")
        check(PipelineType::RayTracing, 129, 73);
    // A mirrored transform exercises PSO winding and packed normal matrices.
    const auto firstMesh = std::ranges::find_if(document.objects, [](const auto& object)
                                                { return object.mesh.has_value(); });
    Require(firstMesh != document.objects.end(), "Fixture has no mesh");
    firstMesh->transform.scale.x *= -1;
    check(PipelineType::Forward, 213, 117);
    check(PipelineType::Deferred, 213, 117);
    document = original;
    for (auto& object : document.objects)
        if (object.mesh)
            object.transform.position.x += 100000;
    Require(check(PipelineType::Deferred).visible == 0, "Fully culled scene retained draws");
    for (auto& object : document.objects)
        if (object.mesh)
            object.enabled = false;
    Require(check(PipelineType::Forward).instances == 0, "Empty scene retained instance data");
    document = original;
    // Two graph submissions retain independent transforms, tables and upload pages.
    SceneRenderOptions options;
    options.submission = GeometrySubmission::CpuIndirect;
    auto colorA = runtime.CreateTexture({97, 73, rhi::Format::Rgba8Srgb, false, true, "Indexed.A"});
    auto colorB = runtime.CreateTexture({97, 73, rhi::Format::Rgba8Srgb, false, true, "Indexed.B"});
    const auto before = renderer.Render(colorA, rhi::State::Undefined, nullptr, true, {}, options);
    const auto referenceA = Pixels(runtime, before, 97, 73);
    document.MoveCamera({.3f, 0, 0}, {0, 3, 0});
    const auto after = renderer.Render(colorB, rhi::State::Undefined, nullptr, true, {}, options);
    const auto referenceB = Pixels(runtime, after, 97, 73);
    document = original;
    const auto pendingA =
        renderer.Render(colorA, rhi::State::CopySource, nullptr, true, {}, options);
    document.MoveCamera({.3f, 0, 0}, {0, 3, 0});
    const auto pendingB =
        renderer.Render(colorB, rhi::State::CopySource, nullptr, true, {}, options);
    Require(Pixels(runtime, pendingA, 97, 73) == referenceA &&
                Pixels(runtime, pendingB, 97, 73) == referenceB,
            "In-flight indexed scene versions contaminated each other");
    Require(pendingB.indexedScene.geometryUploads == 1,
            "Static packed geometry was uploaded on camera movement or submission-mode changes");
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 6, "Expected backend, PBR project, Shadow project, shaders and output");
        const std::filesystem::path output(argv[5]);
        const auto root = output / project::NewId();
        std::filesystem::create_directories(root);
        std::filesystem::copy(std::filesystem::path(argv[2]).parent_path() / "Assets",
                              root / "Assets", std::filesystem::copy_options::recursive);
        std::filesystem::copy_file(argv[2], root / "Pbr.rlproject");
        std::filesystem::copy_file(argv[3], root / "Shadow.rlproject");
        Logger logger(output / "indexed_scene.log");
        rhi::DeviceOptions options;
        options.backend =
            std::string(argv[1]) == "dx12" ? rhi::Backend::Dx12 : rhi::Backend::Vulkan;
        options.validation = rhi::ValidationMode::On;
        auto device = rhi::CreateDevice(options, logger);
        {
            auto runtime = device->CreateRuntime();
            runtime->SetAllocationBudget(1024ull * 1024 * 1024);
            for (const auto* scene : {"Pbr", "Shadow"})
            {
                const auto destination = output / scene;
                std::filesystem::create_directories(destination);
                Scene(*runtime,
                      options.backend == rhi::Backend::Dx12 ? shader::Target::Dxil
                                                            : shader::Target::Spirv,
                      root / (std::string(scene) + ".rlproject"), argv[4], destination);
            }
            runtime->WaitIdle();
            Require(runtime->GetMemoryStats().used == 0, "Indexed scene resources leaked");
        }
        device->PollDiagnostics();
        device->Shutdown();
        Require(device->GetValidationErrorCount() == 0, "Indexed scene validation errors");
        std::cout << "Indexed scene contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
