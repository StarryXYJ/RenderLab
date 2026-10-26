#include "samples/common/image_io.hpp"
#include "samples/common/check.hpp"

#include <array>
#include <cstring>
#include <fstream>

namespace renderlab::samples
{
using detail::Require;
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
} // namespace renderlab::samples
