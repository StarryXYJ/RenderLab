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
    {
        auto& document = renderer.Document();
        auto light = std::ranges::find_if(document.objects, [](const auto& object)
                                          { return object.areaLight.has_value(); });
        Require(light != document.objects.end(), "Identity fixture needs an area light");
        const auto lightId = light->id;
        for (auto& object : document.objects)
            if (object.mesh || (object.areaLight && object.id != lightId))
                object.enabled = false;
        auto& camera = document.Get(document.activeCamera);
        camera.transform.position = {0, 0, 3};
        camera.transform.rotation = {0, 0, 0, 1};
        auto& emitter = document.Get(lightId);
        emitter.enabled = true;
        emitter.transform.position = {0, 0, 0};
        emitter.transform.rotation = {0, 1, 0, 0};
        emitter.transform.scale = {1, 1, 1};
        emitter.areaLight->width = 2;
        emitter.areaLight->height = 2;
        document.environmentIntensity = 0;
        options.pathIdentityDiagnostics = true;
        options.pathComponentDiagnostics = true;
        const auto frame = renderer.Render(targetImage, state, nullptr, false, {}, options);
        state = rhi::State::CopySource;
        runtime.Wait(frame.completion);
        Require(frame.pathPrimaryIdentity && frame.pathPrimarySurface && frame.pathComponents[1],
                "Missing primary identity fixture readback");
        const auto pitch = rhi::TextureRowPitch(Width, rhi::Format::Rgba32Float);
        const auto get = [&](const std::shared_ptr<rhi::Buffer>& buffer, uint32_t x, uint32_t y)
        {
            std::array<float, 4> value{};
            std::memcpy(value.data(), buffer->Map().data() + y * pitch + x * 16, 16);
            return value;
        };
        const auto center = get(frame.pathPrimaryIdentity, Width / 2, Height / 2);
        const auto corner = get(frame.pathPrimaryIdentity, 0, 0);
        const auto surface = get(frame.pathPrimarySurface, Width / 2, Height / 2);
        const auto direct = get(frame.pathComponents[1], Width / 2, Height / 2);
        Require(center[0] == 2 && center[1] == 0 && center[3] > 3 && center[3] < 3.1f &&
                    surface[3] == 0 && direct[0] > 0,
                "Primary identity did not identify the visible analytic emitter");
        Require(corner[0] == 0 && corner[3] == 10000,
                "Primary identity did not identify an environment miss");
        options.pathIdentityDiagnostics = false;
        options.pathComponentDiagnostics = false;
        renderer.Document() = original;
    }
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
    const auto rasterCapture = [&](renderer::PipelineType pipeline)
    {
        options.pipeline = pipeline;
        options.pathComponentDiagnostics = false;
        options.sceneHdrDiagnostics = true;
        options.reflections = false;
        auto frame = renderer.Render(targetImage, state, nullptr, false, {}, options);
        state = rhi::State::CopySource;
        runtime.Wait(frame.completion);
        Require(frame.sceneHdr != nullptr, "Missing scene HDR readback");
        const auto pixels = frame.sceneHdr->Map();
        const auto pitch = rhi::TextureRowPitch(Width, rhi::Format::Rgba32Float);
        Image image(Width * Height);
        for (uint32_t y = 0; y < Height; ++y)
            for (uint32_t x = 0; x < Width; ++x)
            {
                std::array<float, 4> value{};
                std::memcpy(value.data(), pixels.data() + y * pitch + 16 * x, 16);
                for (size_t c = 0; c < 3; ++c)
                {
                    Require(std::isfinite(value[c]) && value[c] >= -1e-5f, "Invalid scene HDR");
                    image[y * Width + x][c] = value[c];
                }
            }
        return image;
    };
    const auto raster = rasterCapture(renderer::PipelineType::Deferred);
    const auto hybrid = rasterCapture(renderer::PipelineType::RayTracing);
    const double directMean = mean(reference.direct, reference.total);
    const double rasterMean = mean(raster, reference.total);
    const double hybridMean = mean(hybrid, reference.total);
    std::cout << "Direct HDR means: path=" << directMean << " raster=" << rasterMean
              << " hybrid=" << hybridMean << " MSE=" << mse(raster, reference.direct) << "/"
              << mse(hybrid, reference.direct) << '\n';
    double rasterInterior = 0, hybridInterior = 0;
    size_t interiorChannels = 0;
    for (uint32_t y = 1; y + 1 < Height; ++y)
        for (uint32_t x = 1; x + 1 < Width; ++x)
        {
            const size_t i = y * Width + x;
            const double center = Luminance(reference.direct[i]);
            if (center > 2 || Luminance(reference.total[i]) >= 5)
                continue;
            bool stable = true;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    stable &= std::abs(Luminance(reference.direct[(y + dy) * Width + x + dx]) -
                                       center) < .08;
            if (!stable)
                continue;
            for (size_t c = 0; c < 3; ++c)
            {
                rasterInterior += std::pow(double(raster[i][c]) - reference.direct[i][c], 2);
                hybridInterior += std::pow(double(hybrid[i][c]) - reference.direct[i][c], 2);
                ++interiorChannels;
            }
        }
    Require(interiorChannels > 300, "HDR reference has insufficient interior samples");
    std::cout << "Direct interior HDR MSE: raster=" << rasterInterior / interiorChannels
              << " hybrid=" << hybridInterior / interiorChannels << " channels=" << interiorChannels
              << '\n';
    Require(rasterInterior / interiorChannels < .005 && hybridInterior / interiorChannels < .002,
            "Interior area-light HDR error exceeded the finite-scene tolerance");
    Require(directMean > .001 && rasterMean > .7 * directMean && rasterMean < 1.3 * directMean &&
                hybridMean > .7 * directMean && hybridMean < 1.3 * directMean,
            "Area-light direct HDR energy differs substantially from path tracing");
    options.pipeline = renderer::PipelineType::PathTracing;
    options.sceneHdrDiagnostics = false;
    options.reflections = true;
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
    const auto twoRaster = rasterCapture(renderer::PipelineType::Deferred);
    const auto twoHybrid = rasterCapture(renderer::PipelineType::RayTracing);
    const double twoPathDirect = mean(twoLights.direct, twoLights.total);
    const double twoRasterDirect = mean(twoRaster, twoLights.total);
    const double twoHybridDirect = mean(twoHybrid, twoLights.total);
    std::cout << "Two-light direct HDR means: path=" << twoPathDirect
              << " raster=" << twoRasterDirect << " hybrid=" << twoHybridDirect << '\n';
    Require(twoPathDirect > .001 && twoRasterDirect > .6 * twoPathDirect &&
                twoRasterDirect < 1.5 * twoPathDirect && twoHybridDirect > .6 * twoPathDirect &&
                twoHybridDirect < 1.5 * twoPathDirect,
            "Two-light direct HDR energy differs substantially from path tracing");
    options.pipeline = renderer::PipelineType::PathTracing;
    options.sceneHdrDiagnostics = false;
    options.reflections = true;

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

    doc = original;
    for (auto& object : doc.objects)
        if (object.areaLight)
            object.areaLight->enabled = false;
    scene::Object point;
    point.id = "e4f0a5640d4d41f39dd49fa3b4e96abb";
    point.transform.position = {0, 2.95f, -.1f};
    point.pointLight = scene::PointLight{{1, 1, 1}, 20, 10, true};
    doc.objects.push_back(point);
    options.pipeline = renderer::PipelineType::PathTracing;
    options.sceneHdrDiagnostics = false;
    options.reflections = true;
    const auto pointOpen = capture(701, true, 64);
    doc.Get(point.id).pointLight->intensity = 40;
    const auto pointDouble = capture(701, true, 64);
    const auto pointMean = mean(pointOpen.direct, reference.total);
    const auto doubleMean = mean(pointDouble.direct, reference.total);
    Require(pointMean > .01 && std::abs(doubleMean / pointMean - 2) < .001,
            "Point-light direct energy did not scale with intensity");
    doc.Get(point.id).pointLight->intensity = 20;
    doc.Get(point.id).pointLight->range = .01f;
    const auto pointOutOfRange = capture(701, true, 16);
    Require(mean(pointOutOfRange.direct, reference.total) < 1e-6,
            "Point light emitted outside its range");
    doc.Get(point.id).pointLight->range = 10;
    const auto pointHybridOpen = rasterCapture(renderer::PipelineType::RayTracing);
    doc.objects.push_back(blocker);
    options.pipeline = renderer::PipelineType::PathTracing;
    options.sceneHdrDiagnostics = false;
    options.reflections = true;
    const auto pointBlocked = capture(702, true, 64);
    const auto pointHybridBlocked = rasterCapture(renderer::PipelineType::RayTracing);
    const double pointOpenMean = mean(pointOpen.direct, reference.total);
    const double pointBlockedMean = mean(pointBlocked.direct, reference.total);
    const double hybridOpenMean = mean(pointHybridOpen, reference.total);
    const double hybridBlockedMean = mean(pointHybridBlocked, reference.total);
    std::cout << "Point-light direct HDR mean: path=" << pointOpenMean << "/" << pointBlockedMean
              << " hybrid=" << hybridOpenMean << "/" << hybridBlockedMean << '\n';
    Require(pointOpenMean > .01 && pointBlockedMean < pointOpenMean * .15 &&
                hybridOpenMean > .01 && hybridBlockedMean < hybridOpenMean * .15,
            "Point-light blocker did not suppress path and hybrid direct lighting");
    for (int i = 0; i < 7; ++i)
    {
        auto extra = point;
        extra.id = "d000000000000000000000000000000" + std::to_string(i);
        extra.pointLight->intensity = 0;
        doc.objects.push_back(extra);
    }
    const auto lastBitBlocked = rasterCapture(renderer::PipelineType::RayTracing);
    std::erase_if(doc.objects, [&](const auto& object) { return object.id == blocker.id; });
    const auto lastBitOpen = rasterCapture(renderer::PipelineType::RayTracing);
    const auto lastOpenMean = mean(lastBitOpen, reference.total);
    const auto lastBlockedMean = mean(lastBitBlocked, reference.total);
    Require(lastOpenMean > .01 && lastBlockedMean < lastOpenMean * .15 &&
                std::abs(lastOpenMean - hybridOpenMean) < hybridOpenMean * .01,
            "Eighth point-light visibility bit did not match the first light");
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
