#include "benchmark/recording.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void Reject(F function)
{
    try
    {
        function();
    }
    catch (const std::logic_error&)
    {
        return;
    }
    throw std::runtime_error("Expected rejected measurement");
}
} // namespace
int main()
{
    using namespace renderlab::benchmark;
    try
    {
        const auto root = std::filesystem::absolute("test-output/benchmark");
        Recording recording(root / "raw/frames.jsonl");
        Reject([&] { recording.Summary(root / "report.json"); });
        Reject([&] { recording.Close(1, 0, 0); });
        Reject(
            [&]
            {
                recording.Add({1, {{"frameTimeMs", std::numeric_limits<double>::quiet_NaN()}}, {}});
            });
        recording.Add({10, {{"frameTimeMs", 8}, {"extraMetricMs", 2}}, {{"Scene", .5}}});
        Reject([&] { recording.Add({9, {{"frameTimeMs", 1}}, {}}); });
        recording.Add({20, {{"frameTimeMs", 12}}, {{"Scene", 1.5}}});
        Reject([&] { recording.Close(19, 0, 0); });
        recording.Close(25, 5, 100);
        Reject([&] { recording.Add({30, {{"frameTimeMs", 1}}, {}}); });
        const auto report = recording.Summary(root / "report.json");
        Require(report["sampleCount"] == 2 && report["throughputFps"] == 80 &&
                    report["samplesFile"] == "raw/frames.jsonl" && report["complete"] == true,
                "Completed recording must preserve timing/count and portable sample reference");
        const auto metrics = report["metricsMs"];
        Require(metrics["frameTimeMs"]["meanMs"] == 10 && metrics["extraMetricMs"]["samples"] == 1,
                "Metric summaries must allow independently sampled extension metrics");
        std::ifstream input(root / "raw/frames.jsonl", std::ios::binary);
        std::string line;
        uint64_t count = 0;
        while (std::getline(input, line))
        {
            const auto sample = nlohmann::json::parse(line);
            Require(sample["frame"] == count++ && sample["schemaVersion"] == 1 &&
                        sample["gpuPassesMs"].contains("Scene"),
                    "Raw stream must preserve ordered frame identity and individual GPU passes");
        }
        Require(count == 2, "Raw stream and completed summary must agree");
        std::cout << "Benchmark contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
