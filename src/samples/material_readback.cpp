#include "samples/detail/check.hpp"
#include "samples/detail/material_checks.hpp"

#include <cmath>
#include <cstring>
#include <fstream>

namespace renderlab::samples::detail
{
using namespace rhi;
constexpr uint32_t Size = 32, Pitch = 256;
void MaterialConstants(CommandList& commands, const material::Prepared& prepared)
{
    for (const auto& buffer : prepared.constants)
        commands.Barrier(buffer, State::HostWrite, State::UniformRead);
}
MaterialReadback RecordMaterial(Runtime& runtime, const material::Prepared& prepared,
                                const std::shared_ptr<Buffer>& vertices)
{
    auto color =
        runtime.CreateTexture({Size, Size, Format::Rgba8Srgb, false, true, "Material.Color"});
    auto pixels = runtime.CreateBuffer({Pitch * Size, Memory::Readback, "Material.Readback"});
    auto commands = runtime.CreateCommands();
    MaterialConstants(*commands, prepared);
    commands->Barrier(color, State::Undefined, State::ColorAttachment);
    commands->Begin(runtime.CreateView(color));
    commands->Bind(prepared.pipeline);
    commands->Bind(prepared.bindings);
    commands->Geometry(vertices);
    commands->DrawVertices(3);
    commands->End();
    commands->Barrier(color, State::ColorAttachment, State::CopySource);
    commands->CopyTexture(pixels, color, Pitch, false);
    commands->Barrier(pixels, State::CopyDest, State::HostRead);
    return {pixels, std::move(commands)};
}
void CheckMaterialColor(const std::shared_ptr<Buffer>& pixels, std::array<float, 4> expected)
{
    const auto* actual = reinterpret_cast<const uint8_t*>(pixels->Map().data()) +
                         (Size / 2) * Pitch + (Size / 2) * 4;
    for (size_t i = 0; i < 4; ++i)
    {
        const auto linear = expected[i];
        const auto encoded = i == 3                ? linear
                             : linear <= .0031308f ? linear * 12.92f
                                                   : 1.055f * std::pow(linear, 1 / 2.4f) - .055f;
        Require(std::abs(int(actual[i]) - int(std::lround(encoded * 255))) <= 1,
                "Material raster channel " + std::to_string(i) +
                    ": actual=" + std::to_string(actual[i]));
    }
}
void SaveMaterialImage(const std::filesystem::path& path, const std::shared_ptr<Buffer>& pixels)
{
    // Uncompressed top-down BMP, independent of optional image libraries.
    std::array<uint8_t, 54> header{};
    const auto word = [&](size_t offset, uint32_t value)
    { std::memcpy(header.data() + offset, &value, 4); };
    header[0] = 'B';
    header[1] = 'M';
    word(2, 54 + Size * Size * 4);
    word(10, 54);
    word(14, 40);
    word(18, Size);
    word(22, uint32_t(-int32_t(Size)));
    header[26] = 1;
    header[28] = 32;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<char*>(header.data()), header.size());
    const auto data = pixels->Map();
    for (uint32_t y = 0; y < Size; ++y)
        for (uint32_t x = 0; x < Size; ++x)
        {
            const auto* p = data.data() + y * Pitch + x * 4;
            const std::array bgra{p[2], p[1], p[0], p[3]};
            file.write(reinterpret_cast<const char*>(bgra.data()), 4);
        }
    Require(bool(file), "Cannot save material image");
}
} // namespace renderlab::samples::detail
