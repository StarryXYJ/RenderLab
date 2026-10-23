#include "samples/graph_demo.hpp"

#include "core/file_io.hpp"
#include "core/hash.hpp"
#include "rhi/commands.hpp"
#include "rhi/upload.hpp"
#include "samples/detail/check.hpp"
#include "samples/image_io.hpp"
#include "samples/rhi_demo.hpp"

#include <cstring>
#include <vector>

namespace renderlab::samples
{
namespace
{
using namespace rhi;
using detail::Require;
constexpr uint32_t Width = 640, Height = 360;
std::shared_ptr<Buffer> Reference(Runtime& runtime, const std::filesystem::path& shaders,
                                  Logger& logger)
{
    RhiDemo demo(runtime, shaders, logger);
    auto color =
        runtime.CreateTexture({Width, Height, Format::Rgba8Srgb, false, true, "Graph.Reference"});
    auto depth =
        runtime.CreateTexture({Width, Height, Format::D32, false, true, "Graph.Reference.Depth"});
    auto readback = runtime.CreateBuffer(
        {uint64_t(TextureRowPitch(Width)) * Height, Memory::Readback, "Graph.Reference.Readback"});
    auto commands = runtime.CreateCommands();
    commands->Barrier(color, State::Undefined, State::ColorAttachment);
    commands->Barrier(depth, State::Undefined, State::DepthWrite);
    demo.Render(*commands, runtime.CreateView(color), runtime.CreateView(depth));
    commands->Barrier(color, State::ColorAttachment, State::CopySource);
    commands->CopyTexture(readback, color, TextureRowPitch(Width), false);
    commands->Barrier(readback, State::CopyDest, State::HostRead);
    runtime.Wait(runtime.Submit(std::move(commands)));
    return readback;
}
} // namespace
void VerifyGraph(rhi::Runtime& runtime, const std::filesystem::path& shaders,
                 const std::filesystem::path& output, Logger& logger)
{
    using namespace rhi;
    using detail::Require;
    std::filesystem::create_directories(output);
    const auto baseline = Reference(runtime, shaders, logger);
    GraphDemo demo(runtime, shaders);
    auto target =
        runtime.CreateTexture({Width, Height, Format::Rgba8Srgb, false, true, "Graph.Acceptance"});
    for (uint32_t frame = 0; frame < 6; ++frame)
    {
        const bool cull = frame % 2 == 0;
        const bool update = frame != 3;
        auto result = demo.Render(target, frame ? State::CopySource : State::Undefined, nullptr,
                                  true, {update, frame != 4, cull},
                                  output / ("plan-" + std::to_string(frame) + ".json"));
        runtime.Wait(result.completion);
        if (update)
        {
            const auto pixels = result.color->Map(), expected = baseline->Map();
            Require(pixels.size() == expected.size() &&
                        std::memcmp(pixels.data(), expected.data(), pixels.size()) == 0,
                    "Graph and direct RHI render differ (Clear/Load/depth/blend/indirect)");
        }
        const auto bytes = result.noise->Map();
        std::vector<std::byte> tight(257 * 131 * 4);
        for (uint32_t y = 0; y < 131; ++y)
        {
            std::memcpy(tight.data() + y * 257 * 4, bytes.data() + y * TextureRowPitch(257),
                        257 * 4);
            for (uint32_t x = 0; x < 257; ++x)
            {
                auto hash = x + y * 257 + 0x12345678u;
                hash ^= hash >> 16;
                hash *= 0x7feb352d;
                hash ^= hash >> 15;
                hash *= 0x846ca68b;
                hash ^= hash >> 16;
                const std::array<uint8_t, 4> expected{
                    uint8_t(hash ^ (update ? 0x5a : 0)), uint8_t((hash >> 8) ^ (update ? 0xa5 : 0)),
                    uint8_t((hash >> 16) ^ (update ? 0x3c : 0)), 255};
                Require(std::memcmp(tight.data() + (y * 257 + x) * 4, expected.data(), 4) == 0,
                        "Graph compute noise/reference mismatch");
            }
        }
        if (frame == 0)
        {
            Require(Sha256(tight) ==
                        "ed59cd2517b5c5c076a93c45fda5a61aa1f6f43ff52f96af600bec1059be9527",
                    "Graph noise SHA-256 changed");
            WriteFileAtomically(output / "noise.rgba", tight);
            SaveImage(output / "noise.bmp", bytes, 257, 131, TextureRowPitch(257));
            SaveImage(output / "triangle-noise.bmp", result.color->Map(), Width, Height,
                      TextureRowPitch(Width));
        }
    }
    VerifyGraphBuffers(runtime, shaders, output);
    const auto stats = demo.Stats();
    Require(stats.reuses > 0 && stats.allocations < 12,
            "Graph resource pool did not reuse completed frame resources");
    logger.Write(LogLevel::Info, "M3 acceptance passed: six per-frame graphs, conditional Feature, "
                                 "culling enabled/disabled, graph-generated barriers, Clear/Load, "
                                 "exact direct-RHI image and compute noise, bounded pool reuse=" +
                                     std::to_string(stats.reuses));
}
} // namespace renderlab::samples
