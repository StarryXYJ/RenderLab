#include "core/log.hpp"
#include "renderer/raytracing.hpp"
#include "renderer/scene_renderer.hpp"
#include "rhi/device.hpp"
#include "rhi/runtime.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
using namespace renderlab;
constexpr uint32_t Width = 80, Height = 80;
void Require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}
void Check(rhi::Runtime& runtime, shader::Target target, const std::filesystem::path& project,
           const std::filesystem::path& shaders)
{
    renderer::SceneRenderer renderer(runtime, target, project, shaders);
    auto& document = renderer.Document();
    const auto originalDocument = document;
    auto light = std::ranges::find_if(document.objects,
                                      [](const auto& o) { return o.areaLight.has_value(); });
    Require(light != document.objects.end(), "Missing area light");
    const auto lightId = light->id;
    auto targetImage = runtime.CreateTexture(
        {Width, Height, rhi::Format::Rgba8Srgb, false, true, "RasterArea.Test"});
    auto state = rhi::State::Undefined;
    const auto capture =
        [&](renderer::PipelineType pipeline, renderer::GeometrySubmission submission)
    {
        renderer::SceneRenderOptions options;
        options.pipeline = pipeline;
        options.submission = submission;
        options.reflections = false;
        auto frame = renderer.Render(targetImage, state, nullptr, true, {}, options);
        state = rhi::State::CopySource;
        runtime.Wait(frame.completion);
        auto source = frame.pixels->Map();
        std::vector<uint8_t> pixels(Width * Height * 4);
        for (uint32_t y = 0; y < Height; ++y)
            std::memcpy(pixels.data() + y * Width * 4,
                        source.data() + y * rhi::TextureRowPitch(Width), Width * 4);
        return pixels;
    };
    using P = renderer::PipelineType;
    using S = renderer::GeometrySubmission;
    const auto compare = [&](const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
    {
        double squared = 0;
        for (size_t i = 0; i < a.size(); ++i)
            squared += std::pow(double(a[i]) - b[i], 2);
        const double mse = squared / a.size();
        std::cout << "Raster comparison MSE=" << mse << '\n';
        Require(mse < .15, "Raster area-light paths differ");
    };
    const auto forward = capture(P::Forward, S::Direct);
    compare(forward, capture(P::Deferred, S::Direct));
    compare(forward, capture(P::Forward, S::IndexedDirect));
    compare(forward, capture(P::Forward, S::GpuCulled));
    if (runtime.SupportsAccelerationStructures() && runtime.SupportsRayQuery() &&
        runtime.SupportsRayTracingPipeline())
    {
        for (auto& object : document.objects)
            if (object.name == "diffuse-sphere" || object.name == "metal-sphere" ||
                object.name == "box")
                object.enabled = false;
        const auto emptyRaster = capture(P::Forward, S::Direct);
        const auto emptyRay = capture(P::RayTracing, S::Direct);
        double emptyError = 0;
        size_t emptyChannels = 0;
        // Avoid the floor/wall intersection, where a single center shadow map remains approximate.
        for (uint32_t y = 30; y < 75; ++y)
            for (uint32_t x = 20; x < 60; ++x)
                if (y < 55 || y >= 70)
                    for (size_t c = 0; c < 3; ++c)
                    {
                        const size_t i = (y * Width + x) * 4 + c;
                        emptyError += std::pow(double(emptyRaster[i]) - emptyRay[i], 2);
                        ++emptyChannels;
                    }
        std::cout << "Empty-room raster/Ray Query MSE=" << emptyError / emptyChannels << '\n';
        Require(emptyError / emptyChannels < 15, "Area shadow map darkened an unobstructed room");
        document = originalDocument;
    }
    else
        std::cout << "Skipped empty-room Ray Query comparison: ray tracing unavailable\n";
    scene::Object shadowBlocker;
    shadowBlocker.id = "e4f0a5640d4d41f39dd49fa3b4e96abb";
    shadowBlocker.transform.position = {0, 2.84f, -.1f};
    shadowBlocker.transform.scale = {1.5f, .04f, 1.5f};
    shadowBlocker.mesh = scene::MeshRenderer{"b4748e75e72a5dba8d4439fa86854c92"};
    document.objects.push_back(shadowBlocker);
    const auto shadowed = capture(P::Forward, S::Direct);
    compare(shadowed, capture(P::Deferred, S::Direct));
    compare(shadowed, capture(P::Forward, S::IndexedDirect));
    uint64_t openSum = 0, shadowSum = 0;
    for (size_t i = 0; i < forward.size(); i += 4)
        for (size_t c = 0; c < 3; ++c)
        {
            openSum += forward[i + c];
            shadowSum += shadowed[i + c];
        }
    std::cout << "Raster area-shadow sum: open=" << openSum << " blocked=" << shadowSum << '\n';
    Require(shadowSum * 2 < openSum, "Area shadow map did not block light");
    document.objects.pop_back();
    auto& splitFirst = document.Get(lightId);
    const auto originalFirst = splitFirst;
    splitFirst.transform.position.x = -.65f;
    splitFirst.areaLight->width = splitFirst.areaLight->height = .4f;
    auto splitSecond = splitFirst;
    splitSecond.id = "e5f0a5640d4d41f39dd49fa3b4e96abb";
    splitSecond.transform.position.x = .65f;
    document.objects.push_back(splitSecond);
    shadowBlocker.transform.position.x = -.65f;
    shadowBlocker.transform.scale = {.55f, .04f, .55f};
    document.objects.push_back(shadowBlocker);
    const auto splitBoth = capture(P::Forward, S::Direct);
    compare(splitBoth, capture(P::Deferred, S::Direct));
    document.Get(splitSecond.id).areaLight->enabled = false;
    const auto splitBlocked = capture(P::Forward, S::Direct);
    document.Get(lightId).areaLight->enabled = false;
    document.Get(splitSecond.id).areaLight->enabled = true;
    const auto splitOpen = capture(P::Forward, S::Direct);
    uint64_t blockedSum = 0, secondSum = 0;
    for (size_t i = 0; i < splitOpen.size(); i += 4)
        for (size_t c = 0; c < 3; ++c)
        {
            blockedSum += splitBlocked[i + c];
            secondSum += splitOpen[i + c];
        }
    Require(secondSum > blockedSum + 1000, "Raster area shadows did not separate light visibility");
    document.objects.pop_back();
    document.objects.pop_back();
    document.Get(lightId) = originalFirst;
    const auto first = document.Get(lightId);
    auto second = first;
    second.id = "e2f0a5640d4d41f39dd49fa3b4e96abb";
    second.transform.position.x += .2f;
    document.objects.push_back(second);
    const auto two = capture(P::Forward, S::Direct);
    compare(two, capture(P::Deferred, S::Direct));
    compare(two, capture(P::Forward, S::IndexedDirect));
    double added = 0;
    for (size_t i = 0; i < two.size(); i += 4)
        for (size_t c = 0; c < 3; ++c)
            added += double(two[i + c]) - forward[i + c];
    Require(added > 1000, "Second area light contributed no energy");
    auto third = second;
    third.id = "e3f0a5640d4d41f39dd49fa3b4e96abb";
    document.objects.push_back(third);
    bool rejected = false;
    try
    {
        renderer.ValidatePipeline(P::Forward);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Require(rejected, "Area light limit not validated in Forward");
    document.objects.pop_back();
    document.objects.pop_back();
    document.Get(lightId).areaLight->radiance = {};
    const auto dark = capture(P::Forward, S::Direct);
    double energy = 0;
    for (size_t i = 0; i < forward.size(); i += 4)
        for (size_t c = 0; c < 3; ++c)
            energy += double(forward[i + c]) - dark[i + c];
    std::cout << "Raster light energy difference=" << energy << '\n';
    Require(energy > 1000, "Area light contributed no visible illumination");
    compare(dark, capture(P::Deferred, S::Direct));
    document.Get(lightId).areaLight->enabled = false;
    compare(dark, capture(P::Forward, S::Direct));

    document.ambient = {.4f, .4f, .4f};
    auto& emitter = document.Get(lightId);
    emitter.areaLight->enabled = true;
    emitter.transform.position = {0, 1.5f, 4};
    emitter.transform.rotation = {0, 1, 0, 0};
    emitter.areaLight->radiance = {2, 1, .5f};
    emitter.areaLight->width = emitter.areaLight->height = .8f;
    const auto visible = capture(P::Forward, S::Direct);
    compare(visible, capture(P::Deferred, S::Direct));
    compare(visible, capture(P::Forward, S::IndexedDirect));
    const size_t center = (Height / 2 * Width + Width / 2) * 4;
    std::cout << "Visible center RGB=" << int(visible[center]) << "," << int(visible[center + 1])
              << "," << int(visible[center + 2]) << '\n';
    Require(visible[center] > visible[center + 1] && visible[center + 1] > visible[center + 2] &&
                visible[center + 2] > 40,
            "Visible area-light radiance or facing is incorrect");
    emitter.transform.rotation = {};
    const auto back = capture(P::Forward, S::Direct);
    Require(back[center] < 3 && back[center + 1] < 3 && back[center + 2] < 3,
            "Area-light back face did not occlude as black");
    compare(back, capture(P::Deferred, S::Direct));
    emitter.areaLight->enabled = false;
    const auto behind = capture(P::Forward, S::Direct);
    Require(behind[center] > 20, "Back face test lacks visible background geometry");
    emitter.areaLight->enabled = true;
    emitter.transform.rotation = {0, 1, 0, 0};
    scene::Object blocker;
    blocker.id = "e1f0a5640d4d41f39dd49fa3b4e96abb";
    blocker.transform.position = {0, 1.5f, 5};
    blocker.transform.scale = {1, 1, .1f};
    blocker.mesh = scene::MeshRenderer{"b4748e75e72a5dba8d4439fa86854c92"};
    document.objects.push_back(blocker);
    const auto hidden = capture(P::Forward, S::Direct);
    Require(hidden[center] + 30 < visible[center], "Geometry failed to occlude visible area light");
    compare(hidden, capture(P::Deferred, S::Direct));
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 5, "Expected backend, project, shader directory and output");
        const std::string backend = argv[1];
        Require(backend == "dx12" || backend == "vulkan", "Invalid backend");
        std::filesystem::create_directories(argv[4]);
        Logger logger(std::filesystem::path(argv[4]) / "raster-area-light.log");
        rhi::DeviceOptions options;
        options.backend = backend == "dx12" ? rhi::Backend::Dx12 : rhi::Backend::Vulkan;
        options.validation = rhi::ValidationMode::On;
        auto device = rhi::CreateDevice(options, logger);
        {
            auto runtime = device->CreateRuntime();
            Check(*runtime, backend == "dx12" ? shader::Target::Dxil : shader::Target::Spirv,
                  argv[2], argv[3]);
            runtime->WaitIdle();
            Require(runtime->GetMemoryStats().used == 0,
                    "Raster area-light render leaked resources");
        }
        device->PollDiagnostics();
        device->Shutdown();
        Require(device->GetValidationErrorCount() == 0, "Raster area-light validation errors");
        std::cout << "Raster area-light contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
