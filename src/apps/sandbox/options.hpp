#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace renderlab::sandbox
{
struct Options
{
    std::string backend = "dx12";
    std::string validation = "auto";
    std::optional<uint32_t> adapter;
    std::optional<uint32_t> exitAfterMs;
    std::optional<uint32_t> frames;
    std::string logPath = "logs/renderlab.log";
    std::string reportPath;
    std::string projectPath;
    std::string outputPath = "out/m1";
    std::string quality = "scene", view = "final", pipeline = "forward", profilePath;
    uint32_t width = 1280, height = 720, benchmarkFrames = 0;
    /// Zero selects the host default: scene 2048 MiB, standalone RHI demos 256 MiB.
    uint32_t gpuBudgetMiB = 0;
    double benchmarkSeconds = 0, warmupSeconds = 0;
    uint32_t warmupFrames = 5;
    std::string samplesPath;
    bool Benchmark() const
    {
        return benchmarkFrames > 0 || benchmarkSeconds > 0;
    }
    bool reflectionDenoise = true;
    bool debugPanel = false, fixedCamera = false, cpuProfile = false;
    bool headless = false;
    bool windowTest = false;
    bool rhiTest = false;
    bool accelerationStructureTest = false;
    bool sceneTest = false;
    bool graphTest = false, graphNoCull = false, graphNoUpdate = false;
    std::string graphDumpPath;
    bool help = false;
};

/**
 * @brief Parses arguments excluding argv[0]; the returned options own their string values.
 * @throws std::invalid_argument For unknown options, missing values or incompatible settings.
 */
Options ParseOptions(std::span<const std::string_view> arguments);
std::string_view Usage();
} // namespace renderlab::sandbox
