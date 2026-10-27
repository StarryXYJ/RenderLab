#include "samples/scene/reflection_temporal_motion.hpp"

#include "core/file_io.hpp"
#include "core/log.hpp"
#include "rhi/runtime.hpp"
#include "rhi/upload.hpp"
#include "samples/common/image_io.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace renderlab::samples
{
void VerifyReflectionTemporalMotion(rhi::Runtime& runtime, renderer::SceneRenderer& renderer,
                                    std::shared_ptr<rhi::Texture> color, rhi::State& initial,
                                    const std::filesystem::path& output, Logger& logger)
{
#ifdef NDEBUG
    constexpr uint32_t referenceSpp = 256, endpointSpp = 2048;
#else
    // Debug exercises validation/lifetimes; full image-quality references are captured in Release.
    constexpr uint32_t referenceSpp = 32, endpointSpp = 128;
#endif
    const auto original = renderer.Document();
    auto fixture = original;
    for (auto& object : fixture.objects)
        if (object.light)
            object.light->intensity *= 4;
    renderer::SceneRenderOptions options;
    options.pipeline = renderer::PipelineType::RayTracing;
    options.shadowResolution = 128;
    options.profile = true;
    const auto render = [&](bool readback = false, const std::string& name = "")
    {
        auto frame = renderer.Render(
            color, initial, nullptr, readback,
            name.empty() ? std::filesystem::path{} : output / (name + "-plan.json"), options);
        initial = rhi::State::CopySource;
        runtime.Wait(frame.completion);
        return frame;
    };
    const auto reset = [&](const scene::Scene& document)
    {
        renderer.Document() = document;
        options.reflections = false;
        render();
        options.reflections = true;
    };
    const auto copyPixels = [&](const renderer::SceneReadback& frame, const std::string& name)
    {
        auto pixels = frame.pixels->Map();
        std::filesystem::create_directories((output / name).parent_path());
        SaveImage(output / (name + ".bmp"), pixels, color->desc.width, color->desc.height,
                  rhi::TextureRowPitch(color->desc.width));
        return std::vector<std::byte>(pixels.begin(), pixels.end());
    };
    std::string json = "{\n  \"referenceSpp\": " + std::to_string(endpointSpp) +
                       ",\n  \"lightIntensityMultiplier\": 4,\n  \"cases\": [\n";
    for (uint32_t trajectory = 0; trajectory < 3; ++trajectory)
    {
        const std::string name = trajectory == 0 ? "gentle" : trajectory == 1 ? "fast" : "reveal";
        std::array<std::vector<std::byte>, 3> images;
        constexpr uint32_t motionFrames = 16, sequenceFrames = 24;
        std::array<std::vector<std::vector<std::byte>>, 3> sequences;
        std::vector<scene::Scene> poses;
        double acceptedPixels = 0, validPixels = 0, historyWeight = 0, virtualPixels = 0;
        double temporalCost = 0;
        for (uint32_t mode = 0; mode < 3; ++mode)
        {
            const std::string label = mode == 0 ? "raw-final" : mode == 1 ? "spatial" : "temporal";
            reset(fixture);
            options.reflectionDenoise = mode != 0;
            options.reflectionTemporal = mode == 2;
            options.reflectionDiagnostics = mode == 2;
            for (uint32_t warmup = 0; warmup < 8; ++warmup)
                render();
            for (uint32_t frame = 0; frame < sequenceFrames; ++frame)
            {
                if (frame < motionFrames)
                    renderer.Document().MoveCamera({trajectory == 0   ? .02f
                                                    : trajectory == 1 ? .08f
                                                                      : .12f,
                                                    0, 0},
                                                   {0,
                                                    trajectory == 0   ? .15f
                                                    : trajectory == 1 ? .8f
                                                                      : -.6f,
                                                    0});
                if (mode == 0)
                    poses.push_back(renderer.Document());
                auto result = render(true, frame == 15 ? "motion-" + name + "-" + label : "");
                if (result.reflectionSamples != (frame < motionFrames ? 1 : frame - 14) ||
                    result.reflectionTemporalFrames != (mode == 2 ? frame + 9 : 0))
                    throw std::runtime_error("Moving camera lost the independent temporal source");
                if (mode == 2)
                    for (const auto& pass : result.profile.Read(runtime))
                        if (pass.name == "RayTracing.Reflection.Temporal")
                            temporalCost += pass.nanoseconds / 1e6 / sequenceFrames;
                sequences[mode].push_back(
                    copyPixels(result, std::format("sequence/{}/{}/{:02}", name, label, frame)));
                if (result.reflectionDiagnostics)
                {
                    const auto bytes = result.reflectionDiagnostics->Map();
                    const auto diagnosticPitch =
                        rhi::TextureRowPitch(color->desc.width, rhi::Format::Rgba32Float);
                    for (uint32_t y = 0; y < color->desc.height; ++y)
                        for (uint32_t x = 0; x < color->desc.width; ++x)
                        {
                            std::array<float, 4> pixel;
                            std::memcpy(pixel.data(), bytes.data() + y * diagnosticPitch + x * 16,
                                        sizeof(pixel));
                            if (pixel[1] < 1)
                                continue;
                            ++validPixels;
                            acceptedPixels += pixel[0] > 0;
                            virtualPixels += pixel[2] == 2;
                            historyWeight += pixel[0];
                        }
                }
                if (frame == 15)
                    images[mode] = copyPixels(result, "motion-" + name + "-" + label);
            }
        }
        const auto endpoint = renderer.Document();
        reset(endpoint);
        options.reflectionDenoise = false;
        options.reflectionTemporal = false;
        options.profile = false;
        options.reflectionDiagnostics = false;
        std::vector<std::byte> reference;
        for (uint32_t frame = 0; frame < endpointSpp; ++frame)
        {
            const auto result = render(frame + 1 == endpointSpp);
            if (result.reflectionSamples != frame + 1)
                throw std::runtime_error("Stationary endpoint reference did not accumulate");
            if (frame + 1 == endpointSpp)
                reference =
                    copyPixels(result, std::format("motion-{}-reference-{}", name, endpointSpp));
        }
        std::vector<std::vector<std::byte>> references;
        for (uint32_t frame = 0; frame < sequenceFrames; ++frame)
        {
            if (frame >= motionFrames)
            {
                references.push_back(references.back());
                continue;
            }
            reset(poses[frame]);
            for (uint32_t sample = 0; sample < referenceSpp; ++sample)
            {
                const auto result = render(sample + 1 == referenceSpp);
                if (sample + 1 == referenceSpp)
                    references.push_back(copyPixels(
                        result, std::format("sequence/{}/reference/{:02}", name, frame)));
            }
        }
        std::array<double, 3> sequenceError{}, flicker{}, signedError{}, peakError{};
        std::string frameRecords;
        const auto rowPitch = rhi::TextureRowPitch(color->desc.width);
        const double components = double(color->desc.width) * color->desc.height * 3;
        for (uint32_t frame = 0; frame < sequenceFrames; ++frame)
        {
            std::array<double, 3> frameError{}, frameDelta{};
            for (uint32_t y = 0; y < color->desc.height; ++y)
                for (uint32_t x = 0; x < color->desc.width; ++x)
                    for (uint32_t channel = 0; channel < 3; ++channel)
                    {
                        const auto pixel = y * rowPitch + x * 4 + channel;
                        for (uint32_t mode = 0; mode < 3; ++mode)
                        {
                            const double delta =
                                int(std::to_integer<uint8_t>(sequences[mode][frame][pixel])) -
                                int(std::to_integer<uint8_t>(references[frame][pixel]));
                            frameError[mode] += delta * delta / components;
                            signedError[mode] += delta / components / sequenceFrames;
                            peakError[mode] += (delta > 16 ? 1. : 0.) / components / sequenceFrames;
                            if (frame == 0)
                                continue;
                            const double oldDelta =
                                int(std::to_integer<uint8_t>(sequences[mode][frame - 1][pixel])) -
                                int(std::to_integer<uint8_t>(references[frame - 1][pixel]));
                            frameDelta[mode] +=
                                (delta - oldDelta) * (delta - oldDelta) / components;
                        }
                    }
            for (uint32_t mode = 0; mode < 3; ++mode)
            {
                sequenceError[mode] += frameError[mode] / sequenceFrames;
                flicker[mode] += frameDelta[mode] / (sequenceFrames - 1);
            }
            frameRecords += std::format("{}{{\"frame\":{},\"mse\":[{:.6f},{:.6f},{:.6f}],"
                                        "\"residualDeltaMse\":[{:.6f},{:.6f},{:.6f}]}}",
                                        frame ? "," : "", frame, frameError[0], frameError[1],
                                        frameError[2], frameDelta[0], frameDelta[1], frameDelta[2]);
        }
        const auto arrayJson = [](const std::array<double, 3>& values)
        { return std::format("[{:.8f},{:.8f},{:.8f}]", values[0], values[1], values[2]); };
        const auto sequenceRecord = std::format(
            "{{\"trajectory\":\"{}\",\"referenceSpp\":{},\"motionFrames\":{},\"stopFrames\":8,"
            "\"modeOrder\":[\"unfiltered\",\"spatial\",\"temporal\"],\"meanMse\":{},"
            "\"residualDeltaMse\":{},\"meanSignedError\":{},\"positiveErrorOver16Fraction\":{},"
            "\"historyAcceptance\":{:.6f},\"meanHistoryWeight\":{:.6f},\"virtualFraction\":{:.6f},"
            "\"frames\":[{}]}}\n",
            name, referenceSpp, motionFrames, arrayJson(sequenceError), arrayJson(flicker),
            arrayJson(signedError), arrayJson(peakError),
            acceptedPixels / std::max(1., validPixels), historyWeight / std::max(1., validPixels),
            virtualPixels / std::max(1., validPixels), frameRecords);
        WriteFileAtomically(output / ("sequence-" + name + ".json"),
                            std::as_bytes(std::span{sequenceRecord.data(), sequenceRecord.size()}));
        logger.Write(LogLevel::Info,
                     std::format("Temporal sequence {}: MSE {}, residual delta {}, history {:.3f}",
                                 name, arrayJson(sequenceError), arrayJson(flicker),
                                 historyWeight / std::max(1., validPixels)));
        std::array<double, 3> error{};
        const auto pitch = rhi::TextureRowPitch(color->desc.width);
        for (uint32_t y = 0; y < color->desc.height; ++y)
            for (uint32_t x = 0; x < color->desc.width; ++x)
                for (uint32_t channel = 0; channel < 3; ++channel)
                {
                    const auto pixel = y * pitch + x * 4 + channel;
                    for (uint32_t mode = 0; mode < 3; ++mode)
                    {
                        const double delta = std::to_integer<uint8_t>(images[mode][pixel]) -
                                             int(std::to_integer<uint8_t>(reference[pixel]));
                        error[mode] +=
                            delta * delta / (double(color->desc.width) * color->desc.height * 3);
                    }
                }
        const auto record = std::format(
            "    {{\"trajectory\":\"{}\",\"unfilteredRgbMse\":{:.8f},\"spatialRgbMse\":{:.8f},"
            "\"temporalRgbMse\":{:.8f},\"temporalGpuMeanMs\":{:.6f}}}",
            name, error[0], error[1], error[2], temporalCost);
        json += record + (trajectory < 2 ? ",\n" : "\n");
        logger.Write(LogLevel::Info, "Temporal camera motion endpoint: " + record);
        options.profile = true;
    }
    json += "  ]\n}\n";
    WriteFileAtomically(output / "temporal-motion.json",
                        std::as_bytes(std::span{json.data(), json.size()}));
    renderer.Document() = original;
}
} // namespace renderlab::samples
