#include "renderer/ltc_table.hpp"

#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace renderlab::renderer
{
namespace
{
uint32_t Read32(std::span<const std::byte> data, size_t offset)
{
    uint32_t value = 0;
    for (size_t i = 0; i < 4; ++i)
        value |= uint32_t(std::to_integer<uint8_t>(data[offset + i])) << (8 * i);
    return value;
}
float Half(uint16_t bits)
{
    const auto exponent = (bits >> 10) & 31;
    const auto mantissa = bits & 1023;
    if (exponent == 31)
        throw std::invalid_argument("Nonfinite LTC table coefficient");
    const float value = exponent ? std::ldexp(1.f + float(mantissa) / 1024.f, exponent - 15)
                                 : std::ldexp(float(mantissa), -24);
    return (bits & 0x8000) ? -value : value;
}
} // namespace
EnvironmentImage DecodeLtcTable(std::span<const std::byte> bytes)
{
    constexpr size_t Header = 128, Width = 64, Height = 64, Channels = 4, BytesPerHalf = 2;
    constexpr size_t Expected = Header + Width * Height * Channels * BytesPerHalf;
    if (bytes.size() != Expected || Read32(bytes, 0) != 0x20534444 || Read32(bytes, 4) != 124 ||
        Read32(bytes, 12) != Height || Read32(bytes, 16) != Width || Read32(bytes, 20) != 512 ||
        Read32(bytes, 28) != 1 || Read32(bytes, 76) != 32 || Read32(bytes, 80) != 4 ||
        Read32(bytes, 84) != 113)
        throw std::invalid_argument("Unsupported LTC DDS table");
    EnvironmentImage image{Width, Height, {}};
    image.pixels.resize(Width * Height * Channels);
    for (size_t i = 0; i < image.pixels.size(); ++i)
    {
        const size_t offset = Header + BytesPerHalf * i;
        const uint16_t bits = uint16_t(std::to_integer<uint8_t>(bytes[offset])) |
                              uint16_t(uint16_t(std::to_integer<uint8_t>(bytes[offset + 1])) << 8);
        image.pixels[i] = Half(bits);
    }
    return image;
}
} // namespace renderlab::renderer
