#include "benchmark/recording.hpp"
#include "platform/text.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace renderlab::benchmark
{
namespace
{
void Validate(double value)
{
    if (!std::isfinite(value) || value < 0)
        throw std::invalid_argument("Benchmark measurements must be finite and nonnegative");
}
} // namespace
Recording::Recording(const std::filesystem::path& samplesPath)
    : path_(std::filesystem::absolute(samplesPath).lexically_normal())
{
    std::filesystem::create_directories(path_.parent_path());
    stream_.exceptions(std::ios::failbit | std::ios::badbit);
    stream_.open(path_, std::ios::binary | std::ios::trunc);
}
void Recording::Metric::Add(double value)
{
    if (!count)
        minimum = maximum = value;
    minimum = std::min(minimum, value);
    maximum = std::max(maximum, value);
    mean += (value - mean) / double(++count);
}
void Recording::Add(const Sample& sample)
{
    if (closed_)
        throw std::logic_error("Cannot append to a completed benchmark");
    Validate(sample.elapsedMs);
    if (sample.elapsedMs < lastElapsedMs_ || sample.metricsMs.empty())
        throw std::invalid_argument("Benchmark frames require ordered timestamps and metrics");
    for (const auto* values : {&sample.metricsMs, &sample.gpuPassesMs})
        for (const auto& [name, value] : *values)
        {
            if (name.empty())
                throw std::invalid_argument("Benchmark metric name cannot be empty");
            Validate(value);
        }
    const nlohmann::json row{{"schemaVersion", 1},
                             {"frame", count_},
                             {"elapsedMs", sample.elapsedMs},
                             {"metricsMs", sample.metricsMs},
                             {"gpuPassesMs", sample.gpuPassesMs}};
    stream_ << row.dump() << '\n';
    for (const auto& [name, value] : sample.metricsMs)
        metrics_[name].Add(value);
    ++count_;
    lastElapsedMs_ = sample.elapsedMs;
}
void Recording::Close(double wallMs, uint32_t warmupFrames, double warmupMs)
{
    Validate(wallMs);
    Validate(warmupMs);
    if (closed_ || !count_ || wallMs <= 0 || wallMs < lastElapsedMs_)
        throw std::invalid_argument("Cannot complete an empty or inconsistent benchmark");
    stream_.flush();
    stream_.close();
    wallMs_ = wallMs;
    warmupFrames_ = warmupFrames;
    warmupMs_ = warmupMs;
    closed_ = true;
}
nlohmann::json Recording::Summary(const std::filesystem::path& reportPath) const
{
    if (!closed_)
        throw std::logic_error("Benchmark must be closed before publishing its summary");
    const auto relative =
        std::filesystem::relative(path_, std::filesystem::absolute(reportPath).parent_path());
    nlohmann::json result{{"complete", true},
                          {"sampleSchemaVersion", 1},
                          {"samplesFile", platform::WideToUtf8(relative.generic_wstring())},
                          {"sampleCount", count_},
                          {"wallTimeMs", wallMs_},
                          {"throughputFps", double(count_) * 1000 / wallMs_},
                          {"warmupFrames", warmupFrames_},
                          {"warmupMs", warmupMs_},
                          {"timingMode", "headless-serial"},
                          {"metricsMs", nlohmann::json::object()}};
    for (const auto& [name, metric] : metrics_)
        result["metricsMs"][name] = {{"samples", metric.count},
                                     {"meanMs", metric.mean},
                                     {"minMs", metric.minimum},
                                     {"maxMs", metric.maximum}};
    return result;
}
} // namespace renderlab::benchmark
