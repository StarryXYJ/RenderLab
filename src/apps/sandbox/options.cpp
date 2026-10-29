#include "apps/sandbox/options.hpp"

#include <charconv>
#include <cmath>
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
        const auto seconds = [&]()
        {
            const auto text = value();
            double result = 0;
            const auto [end, error] =
                std::from_chars(text.data(), text.data() + text.size(), result);
            if (error != std::errc{} || end != text.data() + text.size() ||
                !std::isfinite(result) || result < 0 || result > 3600)
                throw std::invalid_argument("Expected finite seconds in [0,3600] for " +
                                            std::string(arg));
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
        else if (arg == "--gpu-budget-mib")
        {
            options.gpuBudgetMiB = number();
            if (options.gpuBudgetMiB < 64 || options.gpuBudgetMiB > 16384)
                throw std::invalid_argument("--gpu-budget-mib requires 64..16384");
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
        else if (arg == "--project")
        {
            options.projectPath = value();
            if (options.projectPath.empty())
                throw std::invalid_argument("--project cannot be empty");
        }
        else if (arg == "--report")
        {
            options.reportPath = value();
        }
        else if (arg == "--headless")
        {
            options.headless = true;
        }
        else if (arg == "--width")
            options.width = number();
        else if (arg == "--height")
            options.height = number();
        else if (arg == "--benchmark")
        {
            options.benchmarkFrames = number();
            if (!options.benchmarkFrames || options.benchmarkFrames > 10000)
                throw std::invalid_argument("--benchmark requires 1..10000 measured frames");
        }
        else if (arg == "--quality")
            options.quality = value();
        else if (arg == "--benchmark-seconds")
        {
            options.benchmarkSeconds = seconds();
            if (options.benchmarkSeconds == 0)
                throw std::invalid_argument("--benchmark-seconds must be positive");
        }
        else if (arg == "--warmup-seconds")
            options.warmupSeconds = seconds();
        else if (arg == "--warmup-frames")
        {
            options.warmupFrames = number();
            if (options.warmupFrames > 10000)
                throw std::invalid_argument("--warmup-frames requires 0..10000");
        }
        else if (arg == "--samples")
        {
            options.samplesPath = value();
            if (options.samplesPath.empty())
                throw std::invalid_argument("--samples cannot be empty");
        }
        else if (arg == "--view")
            options.view = value();
        else if (arg == "--temporal-denoise")
        {
            const auto mode = value();
            if (mode != "on" && mode != "off")
                throw std::invalid_argument("--temporal-denoise must be on or off");
            options.reflectionTemporal = mode == "on";
        }
        else if (arg == "--denoise")
        {
            const auto mode = value();
            if (mode != "on" && mode != "off")
                throw std::invalid_argument("--denoise must be on or off");
            options.reflectionDenoise = mode == "on";
        }
        else if (arg == "--pipeline")
            options.pipeline = value();
        else if (arg == "--submission")
            options.submission = value();
        else if (arg == "--profile")
        {
            options.profilePath = value();
            if (options.profilePath.empty())
                throw std::invalid_argument("--profile cannot be empty");
        }
        else if (arg == "--debug-panel")
            options.debugPanel = true;
        else if (arg == "--fixed-camera")
            options.fixedCamera = true;
        else if (arg == "--cpu-profile")
            options.cpuProfile = true;
        else if (arg == "--window-test")
        {
            options.windowTest = true;
        }
        else if (arg == "--rhi-test")
        {
            options.rhiTest = true;
        }
        else if (arg == "--as-test")
            options.accelerationStructureTest = true;
        else if (arg == "--scene-test")
            options.sceneTest = true;
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
    if (options.sceneTest && options.projectPath.empty())
        throw std::invalid_argument("--scene-test requires --project");
    if (!options.width || !options.height || options.width > 16384 || options.height > 16384)
        throw std::invalid_argument("--width and --height must be in [1,16384]");
    if (options.quality != "scene" && options.quality != "low" && options.quality != "medium" &&
        options.quality != "high")
        throw std::invalid_argument("--quality must be scene, low, medium or high");
    if (options.view != "final" && options.view != "hdr" && options.view != "shadow" &&
        options.view != "environment" && options.view != "brdf" &&
        options.view != "reflection-raw" && options.view != "reflection-history" &&
        options.view != "reflection-denoised")
        throw std::invalid_argument("--view must be final, hdr, shadow, environment, brdf, "
                                    "reflection-raw, reflection-history or reflection-denoised");
    if (options.submission != "direct" && options.submission != "indexed-direct" &&
        options.submission != "cpu-indirect")
        throw std::invalid_argument("--submission must be direct, indexed-direct or cpu-indirect");
    if (options.submission != "direct" && options.projectPath.empty())
        throw std::invalid_argument("--submission requires --project");
    if (options.pipeline != "forward" && options.pipeline != "deferred" &&
        options.pipeline != "raytracing")
        throw std::invalid_argument("--pipeline must be forward, deferred or raytracing");
    if (options.headless && options.debugPanel)
        throw std::invalid_argument("--debug-panel requires a window");
    if ((options.debugPanel || options.fixedCamera || options.Benchmark() ||
         !options.profilePath.empty() || options.view != "final" || options.quality != "scene" ||
         options.pipeline != "forward") &&
        options.projectPath.empty())
        throw std::invalid_argument("Scene tools require --project");
    if (options.benchmarkFrames && options.benchmarkSeconds)
        throw std::invalid_argument("Select either --benchmark or --benchmark-seconds");
    if (options.Benchmark() && (!options.headless || options.sceneTest || options.rhiTest ||
                                options.graphTest || options.accelerationStructureTest))
        throw std::invalid_argument("Benchmark requires --headless and excludes acceptance demos");
    if (!options.Benchmark() &&
        (!options.samplesPath.empty() || options.warmupSeconds || options.warmupFrames != 5))
        throw std::invalid_argument("Samples and warmup options require a benchmark");
    if (options.cpuProfile && !options.Benchmark())
        throw std::invalid_argument("--cpu-profile requires a benchmark");
    return options;
}

std::string_view Usage()
{
    return "RenderLab Sandbox\n"
           "  --backend dx12|vulkan       Backend (default: dx12)\n"
           "  --adapter N                 Index from this backend's adapter list\n"
           "  --validation auto|on|off    auto warns if validation is unavailable\n"
           "  --headless                  Initialize and report without a window\n"
           "  --exit-after-ms N           Close the window after N milliseconds\n"
           "  --window-test               Exercise resize, minimize, restore and close\n"
           "  --frames N                  Present N frames then close\n"
           "  --rhi-test                  Verify transfers, lifetime, compute and raster readback\n"
           "  --as-test                   Verify BLAS/TLAS builds and Ray Query\n"
           "  --graph-test                Verify graph/direct image equivalence and resource "
           "reuse\n"
           "  --graph-no-cull             Disable graph pass culling in the window demo\n"
           "  --graph-no-update           Omit the noise update Feature pass in the window demo\n"
           "  --graph-dump PATH           Write the first window frame graph plan as JSON\n"
           "  --output PATH               Acceptance images and data (default: out/m1)\n"
           "  --scene-test               Check the M4 reference scene (--project required)\n"
           "  --project FILE             Load a project scene using Forward PBR/HDR\n"
           "  --width N --height N        Scene output size (default: 1280x720)\n"
           "  --gpu-budget-mib N          Tracked allocation limit (scene default: 2048 MiB)\n"
           "  --quality scene|low|medium|high  Scene shadows, or 128/512/1024 resolution\n"
           "  --temporal-denoise on|off        Camera reprojection denoising (default on)\n"
           "  --denoise on|off                 Spatial reflection denoising (default on)\n"
           "  --view "
           "final|hdr|shadow|environment|brdf|reflection-raw|reflection-history|reflection-"
           "denoised  Select "
           "the displayed texture\n"
           "  --submission direct|indexed-direct|cpu-indirect  Geometry submission\n"
           "  --pipeline forward|deferred|raytracing  Select scene rendering path\n"
           "  --debug-panel               Open native scene controls (F1 toggles)\n"
           "  --fixed-camera              Ignore camera movement keys\n"
           "  --profile PATH              Save CPU/GPU pass timing and memory JSON\n"
           "  --benchmark N               Headless: 5 warmup frames then N measured frames\n"
           "  --benchmark-seconds S       Headless: measure for S seconds (excludes warmup)\n"
           "  --cpu-profile               Benchmark: record RHI CPU operations and call counts\n"
           "  --warmup-frames N           Minimum warmup frames (default: 5)\n"
           "  --warmup-seconds S          Minimum warmup seconds (default: 0)\n"
           "  --samples PATH              Benchmark raw JSONL (default: "
           "OUTPUT/BACKEND/frames.jsonl)\n"
           "  --report PATH               Write a JSON capability report\n"
           "  --log PATH                  UTF-8 log (default: logs/renderlab.log)\n"
           "  --help                      Show this help\n";
}
} // namespace renderlab::sandbox
