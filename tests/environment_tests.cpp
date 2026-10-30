#include "reference/environment.hpp"
#include "renderer/environment.hpp"

#ifdef RENDERLAB_ENVIRONMENT_SIMD
#include "environment_ispc.h"
#include <array>
#include <vector>
#endif

#include <algorithm>
#include <cmath>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace
{
using namespace renderlab;
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void Fails(F&& work)
{
    bool failed = false;
    try
    {
        work();
    }
    catch (const std::invalid_argument&)
    {
        failed = true;
    }
    Require(failed, "Invalid environment input accepted");
}
void Compare(const renderer::EnvironmentImage& actual, const renderer::EnvironmentImage& expected,
             [[maybe_unused]] float simdError = 2e-6f)
{
    Require(actual.width == expected.width && actual.height == expected.height &&
                actual.pixels.size() == expected.pixels.size(),
            "Precomputed environment extent mismatch");
    float maximum = 0, filteredMaximum = 0;
    size_t differences = 0, worst = 0;
    for (size_t i = 0; i < actual.pixels.size(); ++i)
        if (actual.pixels[i] != expected.pixels[i])
        {
            ++differences;
            const auto delta = std::abs(actual.pixels[i] - expected.pixels[i]);
            if (i >= size_t(renderer::EnvironmentWidth) * renderer::EnvironmentHeight * 4)
                filteredMaximum = std::max(filteredMaximum, delta);
            if (delta > maximum)
            {
                maximum = delta;
                worst = i;
            }
        }
    if (differences)
        std::cout << std::setprecision(9) << "Differing components=" << differences
                  << " max=" << maximum << " at=" << worst << " actual=" << actual.pixels[worst]
                  << " expected=" << expected.pixels[worst] << " filteredMax=" << filteredMaximum
                  << '\n';
#ifdef RENDERLAB_ENVIRONMENT_SIMD
    for (size_t i = 0; i < actual.pixels.size(); ++i)
    {
        if (actual.height == renderer::EnvironmentHeight * (renderer::EnvironmentLevels + 1) &&
            i < size_t(renderer::EnvironmentWidth) * renderer::EnvironmentHeight * 4)
            Require(actual.pixels[i] == expected.pixels[i], "Unfiltered tile changed");
        Require(std::isfinite(actual.pixels[i]) &&
                    std::abs(actual.pixels[i] - expected.pixels[i]) <= simdError,
                "SIMD environment exceeds scalar error bound");
    }
#else
    Require(differences == 0, "Precomputed environment differs from frozen scalar reference");
#endif
}
#ifdef RENDERLAB_ENVIRONMENT_SIMD
void SimdRanges()
{
    const uint8_t rgba[]{255, 255, 255, 255};
    std::array<float, 256> linear{};
    linear[255] = 1;
    std::vector<ispc::EnvironmentFrame> frames(2048, {0, 0, 1, 1, 0, 0, 0, 1, 0});
    std::vector<ispc::EnvironmentDirection> directions(7 * 128, {0, 0, 1});
    for (const int count : {0, 1, 3, 4, 5, 7, 8, 9, 65, 14333})
    {
        constexpr int begin = 3, pixels = 7 * 2048;
        std::vector<float> output(pixels * 4, -42);
        ispc::FilterEnvironment(rgba, 1, 1, linear.data(), frames.data(), directions.data(), begin,
                                begin + count, output.data());
        for (int p = 0; p < pixels; ++p)
            for (int c = 0; c < 4; ++c)
                Require(output[p * 4 + c] == (p >= begin && p < begin + count ? 1.f : -42.f),
                        "SIMD empty/tail range overwrote a guard or missed a lane");
    }
}
#endif
void Contracts()
{
    std::future<renderer::EnvironmentImage> copies[4];
    for (auto& copy : copies)
        copy = std::async(std::launch::async, renderer::IntegrateEnvironmentBrdf);
    const auto expected = tests::reference::IntegrateEnvironmentBrdf();
    for (auto& copy : copies)
        Compare(copy.get(), expected, 0);
    Compare(renderer::PrefilterEnvironment(nullptr, {.25f, .5f, 2}),
            tests::reference::PrefilterEnvironment(nullptr, {.25f, .5f, 2}));
    for (const auto extent : {std::pair{1u, 1u}, std::pair{3u, 5u}, std::pair{65u, 33u},
                              std::pair{2u, 1u}, std::pair{8192u, 1u}, std::pair{1u, 8192u}})
    {
        std::cout << "Reference image " << extent.first << 'x' << extent.second << '\n';
        assets::Image image{extent.first, extent.second};
        image.rgba.resize(size_t(image.width) * image.height * 4);
        for (size_t i = 0; i < image.rgba.size(); ++i)
            image.rgba[i] = uint8_t((i * 37 + i / 4) % 256);
        const auto actual = renderer::PrefilterEnvironment(&image, {});
        Compare(actual, tests::reference::PrefilterEnvironment(&image, {}),
                std::max(image.width, image.height) > 65 ? 2e-4f : 2e-6f);
        Require(renderer::PrefilterEnvironment(&image, {}).pixels == actual.pixels,
                "Environment is not deterministic");
        for (float value : actual.pixels)
            Require(std::isfinite(value) && value >= 0 && value <= 1.00001f,
                    "Environment radiance/alpha outside source range");
    }
    assets::Image constant{3, 2};
    constant.rgba.resize(3 * 2 * 4, 255);
    const auto white = renderer::PrefilterEnvironment(&constant, {});
    for (float value : white.pixels)
        Require(std::abs(value - 1) <= 2e-6f, "Constant environment lost energy");
    assets::Image impulse{65, 33};
    impulse.rgba.resize(65 * 33 * 4);
    impulse.rgba[0] = impulse.rgba[1] = impulse.rgba[2] = 255;
    Compare(renderer::PrefilterEnvironment(&impulse, {}),
            tests::reference::PrefilterEnvironment(&impulse, {}));
    Compare(renderer::IntegrateEnvironmentBrdf(), expected, 0);
    auto edited = renderer::IntegrateEnvironmentBrdf();
    edited.pixels[0] = 42;
    Compare(renderer::IntegrateEnvironmentBrdf(), expected, 0);
    assets::Image empty;
    Fails([&] { renderer::PrefilterEnvironment(&empty, {}); });
    assets::Image truncated{2, 2, {0, 0, 0, 255}};
    Fails([&] { renderer::PrefilterEnvironment(&truncated, {}); });
    assets::Image excessive{8193, 1};
    Fails([&] { renderer::PrefilterEnvironment(&excessive, {}); });
    Fails([] { renderer::PrefilterEnvironment(nullptr, {-1, 0, 0}); });
    Fails(
        []
        {
            renderer::PrefilterEnvironment(nullptr,
                                           {std::numeric_limits<float>::quiet_NaN(), 0, 0});
        });
    Fails(
        []
        {
            renderer::PrefilterEnvironment(nullptr, {0, std::numeric_limits<float>::infinity(), 0});
        });
}
} // namespace
int main()
{
    try
    {
        Contracts();
#ifdef RENDERLAB_ENVIRONMENT_SIMD
        SimdRanges();
#endif
        std::cout
            << "Environment contracts passed: scalar reference, edges, energy, cache ownership\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
