#include "samples/scene/reflection_temporal_motion.hpp"

#include "core/file_io.hpp"
#include "core/log.hpp"
#include "rhi/runtime.hpp"
#include "rhi/upload.hpp"
#include "samples/common/image_io.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
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
        SaveImage(output / (name + ".bmp"), pixels, color->desc.width, color->desc.height,
                  rhi::TextureRowPitch(color->desc.width));
        return std::vector<std::byte>(pixels.begin(), pixels.end());
    };
    std::string json =
        "{\n  \"referenceSpp\": 2048,\n  \"lightIntensityMultiplier\": 4,\n  \"cases\": [\n";
    for (uint32_t trajectory = 0; trajectory < 2; ++trajectory)
    {
        const std::string name = trajectory == 0 ? "gentle" : "fast";
        std::array<std::vector<std::byte>, 3> images;
        double temporalCost = 0;
        for (uint32_t mode = 0; mode < 3; ++mode)
        {
            const std::string label = mode == 0 ? "raw-final" : mode == 1 ? "spatial" : "temporal";
            reset(fixture);
            options.reflectionDenoise = mode != 0;
            options.reflectionTemporal = mode == 2;
            for (uint32_t warmup = 0; warmup < 8; ++warmup)
                render();
            for (uint32_t frame = 0; frame < 16; ++frame)
            {
                renderer.Document().MoveCamera({trajectory == 0 ? .02f : .08f, 0, 0},
                                               {0, trajectory == 0 ? .15f : .8f, 0});
                auto result =
                    render(frame == 15, frame == 15 ? "motion-" + name + "-" + label : "");
                if (result.reflectionSamples != 1 ||
                    result.reflectionTemporalFrames != (mode == 2 ? frame + 9 : 0))
                    throw std::runtime_error("Moving camera lost the independent temporal source");
                if (mode == 2)
                    for (const auto& pass : result.profile.Read(runtime))
                        if (pass.name == "RayTracing.Reflection.Temporal")
                            temporalCost += pass.nanoseconds / 1e6 / 16;
                if (frame == 15)
                    images[mode] = copyPixels(result, "motion-" + name + "-" + label);
            }
        }
        const auto endpoint = renderer.Document();
        reset(endpoint);
        options.reflectionDenoise = false;
        options.reflectionTemporal = false;
        options.profile = false;
        std::vector<std::byte> reference;
        for (uint32_t frame = 0; frame < 2048; ++frame)
        {
            const auto result = render(frame == 2047);
            if (result.reflectionSamples != frame + 1)
                throw std::runtime_error("Stationary endpoint reference did not accumulate");
            if (frame == 2047)
                reference = copyPixels(result, "motion-" + name + "-reference-2048");
        }
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
        json += record + (trajectory == 0 ? ",\n" : "\n");
        logger.Write(LogLevel::Info, "Temporal camera motion endpoint: " + record);
        options.profile = true;
    }
    json += "  ]\n}\n";
    WriteFileAtomically(output / "temporal-motion.json",
                        std::as_bytes(std::span{json.data(), json.size()}));
    renderer.Document() = original;
}
} // namespace renderlab::samples
