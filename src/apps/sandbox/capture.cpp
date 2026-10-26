#include "apps/sandbox/capture.hpp"
#include "apps/sandbox/statistics.hpp"
#include "benchmark/recording.hpp"
#include "platform/text.hpp"
#include "rhi/upload.hpp"
#include "samples/common/image_io.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <format>
#include <memory>
#include <stdexcept>

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
    using Clock = std::chrono::steady_clock;
    const auto milliseconds = [](auto duration)
    { return std::chrono::duration<double, std::milli>(duration).count(); };
    const auto render = [&]()
    {
        auto frame = scene.Render(target, initial, nullptr, false, {}, settings);
        initial = rhi::State::CopySource;
        return frame;
    };
    uint32_t warmupFrames = 0;
    const auto warmupStart = Clock::now();
    if (options.Benchmark())
        while (warmupFrames < options.warmupFrames ||
               milliseconds(Clock::now() - warmupStart) < options.warmupSeconds * 1000)
        {
            runtime.Wait(render().completion);
            ++warmupFrames;
        }
    const auto warmupMs = milliseconds(Clock::now() - warmupStart);
    auto reportPath = options.profilePath.empty()
                          ? output / "benchmark.json"
                          : std::filesystem::path(platform::Utf8ToWide(options.profilePath));
    std::unique_ptr<benchmark::Recording> recording;
    if (options.Benchmark())
    {
        const auto samplesPath =
            options.samplesPath.empty()
                ? output / "frames.jsonl"
                : std::filesystem::path(platform::Utf8ToWide(options.samplesPath));
        if (std::filesystem::weakly_canonical(samplesPath) ==
            std::filesystem::weakly_canonical(reportPath))
            throw std::invalid_argument("Benchmark report and samples must use different files");
        recording = std::make_unique<benchmark::Recording>(samplesPath);
    }
    runtime.SetCpuProfiling(options.cpuProfile);
    const auto cpuMeasureStart = runtime.GetCpuProfile();
    const auto measureStart = Clock::now();
    uint64_t measured = 0;
    do
    {
        const auto cpuBefore = runtime.GetCpuProfile();
        const auto start = Clock::now();
        const auto rendered = render();
        const auto submitted = Clock::now();
        runtime.Wait(rendered.completion);
        const auto completed = Clock::now();
        benchmark::Sample sample;
        sample.elapsedMs = milliseconds(completed - measureStart);
        sample.metricsMs = {{"frameTimeMs", milliseconds(completed - start)},
                            {"gpuWaitMs", milliseconds(completed - submitted)}};
        statistics.Add(rendered, runtime, milliseconds(submitted - start),
                       recording ? &sample : nullptr);
        if (options.cpuProfile)
        {
            const auto delta = rhi::Difference(runtime.GetCpuProfile(), cpuBefore);
            for (size_t i = 0; i < delta.size(); ++i)
                sample.metricsMs.emplace(rhi::CpuOperationNames[i],
                                         double(delta[i].nanoseconds) / 1e6);
        }
        if (recording)
            recording->Add(sample);
        ++measured;
    } while (options.benchmarkSeconds > 0
                 ? milliseconds(Clock::now() - measureStart) < options.benchmarkSeconds * 1000
                 : measured < (options.benchmarkFrames ? options.benchmarkFrames : 1u));
    const auto cpuOperations = rhi::Difference(runtime.GetCpuProfile(), cpuMeasureStart);
    runtime.SetCpuProfiling(false);
    if (recording)
        recording->Close(milliseconds(Clock::now() - measureStart), warmupFrames, warmupMs);
    const auto memory = runtime.GetMemoryStats();
    const auto descriptors = runtime.GetDescriptorStats();
    const auto uploads = scene.UploadStatistics();
    auto captureOptions = settings;
    captureOptions.profile = false;
    const auto capture =
        scene.Render(target, initial, nullptr, true, output / "scene-plan.json", captureOptions);
    runtime.Wait(capture.completion);
    samples::SaveImage(output / "scene.bmp", capture.pixels->Map(), options.width, options.height,
                       rhi::TextureRowPitch(options.width));
    if (recording || !options.profilePath.empty())
        statistics.Save(reportPath, options, device, memory, recording.get(),
                        options.cpuProfile ? &cpuOperations : nullptr, &descriptors, &uploads);
    if (recording)
    {
        const auto summary = recording->Summary(reportPath);
        logger.Write(
            LogLevel::Info,
            std::format("Benchmark: {} frames, serial frame mean {:.3f} ms, wall throughput {:.1f} "
                        "FPS; report: {}",
                        summary.at("sampleCount").get<uint64_t>(),
                        summary.at("metricsMs").at("frameTimeMs").at("meanMs").get<double>(),
                        summary.at("throughputFps").get<double>(),
                        platform::WideToUtf8(reportPath.native())));
    }
    logger.Write(LogLevel::Info, statistics.Text(scene.LastSnapshot(), memory));
}
} // namespace renderlab::sandbox
