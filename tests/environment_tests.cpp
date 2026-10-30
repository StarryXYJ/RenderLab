#include "reference/environment.hpp"
#include "renderer/environment.hpp"

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
void Compare(const renderer::EnvironmentImage& actual, const renderer::EnvironmentImage& expected)
{
    Require(actual.width == expected.width && actual.height == expected.height &&
                actual.pixels.size() == expected.pixels.size(),
            "Precomputed environment extent mismatch");
    float maximum = 0;
    size_t differences = 0, worst = 0;
    for (size_t i = 0; i < actual.pixels.size(); ++i)
        if (actual.pixels[i] != expected.pixels[i])
        {
            ++differences;
            const auto delta = std::abs(actual.pixels[i] - expected.pixels[i]);
            if (delta > maximum)
            {
                maximum = delta;
                worst = i;
            }
        }
    if (differences)
        std::cout << std::setprecision(9) << "Differing components=" << differences
                  << " max=" << maximum << " at=" << worst << " actual=" << actual.pixels[worst]
                  << " expected=" << expected.pixels[worst] << '\n';
    Require(differences == 0, "Precomputed environment differs from frozen scalar reference");
}
void Contracts()
{
    std::future<renderer::EnvironmentImage> copies[4];
    for (auto& copy : copies)
        copy = std::async(std::launch::async, renderer::IntegrateEnvironmentBrdf);
    const auto expected = tests::reference::IntegrateEnvironmentBrdf();
    for (auto& copy : copies)
        Compare(copy.get(), expected);
    Compare(renderer::PrefilterEnvironment(nullptr, {.25f, .5f, 2}),
            tests::reference::PrefilterEnvironment(nullptr, {.25f, .5f, 2}));
    for (const auto extent :
         {std::pair{1u, 1u}, std::pair{3u, 5u}, std::pair{65u, 33u}, std::pair{2u, 1u}})
    {
        std::cout << "Reference image " << extent.first << 'x' << extent.second << '\n';
        assets::Image image{extent.first, extent.second};
        image.rgba.resize(size_t(image.width) * image.height * 4);
        for (size_t i = 0; i < image.rgba.size(); ++i)
            image.rgba[i] = uint8_t((i * 37 + i / 4) % 256);
        const auto actual = renderer::PrefilterEnvironment(&image, {});
        Compare(actual, tests::reference::PrefilterEnvironment(&image, {}));
        Compare(renderer::PrefilterEnvironment(&image, {}), actual);
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
    Compare(renderer::IntegrateEnvironmentBrdf(), expected);
    auto edited = renderer::IntegrateEnvironmentBrdf();
    edited.pixels[0] = 42;
    Compare(renderer::IntegrateEnvironmentBrdf(), expected);
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
