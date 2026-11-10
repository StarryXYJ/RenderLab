#include "assets/io.hpp"
#include "renderer/ltc_table.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void Reject(F&& operation)
{
    bool rejected = false;
    try
    {
        operation();
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Require(rejected, "Invalid LTC DDS was accepted");
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "Expected shader directory");
        const auto dir = std::filesystem::path(argv[1]) / "RenderLab";
        auto matrix = renderlab::assets::detail::Read(dir / "LtcMatrix.dds");
        auto amplitude = renderlab::assets::detail::Read(dir / "LtcAmplitude.dds");
        const auto decoded = renderlab::renderer::DecodeLtcTable(matrix);
        const auto decodedAmplitude = renderlab::renderer::DecodeLtcTable(amplitude);
        Require(decoded.width == 64 && decoded.height == 64 && decoded.pixels.size() == 64 * 64 * 4,
                "Incorrect LTC table dimensions");
        Require(decoded.pixels[0] == 1 && decoded.pixels[1] == 0 && decoded.pixels[2] == 0 &&
                    std::abs(decoded.pixels[3] - 2.002716064453125e-5f) < 1e-12f &&
                    decodedAmplitude.pixels[0] == 1,
                "DDS channel order, signed value or half-float decoding is wrong");
        auto bad = matrix;
        bad[0] = std::byte{0};
        Reject([&] { (void)renderlab::renderer::DecodeLtcTable(bad); });
        bad = matrix;
        bad.pop_back();
        Reject([&] { (void)renderlab::renderer::DecodeLtcTable(bad); });
        bad = matrix;
        bad[84] = std::byte{0};
        Reject([&] { (void)renderlab::renderer::DecodeLtcTable(bad); });
        bad = matrix;
        bad[128] = std::byte{0};
        bad[129] = std::byte{0x7c};
        Reject([&] { (void)renderlab::renderer::DecodeLtcTable(bad); });
        std::cout << "LTC data contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
