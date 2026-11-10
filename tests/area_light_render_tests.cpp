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
constexpr uint32_t Width = 40, Height = 40;
using Pixel = std::array<float, 3>;
using Image = std::vector<Pixel>;
void Require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}
double Luminance(const Pixel& value)
{
    return .2126 * value[0] + .7152 * value[1] + .0722 * value[2];
}
struct Capture
{
    Image total, direct, diffuse;
};
void Check(rhi::Runtime& runtime, shader::Target target, const std::filesystem::path& project,
           const std::filesystem::path& shaders)
{
    renderer::SceneRenderer renderer(runtime, target, project, shaders);
    const auto original = renderer.Document();
    auto targetImage = runtime.CreateTexture(
        {Width, Height, rhi::Format::Rgba8Srgb, false, true, "AreaLight.RenderValidation"});
    auto state = rhi::State::Undefined;
    renderer::SceneRenderOptions options;
    options.pipeline = renderer::PipelineType::PathTracing;
    options.reflectionDenoise = false;
    options.reflectionTemporal = false;
    const auto capture = [&](uint16_t seed, bool nee, uint32_t frames)
    {
        options.pathSeed = seed;
        options.pathAreaLightSampling = nee;
        renderer::SceneReadback frame;
        for (uint32_t i = 0; i < frames; ++i)
        {
            options.pathComponentDiagnostics = i + 1 == frames;
            frame = renderer.Render(targetImage, state, nullptr, false, {}, options);
            state = rhi::State::CopySource;
            runtime.Wait(frame.completion);
            if (i == 0)
                Require(frame.reflectionHistoryReset, "Reference stream did not reset");
        }
        Require(frame.reflectionSamples == frames * renderer::PathSamplesPerFrame,
                "Unexpected number of path samples");
        Capture result;
        const auto pitch = rhi::TextureRowPitch(Width, rhi::Format::Rgba32Float);
        std::array<Image*, 3> images{&result.total, &result.direct, &result.diffuse};
        for (size_t component = 0; component < images.size(); ++component)
        {
            Require(frame.pathComponents[component] != nullptr, "Missing HDR component readback");
            images[component]->resize(Width * Height);
            auto bytes = frame.pathComponents[component]->Map();
            for (uint32_t y = 0; y < Height; ++y)
                for (uint32_t x = 0; x < Width; ++x)
                {
                    std::array<float, 4> color{};
                    std::memcpy(color.data(), bytes.data() + y * pitch + 16 * x, 16);
                    for (size_t c = 0; c < 3; ++c)
                    {
                        Require(std::isfinite(color[c]) && color[c] >= -1e-5f,
                                "Invalid path component");
                        (*images[component])[y * Width + x][c] = color[c];
                    }
                }
        }
        for (size_t i = 0; i < result.total.size(); ++i)
            for (size_t c = 0; c < 3; ++c)
                Require(result.direct[i][c] + result.diffuse[i][c] <=
                            result.total[i][c] + 1e-4f * std::max(1.f, result.total[i][c]),
                        "Path components exceed total radiance");
        return result;
    };
    const auto mean = [](const Image& image, const Image& mask)
    {
        double sum = 0;
        size_t count = 0;
        for (size_t i = 0; i < image.size(); ++i)
            if (Luminance(mask[i]) < 5)
            {
                sum += Luminance(image[i]);
                ++count;
            }
        return sum / count;
    };
    const auto mse = [](const Image& image, const Image& reference)
    {
        double sum = 0;
        size_t count = 0;
        for (size_t i = 0; i < image.size(); ++i)
            if (Luminance(reference[i]) < 5)
                for (size_t c = 0; c < 3; ++c)
                {
                    const double difference = image[i][c] - reference[i][c];
                    sum += difference * difference;
                    ++count;
                }
        return sum / count;
    };
    const auto reference = capture(101, true, 512);
    const auto bsdfReference = capture(102, false, 512);
    const double referenceMean = mean(reference.total, reference.total);
    const double bsdfMean = mean(bsdfReference.total, reference.total);
    std::cout << "HDR reference mean: NEE=" << referenceMean << " BSDF=" << bsdfMean << '\n';
    Require(referenceMean > .01 && std::abs(referenceMean - bsdfMean) / referenceMean < .03,
            "Area-light NEE/MIS and BSDF-only energy disagree");
    double neeError = 0, bsdfError = 0;
    for (uint16_t seed = 201; seed < 205; ++seed)
    {
        const auto nee = capture(seed, true, 8);
        const auto bsdf = capture(seed, false, 8);
        neeError += mse(nee.total, reference.total);
        bsdfError += mse(bsdf.total, reference.total);
    }
    std::cout << "Four-seed 64 spp HDR MSE: NEE=" << neeError / 4 << " BSDF=" << bsdfError / 4
              << '\n';
    Require(neeError < bsdfError, "Area-light sampling failed to reduce fixed-scene HDR MSE");
    auto& doc = renderer.Document();
    scene::Object blocker;
    blocker.id = "e1f0a5640d4d41f39dd49fa3b4e96abb";
    blocker.name = "area-light-validation-blocker";
    blocker.transform.position = {0, 2.84f, -.1f};
    blocker.transform.scale = {1.5f, .04f, 1.5f};
    blocker.mesh = scene::MeshRenderer{"b4748e75e72a5dba8d4439fa86854c92"};
    doc.objects.push_back(blocker);
    const auto blocked = capture(301, true, 32);
    const double blockedMean = mean(blocked.total, reference.total);
    std::cout << "Blocked HDR mean=" << blockedMean << '\n';
    Require(blockedMean < referenceMean * .02, "Opaque blocker did not occlude the area light");
    doc = original;
    auto& light =
        *std::ranges::find_if(doc.objects, [](const auto& o) { return o.areaLight.has_value(); });
    light.areaLight->radiance = {};
    const auto dark = capture(302, true, 8);
    Require(mean(dark.total, reference.total) < 1e-6, "Zero-radiance light emitted energy");

    doc = original;
    auto lightIt =
        std::ranges::find_if(doc.objects, [](const auto& o) { return o.areaLight.has_value(); });
    lightIt->transform.position.x = -.65f;
    lightIt->areaLight->width = .4f;
    lightIt->areaLight->height = .4f;
    auto otherLight = *lightIt;
    otherLight.id = "e2f0a5640d4d41f39dd49fa3b4e96abb";
    otherLight.transform.position.x = .65f;
    otherLight.areaLight->enabled = false;
    doc.objects.push_back(otherLight);
    const auto oneLight = capture(401, true, 128);
    doc.Get(otherLight.id).areaLight->enabled = true;
    const auto twoLights = capture(402, true, 128);
    const double oneMean = mean(oneLight.total, reference.total);
    const double twoMean = mean(twoLights.total, reference.total);
    std::cout << "Multi-light HDR mean: one=" << oneMean << " two=" << twoMean << '\n';
    Require(oneMean > 0 && twoMean > 1.45 * oneMean && twoMean < 2.7 * oneMean,
            "Uniform light selection lost or duplicated multi-light energy");

    doc = original;
    auto& cameraLight =
        *std::ranges::find_if(doc.objects, [](const auto& o) { return o.areaLight.has_value(); });
    cameraLight.transform.position = {0, 1.5f, 4};
    cameraLight.transform.rotation = {0, 1, 0, 0};
    cameraLight.areaLight->radiance = {2, 1, .5f};
    cameraLight.areaLight->width = cameraLight.areaLight->height = .8f;
    const auto front = capture(501, true, 1);
    const auto index = (Height / 2) * Width + Width / 2;
    Require(std::abs(front.direct[index][0] - 2) < 1e-5f &&
                std::abs(front.direct[index][1] - 1) < 1e-5f &&
                std::abs(front.direct[index][2] - .5f) < 1e-5f,
            "Camera-visible emitter was not recorded as primary direct radiance");
    cameraLight.transform.rotation = {};
    const auto back = capture(502, true, 1);
    Require(Luminance(back.total[index]) < 1e-6,
            "One-sided emitter back face did not remain dark and opaque");

    doc = original;
    std::erase_if(doc.objects, [](const auto& o) { return o.name == "ceiling"; });
    const auto lightId =
        std::ranges::find_if(doc.objects, [](const auto& o) { return o.areaLight.has_value(); })
            ->id;
    auto& blockerLight = doc.Get(lightId);
    blockerLight.areaLight->radiance = {};
    blockerLight.areaLight->width = blockerLight.areaLight->height = 3;
    blockerLight.areaLight->enabled = false;
    scene::Object sun;
    sun.id = "e3f0a5640d4d41f39dd49fa3b4e96abb";
    sun.transform.rotation = {-.70710678f, 0, 0, .70710678f};
    sun.light = scene::DirectionalLight{};
    sun.light->intensity = 3;
    sun.light->shadows = true;
    doc.objects.push_back(sun);
    const auto unobstructedSun = capture(601, true, 64);
    doc.Get(lightId).areaLight->enabled = true;
    const auto obstructedSun = capture(602, true, 64);
    const auto sunMean = mean(unobstructedSun.total, reference.total);
    const auto shadowMean = mean(obstructedSun.total, reference.total);
    std::cout << "Directional-light HDR mean: open=" << sunMean << " blocked=" << shadowMean
              << '\n';
    Require(sunMean > .01 && shadowMean < .4 * sunMean,
            "Analytic area light failed to block directional shadow rays");
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
        Logger logger(std::filesystem::path(argv[4]) / "area-light-render.log");
        rhi::DeviceOptions options;
        options.backend = backend == "dx12" ? rhi::Backend::Dx12 : rhi::Backend::Vulkan;
        options.validation = rhi::ValidationMode::On;
        auto device = rhi::CreateDevice(options, logger);
        {
            auto runtime = device->CreateRuntime();
            Check(*runtime, backend == "dx12" ? shader::Target::Dxil : shader::Target::Spirv,
                  argv[2], argv[3]);
            runtime->WaitIdle();
            Require(runtime->GetMemoryStats().used == 0, "Area-light render leaked resources");
        }
        device->PollDiagnostics();
        device->Shutdown();
        Require(device->GetValidationErrorCount() == 0, "Area-light render validation errors");
        std::cout << "Area-light render contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
