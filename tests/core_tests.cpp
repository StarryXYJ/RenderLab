#include "apps/sandbox/options.hpp"
#include "core/log.hpp"
#include "rhi/commands.hpp"
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
