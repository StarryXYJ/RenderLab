#include "apps/sandbox/options.hpp"
#include "core/log.hpp"
#include "rhi/commands.hpp"
#include "rhi/cpu_profile.hpp"
#include "rhi/device.hpp"
#include "rhi/upload.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>

namespace
{
struct TestBuffer : renderlab::rhi::Buffer
{
    using Buffer::Buffer;
    std::span<std::byte> Map() override
    {
        return {};
    }
};
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

template <class F> void RequireInvalid(F function)
{
    try
    {
        function();
    }
    catch (const std::invalid_argument&)
    {
        return;
    }
    throw std::runtime_error("Expected invalid argument diagnostic");
}
} // namespace

int main()
{
    using namespace std::literals;
    using renderlab::sandbox::ParseOptions;
    try
    {
        const auto parsed = ParseOptions(std::array{"--backend"sv, "vulkan"sv, "--adapter"sv, "2"sv,
                                                    "--validation"sv, "on"sv, "--headless"sv,
                                                    "--report"sv, "报告.json"sv});
        Require(parsed.backend == "vulkan" && parsed.adapter == 2u && parsed.headless &&
                    parsed.reportPath == "报告.json",
                "Valid CLI values were not preserved");
        RequireInvalid([] { ParseOptions(std::array{"--adapter"sv, "4294967296"sv}); });
        RequireInvalid([] { ParseOptions(std::array{"--adapter"sv, "3junk"sv}); });
        RequireInvalid([] { ParseOptions(std::array{"--validation"sv, "maybe"sv}); });
        RequireInvalid([] { ParseOptions(std::array{"--report"sv}); });
        RequireInvalid([] { ParseOptions(std::array{"--window-test"sv, "--headless"sv}); });
        RequireInvalid([] { ParseOptions(std::array{"--frames"sv, "1"sv, "--headless"sv}); });
        RequireInvalid([] { ParseOptions(std::array{"--frames"sv, "-1"sv}); });
        RequireInvalid([] { ParseOptions(std::array{"--output"sv, ""sv}); });
        RequireInvalid([] { ParseOptions(std::array{"--width"sv, "0"sv}); });
        RequireInvalid([] { ParseOptions(std::array{"--height"sv, "16385"sv}); });
        RequireInvalid([] { ParseOptions(std::array{"--quality"sv, "ultra"sv}); });
        RequireInvalid([] { ParseOptions(std::array{"--view"sv, "missing"sv}); });
        RequireInvalid([] { ParseOptions(std::array{"--denoise"sv, "maybe"sv}); });
        RequireInvalid([] { ParseOptions(std::array{"--temporal-denoise"sv, "maybe"sv}); });
        Require(!ParseOptions(std::array{"--temporal-denoise"sv, "off"sv}).reflectionTemporal,
                "Temporal denoise switch was not parsed");
        RequireInvalid([] { ParseOptions(std::array{"--gpu-budget-mib"sv, "0"sv}); });
        RequireInvalid([] { ParseOptions(std::array{"--gpu-budget-mib"sv, "16385"sv}); });
        Require(ParseOptions(std::array{"--gpu-budget-mib"sv, "1024"sv}).gpuBudgetMiB == 1024,
                "Explicit allocation budget was not parsed");
        const auto denoise =
            ParseOptions(std::array{"--project"sv, "p"sv, "--pipeline"sv, "raytracing"sv,
                                    "--view"sv, "reflection-denoised"sv, "--denoise"sv, "off"sv});
        Require(!denoise.reflectionDenoise && denoise.view == "reflection-denoised",
                "Spatial denoise options were not parsed independently");
        RequireInvalid([] { ParseOptions(std::array{"--profile"sv, ""sv}); });
        RequireInvalid([] { ParseOptions(std::array{"--benchmark"sv, "0"sv}); });
        RequireInvalid([] { ParseOptions(std::array{"--debug-panel"sv, "--headless"sv}); });
        const auto tools = ParseOptions(
            std::array{"--project"sv, "sample.rlproject"sv, "--headless"sv, "--benchmark"sv, "12"sv,
                       "--quality"sv, "high"sv, "--view"sv, "shadow"sv, "--width"sv, "257"sv});
        Require(tools.benchmarkFrames == 12 && tools.quality == "high" && tools.view == "shadow" &&
                    tools.width == 257,
                "Scene capture options must remain explicit and reproducible");
        const auto timed = ParseOptions(
            std::array{"--project"sv, "sample.rlproject"sv, "--headless"sv, "--benchmark-seconds"sv,
                       "0.25"sv, "--warmup-seconds"sv, "0.1"sv, "--warmup-frames"sv, "0"sv,
                       "--samples"sv, "raw.jsonl"sv});
        Require(timed.Benchmark() && timed.benchmarkSeconds == .25 && timed.warmupSeconds == .1 &&
                    timed.warmupFrames == 0 && timed.samplesPath == "raw.jsonl",
                "Timed benchmark CLI");
        for (const auto value : {"0"sv, "nan"sv, "inf"sv, "-1"sv, "3601"sv, "1junk"sv})
            RequireInvalid([&] { ParseOptions(std::array{"--benchmark-seconds"sv, value}); });
        RequireInvalid(
            []
            {
                ParseOptions(std::array{"--project"sv, "p"sv, "--headless"sv, "--benchmark"sv,
                                        "1"sv, "--benchmark-seconds"sv, "1"sv});
            });
        RequireInvalid(
            []
            {
                ParseOptions(std::array{"--project"sv, "p"sv, "--headless"sv,
                                        "--benchmark-seconds"sv, "1"sv, "--graph-test"sv});
            });
        RequireInvalid([] { ParseOptions(std::array{"--samples"sv, "raw.jsonl"sv}); });
        RequireInvalid([] { ParseOptions(std::array{"--cpu-profile"sv}); });
        const auto profiled =
            ParseOptions(std::array{"--project"sv, "sample.rlproject"sv, "--headless"sv,
                                    "--benchmark"sv, "1"sv, "--cpu-profile"sv});
        Require(profiled.cpuProfile, "CPU profiling must be explicit and benchmark scoped");
        {
            renderlab::rhi::CpuProfileSnapshot before{}, after{};
            {
                renderlab::rhi::CpuTimer disabled(nullptr);
            }
            try
            {
                renderlab::rhi::CpuTimer timer(&after[0]);
                throw std::runtime_error("profiled exception");
            }
            catch (const std::runtime_error&)
            {
            }
            const auto delta = renderlab::rhi::Difference(after, before);
            Require(delta[0].calls == 1 && delta[1].calls == 0,
                    "CPU scopes count exception exits and isolate operations");
        }

        using namespace renderlab::rhi;
        int owner = 0, otherOwner = 0;
        auto buffer =
            std::make_shared<TestBuffer>(&owner, BufferDesc{1024, Memory::Device, "test"});
        auto foreign =
            std::make_shared<TestBuffer>(&otherOwner, BufferDesc{1024, Memory::Device, "foreign"});
        CommandList commands(&owner);
        RequireInvalid([&] { commands.Copy(buffer, foreign, 1); });
        RequireInvalid([&] { commands.Copy(buffer, buffer, 1); });
        auto destination =
            std::make_shared<TestBuffer>(&owner, BufferDesc{1024, Memory::Readback, "readback"});
        RequireInvalid([&] { commands.Copy(buffer, destination, 2, 1023); });
        RequireInvalid([&] { commands.Copy(buffer, destination, 1, UINT64_MAX); });
        auto texture = std::make_shared<Texture>(&owner, TextureDesc{65, 3, Format::Rgba8});
        RequireInvalid([&] { commands.CopyTexture(buffer, texture, 260, true); });
        RequireInvalid([&] { commands.CopyTexture(buffer, texture, 512, true); });
        RequireInvalid([&] { commands.CopyTexture(buffer, texture, 512, true, 1); });
        RequireInvalid([&] { commands.Barrier(texture, State::Undefined, State::Storage); });
        RequireInvalid([&] { commands.ReadTimestamps(destination); });
        RequireInvalid([&] { commands.WriteTimestamp(MaxTimestampSlots); });
        {
            CommandList many(&owner);
            for (uint32_t i = 0; i < MaxTimestampSlots; ++i)
                many.WriteTimestamp(i);
            RequireInvalid([&] { many.ReadTimestamps(destination, 0); });
            RequireInvalid([&] { many.ReadTimestamps(destination, MaxTimestampSlots + 1); });
            many.ReadTimestamps(destination, MaxTimestampSlots);
            many.Cancel();
        }
        commands.WriteTimestamp(0);
        RequireInvalid([&] { commands.WriteTimestamp(0); });
        RequireInvalid([&] { commands.EndMarker(); });
        commands.Cancel();
        RequireInvalid([&] { commands.WriteTimestamp(1); });
        Require(TextureRowPitch(1) == 256 && TextureRowPitch(64) == 256 &&
                    TextureRowPitch(65) == 512,
                "Texture row pitch boundaries");
        RequireInvalid([] { TextureRowPitch(UINT32_MAX); });

        renderlab::rhi::DeviceReport report;
        report.backend = "test\"\\\n\t";
        report.formats = {{"RGBA8_UNORM", true, false, true, false},
                          {"RGBA16_FLOAT", true, false, true, false},
                          {"D32_FLOAT", false, false, false, true}};
        renderlab::rhi::ValidateRequiredFormats(report);
        const auto json = renderlab::rhi::ReportToJson(report, 3);
        Require(json.find("test\\\"\\\\\\n\\t") != std::string::npos, "JSON escaping failed");
        Require(json.find("\"validationErrors\": 3") != std::string::npos,
                "Validation errors lost");
        report.formats.pop_back();
        bool missingFormat = false;
        try
        {
            renderlab::rhi::ValidateRequiredFormats(report);
        }
        catch (const std::runtime_error&)
        {
            missingFormat = true;
        }
        Require(missingFormat, "Missing required format was accepted");
        report.formats.push_back({"D32_FLOAT", true, false, true, false});
        bool wrongRole = false;
        try
        {
            renderlab::rhi::ValidateRequiredFormats(report);
        }
        catch (const std::runtime_error&)
        {
            wrongRole = true;
        }
        Require(wrongRole, "Depth format without depth attachment support was accepted");

        const auto path = std::filesystem::path("test-output") / "nested" / "logger.log";
        {
            renderlab::Logger logger(path);
            logger.Write(renderlab::LogLevel::Warning, "UTF-8 日志");
        }
        std::ifstream file(path, std::ios::binary);
        const std::string contents{std::istreambuf_iterator<char>(file), {}};
        Require(contents.find("[WARN] UTF-8 日志\n") != std::string::npos,
                "UTF-8 log write failed");
        Require(contents.find('\r') == std::string::npos, "Log contains CRLF instead of LF");
        std::cout << "Core contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
