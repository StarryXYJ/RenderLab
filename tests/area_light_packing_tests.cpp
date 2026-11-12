#include "renderer/area_light.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

int main()
{
    try
    {
        using namespace renderlab;
        const auto empty = renderer::PackPathRectLights({});
        if (!std::ranges::all_of(empty, [](float value) { return value == 0; }))
            throw std::runtime_error("Empty area-light constants are not zero");
        std::array<scene::AreaLight, 2> lights{};
        lights[0].center = {1, 2, 3};
        lights[0].edgeU = {4, 5, 6};
        lights[0].edgeV = {7, 8, 9};
        lights[0].radiance = {10, 11, 12};
        lights[1].center = {13, 14, 15};
        lights[1].radiance = {16, 17, 18};
        const auto packed = renderer::PackPathRectLights(lights);
        if (packed != renderer::PackRectLights(lights))
            throw std::runtime_error("Raster and path area-light layouts differ");
        const std::array<float, 16> expected{2, 0, 0, 0, 1, 2, 3, 10, 4, 5, 6, 11, 7, 8, 9, 12};
        if (!std::equal(expected.begin(), expected.end(), packed.begin()) || packed[16] != 13 ||
            packed[19] != 16 || packed[23] != 17 || packed[27] != 18)
            throw std::runtime_error("Rectangular area-light constants layout changed");
        bool rejected = false;
        try
        {
            std::array<scene::AreaLight, 3> excess{};
            (void)renderer::PackPathRectLights(excess);
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Area-light capacity overflow was not rejected");
        std::cout << "Rectangular light packing contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
