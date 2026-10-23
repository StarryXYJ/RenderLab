#include "samples/rhi_acceptance.hpp"

#include "rhi/commands.hpp"
#include "rhi/runtime.hpp"
#include "rhi/upload.hpp"
#include "samples/detail/check.hpp"
#include "samples/rhi_contract_checks.hpp"
#include "samples/rhi_demo.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <span>

namespace renderlab::samples
{
using namespace rhi;
using detail::Require;

namespace
{
uint32_t Hash(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352d;
    value ^= value >> 15;
    value *= 0x846ca68b;
    return value ^ (value >> 16);
}
std::array<uint8_t, 4> Noise(uint32_t x, uint32_t y, uint32_t width)
{
    const auto value = Hash(x + y * width + 0x12345678);
    return {uint8_t(value ^ 0x5a), uint8_t((value >> 8) ^ 0xa5), uint8_t((value >> 16) ^ 0x3c),
            255};
}
void SaveImage(const std::filesystem::path& path, std::span<const std::byte> pixels, uint32_t width,
               uint32_t height, uint32_t pitch)
{
    // Top-down 32-bit BMP: inspection artifact, never a source asset or shader cache.
    std::array<uint8_t, 54> header{};
    const auto write = [&](size_t offset, uint32_t value)
    { std::memcpy(header.data() + offset, &value, 4); };
    header[0] = 'B';
    header[1] = 'M';
    write(2, 54 + width * height * 4);
    write(10, 54);
    write(14, 40);
    write(18, width);
    write(22, uint32_t(-int32_t(height)));
    header[26] = 1;
    header[28] = 32;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(header.data()), header.size());
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
        {
            const auto* p = pixels.data() + y * pitch + x * 4;
            const std::array<std::byte, 4> bgra{p[2], p[1], p[0], p[3]};
            file.write(reinterpret_cast<const char*>(bgra.data()), 4);
        }
    Require(bool(file), "cannot save image " + path.string());
}
} // namespace

