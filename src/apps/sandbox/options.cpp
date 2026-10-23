#include "apps/sandbox/options.hpp"

#include <charconv>
#include <stdexcept>

namespace renderlab::sandbox
{
Options ParseOptions(std::span<const std::string_view> arguments)
{
    Options options;
    for (size_t i = 0; i < arguments.size(); ++i)
    {
        const auto arg = arguments[i];
        const auto value = [&]()
        {
            if (++i >= arguments.size())
            {
                throw std::invalid_argument("Missing value for " + std::string(arg));
            }
            return arguments[i];
        };
        const auto number = [&]()
        {
            const auto text = value();
            uint32_t result = 0;
            const auto [end, error] =
                std::from_chars(text.data(), text.data() + text.size(), result);
            if (error != std::errc{} || end != text.data() + text.size())
            {
                throw std::invalid_argument("Expected unsigned integer for " + std::string(arg));
            }
            return result;
        };
        if (arg == "--backend")
        {
            options.backend = value();
        }
        else if (arg == "--validation")
        {
            options.validation = value();
        }
        else if (arg == "--adapter")
        {
            options.adapter = number();
        }
        else if (arg == "--exit-after-ms")
        {
            options.exitAfterMs = number();
        }
        else if (arg == "--log")
        {
            options.logPath = value();
        }
        else if (arg == "--frames")
        {
            options.frames = number();
        }
        else if (arg == "--output")
        {
            options.outputPath = value();
        }
        else if (arg == "--report")
        {
            options.reportPath = value();
        }
        else if (arg == "--headless")
        {
            options.headless = true;
        }
        else if (arg == "--window-test")
        {
            options.windowTest = true;
        }
        else if (arg == "--rhi-test")
        {
            options.rhiTest = true;
        }
        else if (arg == "--graph-test")
            options.graphTest = true;
        else if (arg == "--graph-no-cull")
            options.graphNoCull = true;
        else if (arg == "--graph-no-update")
            options.graphNoUpdate = true;
        else if (arg == "--graph-dump")
        {
            options.graphDumpPath = value();
            if (options.graphDumpPath.empty())
                throw std::invalid_argument("--graph-dump cannot be empty");
        }
        else if (arg == "--help" || arg == "-h")
        {
            options.help = true;
        }
        else
        {
            throw std::invalid_argument("Unknown option: " + std::string(arg));
        }
    }
    if (options.backend != "dx12" && options.backend != "vulkan")
    {
        throw std::invalid_argument("--backend must be dx12 or vulkan");
    }
    if (options.validation != "auto" && options.validation != "on" && options.validation != "off")
    {
        throw std::invalid_argument("--validation must be auto, on or off");
    }
    if (options.headless && options.windowTest)
    {
        throw std::invalid_argument("--window-test requires a window");
    }
    if (options.logPath.empty())
    {
        throw std::invalid_argument("--log cannot be empty");
    }
    if (options.outputPath.empty())
        throw std::invalid_argument("--output cannot be empty");
    if (options.headless && options.frames)
        throw std::invalid_argument("--frames requires a window");
    return options;
}

std::string_view Usage()
{
    return "RenderLab M3 Sandbox\n"
           "  --backend dx12|vulkan       Backend (default: dx12)\n"
           "  --adapter N                 Index from this backend's adapter list\n"
           "  --validation auto|on|off    auto warns if validation is unavailable\n"
           "  --headless                  Initialize and report without a window\n"
           "  --exit-after-ms N           Close the window after N milliseconds\n"
           "  --window-test               Exercise resize, minimize, restore and close\n"
           "  --frames N                  Present N frames then close\n"
           "  --rhi-test                  Verify transfers, lifetime, compute and raster readback\n"
           "  --graph-test                Verify graph/direct image equivalence and resource "
           "reuse\n"
           "  --graph-no-cull             Disable graph pass culling in the window demo\n"
           "  --graph-no-update           Omit the noise update Feature pass in the window demo\n"
           "  --graph-dump PATH           Write the first window frame graph plan as JSON\n"
           "  --output PATH               Acceptance images and data (default: out/m1)\n"
           "  --report PATH               Write a JSON capability report\n"
           "  --log PATH                  UTF-8 log (default: logs/renderlab.log)\n"
           "  --help                      Show this help\n";
}
} // namespace renderlab::sandbox
