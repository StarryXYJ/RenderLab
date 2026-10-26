#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace renderlab::benchmark
{
/** @brief One completed offline frame. All timing metrics use milliseconds. */
struct Sample
{
    double elapsedMs = 0;
    std::map<std::string, double> metricsMs, gpuPassesMs;
};
/** @brief Streams raw frames with constant memory; summaries retain online moments only.
 * Close must succeed before publishing a report. Partial recordings have no completed report.
 */
class Recording final
{
public:
    explicit Recording(const std::filesystem::path& samplesPath);
    void Add(const Sample& sample);
    void Close(double wallMs, uint32_t warmupFrames, double warmupMs);
    nlohmann::json Summary(const std::filesystem::path& reportPath) const;

private:
    struct Metric
    {
        uint64_t count = 0;
        double mean = 0, minimum = 0, maximum = 0;
        void Add(double value);
    };
    std::filesystem::path path_;
    std::ofstream stream_;
    std::map<std::string, Metric> metrics_;
    uint64_t count_ = 0;
    uint32_t warmupFrames_ = 0;
    double lastElapsedMs_ = 0, wallMs_ = 0, warmupMs_ = 0;
    bool closed_ = false;
};
} // namespace renderlab::benchmark