void VerifyRhi(Runtime& runtime, RhiDemo& demo, Logger& logger,
               const std::filesystem::path& outputDirectory)
{
    using namespace rhi;
    std::filesystem::create_directories(outputDirectory);
    CheckResourceContracts(runtime);
    const auto noise = demo.GetNoiseTexture();
    const auto noiseWidth = noise->desc.width;
    const auto noiseHeight = noise->desc.height;
    const auto noisePitch = TextureRowPitch(noiseWidth);
    auto noiseReadback = runtime.CreateBuffer(
        {uint64_t(noisePitch) * noiseHeight, Memory::Readback, "Noise.Readback"});
    constexpr uint32_t width = 640, height = 360;
    const auto pitch = TextureRowPitch(width);
    auto color =
        runtime.CreateTexture({width, height, Format::Rgba8Srgb, false, true, "Acceptance.Color"});
    auto depth =
        runtime.CreateTexture({width, height, Format::D32, false, true, "Acceptance.ReversedZ"});
    auto colorReadback =
        runtime.CreateBuffer({uint64_t(pitch) * height, Memory::Readback, "Color.Readback"});
    auto timestamps = runtime.CreateBuffer({16, Memory::Readback, "Frame.Timestamps"});
    auto commands = runtime.CreateCommands();
    commands->WriteTimestamp(0);
    commands->Barrier(noise, State::Sampled, State::CopySource);
    commands->CopyTexture(noiseReadback, noise, noisePitch, false);
    commands->Barrier(noiseReadback, State::CopyDest, State::HostRead);
    commands->Barrier(noise, State::CopySource, State::Sampled);
    commands->Barrier(color, State::Undefined, State::ColorAttachment);
    commands->Barrier(depth, State::Undefined, State::DepthWrite);
    demo.Render(*commands, runtime.CreateView(color), runtime.CreateView(depth));
    commands->Barrier(color, State::ColorAttachment, State::CopySource);
    commands->CopyTexture(colorReadback, color, pitch, false);
    commands->Barrier(colorReadback, State::CopyDest, State::HostRead);
    commands->WriteTimestamp(1);
    commands->ReadTimestamps(timestamps);
    commands->Barrier(timestamps, State::CopyDest, State::HostRead);
    runtime.Wait(runtime.Submit(std::move(commands)));
    const auto actualNoise = noiseReadback->Map();
    for (uint32_t y = 0; y < noiseHeight; ++y)
        for (uint32_t x = 0; x < noiseWidth; ++x)
        {
            const auto expected = Noise(x, y, noiseWidth);
            const auto* actual = actualNoise.data() + y * noisePitch + x * 4;
            Require(std::memcmp(actual, expected.data(), 4) == 0,
                    std::format("noise mismatch at ({},{})", x, y));
        }
    const auto pixels = colorReadback->Map();
    const auto probe = [&](uint32_t x, uint32_t y)
    {
        std::array<uint8_t, 4> result{};
        std::memcpy(result.data(), pixels.data() + y * pitch + x * 4, 4);
        return result;
    };
    // Near the triangle's three corners: verifies Y, winding, vertex layout and reversed-Z
    // rejection of the later magenta draw.
    const auto red = probe(58, 286), green = probe(282, 286), blue = probe(170, 82);
    Require(red[0] > 220 && red[1] < 130 && red[2] < 130, "red corner / winding / reversed-Z");
    Require(green[1] > 220 && green[0] < 130 && green[2] < 130, "green corner / X transform");
    Require(blue[2] > 220 && blue[0] < 130 && blue[1] < 130, "blue corner / viewport Y");
    const auto blend = probe(170, 31);
    Require(
        std::abs(int(blend[0]) - 103) <= 2 && blend[0] == blend[1] && blend[1] == blend[2],
        std::format("linear alpha blend 0.25*0.5+0.02*0.5 -> sRGB expected 103, got {}", blend[0]));
    // Interior noise samples validate top-left UV origin and compute-write -> fragment-sample
    // ordering.
    for (auto p : {std::array{370u, 60u}, std::array{550u, 300u}, std::array{470u, 180u}})
    {
        const float ndcX = (float(p[0]) + .5f) / width * 2 - 1 - .03125f;
        const float ndcY = 1 - (float(p[1]) + .5f) / height * 2;
        const auto x = static_cast<uint32_t>((ndcX - .05f) / .85f * noiseWidth);
        const auto y = static_cast<uint32_t>((.75f - ndcY) / 1.5f * noiseHeight);
        const auto expected = Noise(x, y, noiseWidth), actual = probe(p[0], p[1]);
        for (size_t i = 0; i < 4; ++i)
        {
            const auto linear = float(expected[i]) / 255;
            const auto encoded =
                i == 3 ? 255
                       : int(std::lround(255 *
                                         (linear <= .0031308f
                                              ? linear * 12.92f
                                              : 1.055f * std::pow(linear, 1.0f / 2.4f) - .055f)));
            Require(std::abs(encoded - int(actual[i])) <= 1,
                    std::format("sampled noise / UV at {},{} channel {}: expected {}, got {}", p[0],
                                p[1], i, expected[i], actual[i]));
        }
    }
    std::array<uint64_t, 2> times{};
    std::memcpy(times.data(), timestamps->Map().data(), 16);
    Require(times[0] && times[1] >= times[0], "invalid GPU timestamps");
    SaveImage(outputDirectory / "noise.bmp", actualNoise, noiseWidth, noiseHeight, noisePitch);
    SaveImage(outputDirectory / "triangle-noise.bmp", pixels, width, height, pitch);
    std::ofstream raw(outputDirectory / "noise.rgba", std::ios::binary | std::ios::trunc);
    for (uint32_t y = 0; y < noiseHeight; ++y)
        raw.write(reinterpret_cast<const char*>(actualNoise.data() + y * noisePitch),
                  noiseWidth * 4);
    Require(bool(raw), "cannot write raw noise reference");
    const auto stats = runtime.GetMemoryStats();
    logger.Write(
        LogLevel::Info,
        std::format(
            "M1 acceptance passed: exact noise={} pixels, row widths=1/63/64/65/257, buffer round "
            "trips, cancel/delayed retirement, raster/depth/blend/UV/indirect; GPU region {:.3f} "
            "ms (validation enabled timing, not benchmark); memory used={} highWater={} budget={}",
            noiseWidth * noiseHeight,
            double(times[1] - times[0]) * runtime.TimestampPeriodNs() / 1e6, stats.used,
            stats.highWater, stats.budget));
}
} // namespace renderlab::samples
