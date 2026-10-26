#include "apps/sandbox/statistics.hpp"
#include "benchmark/recording.hpp"
#include "core/file_io.hpp"
#include "platform/text.hpp"

#include <nlohmann/json.hpp>

#include <Windows.h>
#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <format>

namespace renderlab::sandbox
{
renderer::SceneRenderOptions RenderOptions(const Options& options)
{
    renderer::SceneRenderOptions render;
    render.pipeline = options.pipeline == "deferred"     ? renderer::PipelineType::Deferred
                      : options.pipeline == "raytracing" ? renderer::PipelineType::RayTracing
                                                         : renderer::PipelineType::Forward;
    render.profile = options.debugPanel || !options.profilePath.empty() || options.Benchmark();
    if (options.quality != "scene")
        render.shadowResolution = options.quality == "low"      ? 128u
                                  : options.quality == "medium" ? 512u
                                                                : 1024u;
    if (options.view == "hdr")
        render.view = renderer::SceneView::Hdr;
    else if (options.view == "shadow")
        render.view = renderer::SceneView::Shadow;
    else if (options.view == "environment")
        render.view = renderer::SceneView::Environment;
    else if (options.view == "brdf")
        render.view = renderer::SceneView::Brdf;
    if (options.view == "reflection-raw")
        render.view = renderer::SceneView::ReflectionRaw;
    if (options.view == "reflection-history")
        render.view = renderer::SceneView::ReflectionHistory;
    return render;
}
void Statistics::Samples::Add(double value)
{
    if (!count)
        minimum = maximum = value;
    ++count;
    total += value;
    minimum = std::min(minimum, value);
    maximum = std::max(maximum, value);
}
double Statistics::Samples::Mean() const
{
    return count ? total / double(count) : 0;
}
void Statistics::Add(const renderer::SceneReadback& frame, const rhi::Runtime& runtime,
                     double cpuMs, benchmark::Sample* sample)
{
    const auto timings = frame.profile.Read(runtime);
    cpu_.Add(cpuMs);
    compile_.Add(double(frame.compileNs) / 1e6);
    prepare_.Add(double(frame.prepareNs) / 1e6);
    record_.Add(double(frame.recordNs) / 1e6);
    for (const auto& pass : timings)
    {
        passes_[pass.name].Add(pass.nanoseconds / 1e6);
        if (sample)
            sample->gpuPassesMs.emplace(pass.name, pass.nanoseconds / 1e6);
    }
    if (sample)
    {
        sample->metricsMs.insert({{"cpuRenderMs", cpuMs},
                                  {"graphCompileMs", double(frame.compileNs) / 1e6},
                                  {"graphPrepareMs", double(frame.prepareNs) / 1e6},
                                  {"graphRecordMs", double(frame.recordNs) / 1e6}});
        double gpuSumMs = 0;
        for (const auto& [name, ms] : sample->gpuPassesMs)
            gpuSumMs += ms;
        sample->metricsMs.emplace("gpuPassSumMs", gpuSumMs);
    }
}
std::string Statistics::Text(const scene::Snapshot& snapshot, rhi::MemoryStats memory) const
{
    auto text =
        std::format("Completed samples: {}\nCPU render: {:.3f} ms (mean)\nGraph "
                    "compile/prepare/record: {:.3f}/{:.3f}/{:.3f} ms\nVisible / total: {} / "
                    "{}\nRHI used / peak: {:.1f} / {:.1f} MiB\n\nGPU pass means (ms):\n",
                    cpu_.count, cpu_.Mean(), compile_.Mean(), prepare_.Mean(), record_.Mean(),
                    snapshot.instances.size(), snapshot.totalInstances,
                    double(memory.used) / (1024 * 1024), double(memory.highWater) / (1024 * 1024));
    for (const auto& [name, pass] : passes_)
        text += std::format("{}: {:.3f}\n", name, pass.Mean());
    return text;
}
void Statistics::Save(const std::filesystem::path& path, const Options& options,
                      const rhi::DeviceReport& device, rhi::MemoryStats memory,
                      const benchmark::Recording* recording,
                      const rhi::CpuProfileSnapshot* cpuOperations,
                      const rhi::DescriptorStats* descriptors,
                      const rhi::UploadPool::Statistics* uploads) const
{
    const auto summary = [](const Samples& samples)
    {
        return nlohmann::json{{"samples", samples.count},
                              {"meanMs", samples.Mean()},
                              {"minMs", samples.minimum},
                              {"maxMs", samples.maximum}};
    };
    nlohmann::json result{{"schemaVersion", recording ? 2 : 1},
                          {"backend", device.backend},
                          {"adapter", device.adapters.at(device.selectedAdapter).name},
                          {"driver", device.driverVersion},
                          {"validation", device.validationEnabled},
                          {"fixedCamera", options.fixedCamera || options.headless},
                          {"configurationMayChange", !options.headless},
                          {"project", options.projectPath},
                          {"width", options.width},
                          {"height", options.height},
                          {"quality", options.quality},
                          {"view", options.view},
                          {"pipeline", options.pipeline},
                          {"warmupFrames", options.Benchmark() ? options.warmupFrames : 0},
                          {"cpuRender", summary(cpu_)},
                          {"graphCompile", summary(compile_)},
                          {"graphPrepare", summary(prepare_)},
                          {"graphRecord", summary(record_)},
                          {"memory",
                           {{"usedBytes", memory.used},
                            {"highWaterBytes", memory.highWater},
                            {"budgetBytes", memory.budget}}},
                          {"gpuPasses", nlohmann::json::object()}};
    result["recordedAtUtc"] = std::format(
        "{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
#ifdef NDEBUG
    result["buildConfiguration"] = "Release";
#else
    result["buildConfiguration"] = "Debug";
#endif
    const auto& adapter = device.adapters.at(device.selectedAdapter);
    if (descriptors)
        result["resourceReuse"]["descriptors"] = {{"pages", descriptors->pages},
                                                  {"capacitySlots", descriptors->capacity},
                                                  {"inUseSlots", descriptors->inUse},
                                                  {"highWaterSlots", descriptors->highWater},
                                                  {"nativeCreations", descriptors->creations}};
    if (uploads)
        result["resourceReuse"]["frameUpload"] = {{"pages", uploads->pages},
                                                  {"reservedBytes", uploads->reservedBytes},
                                                  {"batchHighWaterBytes", uploads->highWaterBytes},
                                                  {"batches", uploads->batches}};
    result["adapterIdentity"] = {{"vendorId", adapter.vendorId},
                                 {"deviceId", adapter.deviceId},
                                 {"software", adapter.software}};
    if (recording)
    {
        result["project"] = platform::WideToUtf8(
            std::filesystem::weakly_canonical(
                std::filesystem::path(platform::Utf8ToWide(options.projectPath)))
                .generic_wstring());
        result["benchmark"] = recording->Summary(path);
        result["benchmark"]["requestedFrames"] = options.benchmarkFrames;
        result["benchmark"]["requestedSeconds"] = options.benchmarkSeconds;
        result["benchmark"]["requestedWarmupFrames"] = options.warmupFrames;
        result["benchmark"]["requestedWarmupSeconds"] = options.warmupSeconds;
        result["benchmark"]["cpuProfiling"] = options.cpuProfile;
        if (cpuOperations)
            for (size_t i = 0; i < cpuOperations->size(); ++i)
                result["benchmark"]["cpuOperations"][rhi::CpuOperationNames[i]] = {
                    {"calls", (*cpuOperations)[i].calls},
                    {"totalMs", double((*cpuOperations)[i].nanoseconds) / 1e6}};
    }
    for (const auto& [name, pass] : passes_)
        result["gpuPasses"][name] = summary(pass);
    PROCESS_MEMORY_COUNTERS_EX process{};
    process.cb = sizeof(process);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&process), sizeof(process)))
    {
        result["memory"]["processWorkingSetBytes"] = process.WorkingSetSize;
        result["memory"]["processPrivateBytes"] = process.PrivateUsage;
    }
    else
    {
        result["memory"]["processWorkingSetBytes"] = nullptr;
        result["memory"]["processPrivateBytes"] = nullptr;
    }
    const auto text = result.dump(2) + "\n";
    WriteFileAtomically(path, std::as_bytes(std::span{text.data(), text.size()}));
}
} // namespace renderlab::sandbox
