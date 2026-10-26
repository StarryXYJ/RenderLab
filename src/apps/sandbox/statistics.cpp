#include "apps/sandbox/statistics.hpp"
#include "core/file_io.hpp"

#include <nlohmann/json.hpp>

#include <Windows.h>
#include <psapi.h>

#include <algorithm>
#include <format>

namespace renderlab::sandbox
{
renderer::SceneRenderOptions RenderOptions(const Options& options)
{
    renderer::SceneRenderOptions render;
    render.profile =
        options.debugPanel || !options.profilePath.empty() || options.benchmarkFrames > 0;
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
                     double cpuMs)
{
    const auto timings = frame.profile.Read(runtime);
    cpu_.Add(cpuMs);
    compile_.Add(double(frame.compileNs) / 1e6);
    prepare_.Add(double(frame.prepareNs) / 1e6);
    record_.Add(double(frame.recordNs) / 1e6);
    for (const auto& pass : timings)
        passes_[pass.name].Add(pass.nanoseconds / 1e6);
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
                      const rhi::DeviceReport& device, rhi::MemoryStats memory) const
{
    const auto summary = [](const Samples& samples)
    {
        return nlohmann::json{{"samples", samples.count},
                              {"meanMs", samples.Mean()},
                              {"minMs", samples.minimum},
                              {"maxMs", samples.maximum}};
    };
    nlohmann::json result{{"schemaVersion", 1},
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
                          {"warmupFrames", options.benchmarkFrames ? 5 : 0},
                          {"cpuRender", summary(cpu_)},
                          {"graphCompile", summary(compile_)},
                          {"graphPrepare", summary(prepare_)},
                          {"graphRecord", summary(record_)},
                          {"memory",
                           {{"usedBytes", memory.used},
                            {"highWaterBytes", memory.highWater},
                            {"budgetBytes", memory.budget}}},
                          {"gpuPasses", nlohmann::json::object()}};
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
