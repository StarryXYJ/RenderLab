#include "apps/sandbox/capture.hpp"
#include "apps/sandbox/statistics.hpp"
#include "platform/text.hpp"
#include "rhi/upload.hpp"
#include "samples/image_io.hpp"

#include <chrono>

namespace renderlab::sandbox
{
void CaptureScene(rhi::Runtime& runtime, const rhi::DeviceReport& device, const Options& options,
                  const std::filesystem::path& shaders, Logger& logger)
{
    const auto output =
        std::filesystem::path(platform::Utf8ToWide(options.outputPath)) / options.backend;
    std::filesystem::create_directories(output);
    renderer::SceneRenderer scene(
        runtime, options.backend == "dx12" ? shader::Target::Dxil : shader::Target::Spirv,
        std::filesystem::path(platform::Utf8ToWide(options.projectPath)), shaders);
    auto target = runtime.CreateTexture(
        {options.width, options.height, rhi::Format::Rgba8Srgb, false, true, "Capture.Display"});
    const auto settings = RenderOptions(options);
    auto initial = rhi::State::Undefined;
    Statistics statistics;
    const auto measured = options.benchmarkFrames ? options.benchmarkFrames : 1;
    const auto warmup = options.benchmarkFrames ? 5u : 0u;
    for (uint32_t frame = 0; frame < measured + warmup; ++frame)
    {
        const auto start = std::chrono::steady_clock::now();
        const auto rendered = scene.Render(target, initial, nullptr, false, {}, settings);
        const auto cpuMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        runtime.Wait(rendered.completion);
        initial = rhi::State::CopySource;
        if (frame >= warmup)
            statistics.Add(rendered, runtime, cpuMs);
    }
    const auto memory = runtime.GetMemoryStats();
    auto captureOptions = settings;
    captureOptions.profile = false;
    const auto capture =
        scene.Render(target, initial, nullptr, true, output / "scene-plan.json", captureOptions);
    runtime.Wait(capture.completion);
    samples::SaveImage(output / "scene.bmp", capture.pixels->Map(), options.width, options.height,
                       rhi::TextureRowPitch(options.width));
    if (!options.profilePath.empty())
        statistics.Save(std::filesystem::path(platform::Utf8ToWide(options.profilePath)), options,
                        device, memory);
    logger.Write(LogLevel::Info, statistics.Text(scene.LastSnapshot(), memory));
}
} // namespace renderlab::sandbox
