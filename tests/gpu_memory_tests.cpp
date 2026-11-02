#include "core/file_io.hpp"
#include "core/hash.hpp"
#include "core/log.hpp"
#include "rhi/commands.hpp"
#include "rhi/device.hpp"
#include "rhi/runtime.hpp"

#include <Windows.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace renderlab;
using namespace rhi;
using Json = nlohmann::json;
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
std::string FileHash(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    Require(bool(input), "Cannot read measurement identity");
    const std::string bytes{std::istreambuf_iterator<char>(input),
                            std::istreambuf_iterator<char>()};
    return Sha256(bytes);
}
Json Memory(MemoryStats value)
{
    return {{"allocatedBytes", value.used},          {"reservedBytes", value.reserved},
            {"allocationCount", value.allocations},  {"blockCount", value.blocks},
            {"allocatedPeakBytes", value.highWater}, {"reservedPeakBytes", value.reservedHighWater},
            {"blockPeak", value.blockHighWater}};
}
void Measure(Device& device, const std::filesystem::path& output)
{
    Require(!std::filesystem::exists(output / "measure.json"), "Refusing to overwrite measurement");
    Json report{
        {"schemaVersion", 1},
        {"backend", device.GetReport().backend},
        {"adapter", device.GetReport().adapters.at(device.GetReport().selectedAdapter).name},
        {"driver", device.GetReport().driverVersion},
        {"compiler", _MSC_FULL_VER},
        {"validation", device.GetReport().validationEnabled},
        {"scope", "CPU resource create/release; fresh Runtime per case; no GPU work or FPS"},
        {"cases", Json::array()}};
#ifdef RENDERLAB_DX12_MEMORY_ALLOCATOR
    report["dx12AllocatorEnabled"] = bool(RENDERLAB_DX12_MEMORY_ALLOCATOR);
#endif
    wchar_t executable[32768]{};
    const auto length = GetModuleFileNameW(nullptr, executable, 32768);
    Require(length > 0 && length < 32768, "Cannot identify measurement executable");
    report["executableSha256"] = FileHash(executable);
#ifdef NDEBUG
    report["configuration"] = "Release";
#else
    report["configuration"] = "Debug";
#endif
    for (const auto* source :
         {"src/rhi/resources.cpp", "src/rhi/resources.hpp", "src/rhi/runtime.cpp",
          "src/rhi/runtime.hpp", "src/rhi/dx12/dx12_objects.hpp", "src/rhi/dx12/dx12_resources.cpp",
          "src/rhi/dx12/dx12_runtime.cpp", "src/rhi/dx12/dx12_memory.cpp",
          "src/rhi/dx12/dx12_memory.hpp", "tests/gpu_memory_tests.cpp", "cmake/Dependencies.cmake"})
    {
        const auto path = std::filesystem::path(RENDERLAB_SOURCE_ROOT) / source;
        report["sourceSha256"][source] =
            std::filesystem::exists(path) ? Json(FileHash(path)) : Json(nullptr);
    }
    using Clock = std::chrono::steady_clock;
    const auto ms = [](auto delta)
    { return std::chrono::duration<double, std::milli>(delta).count(); };
    for (uint32_t kind = 0; kind < 4; ++kind)
    {
        const auto runtimeStart = Clock::now();
        auto runtime = device.CreateRuntime();
        const auto runtimeCreated = Clock::now();
        runtime->SetAllocationBudget(128ull * 1024 * 1024);
        const uint32_t count = kind == 3 ? 128 : 256;
        Json entry{{"kind", kind},
                   {"count", count},
                   {"runtimeCreateMs", ms(runtimeCreated - runtimeStart)},
                   {"iterations", Json::array()}};
        for (uint32_t iteration = 0; iteration < 6; ++iteration)
        {
            std::vector<std::shared_ptr<Object>> objects;
            objects.reserve(count);
            const auto start = Clock::now();
            for (uint32_t i = 0; i < count; ++i)
                if (kind == 3)
                    objects.push_back(runtime->CreateTexture(
                        {256, 256, Format::Rgba8, false, true, "Memory.Measure.Texture"}));
                else
                    objects.push_back(runtime->CreateBuffer({1024,
                                                             kind == 0   ? rhi::Memory::Device
                                                             : kind == 1 ? rhi::Memory::Upload
                                                                         : rhi::Memory::Readback,
                                                             "Memory.Measure.Buffer"}));
            const auto created = Clock::now();
            const auto live = runtime->GetMemoryStats();
            Require(live.allocations == count && live.used <= live.reserved &&
                        live.reserved <= live.budget,
                    "Invalid measurement allocation accounting");
            const auto releasing = Clock::now();
            objects.clear();
            const auto released = Clock::now();
            const auto idle = runtime->GetMemoryStats();
            Require(!idle.used && !idle.allocations, "Measurement retained resource allocations");
            entry["iterations"].push_back({{"cold", iteration == 0},
                                           {"createMs", ms(created - start)},
                                           {"releaseMs", ms(released - releasing)},
                                           {"live", Memory(live)},
                                           {"idle", Memory(idle)}});
        }
        report["cases"].push_back(entry);
    }
    const auto text = report.dump(2) + "\n";
    WriteFileAtomically(output / "measure.json",
                        std::as_bytes(std::span(text.data(), text.size())));
}
#ifndef RENDERLAB_MEASURE_ONLY
void Empty(Runtime& runtime)
{
    runtime.TrimMemory();
    const auto stats = runtime.GetMemoryStats();
    Require(!stats.used && !stats.reserved && !stats.allocations && !stats.blocks,
            "Trim did not retire allocations/backing completely");
}
void Contracts(Runtime& runtime)
{
    runtime.SetAllocationBudget(64ull * 1024 * 1024);
    {
        std::vector<std::shared_ptr<Buffer>> buffers;
        for (uint32_t i = 0; i < 96; ++i)
        {
            auto buffer = runtime.CreateBuffer({1024, rhi::Memory::Upload, "Memory.Shared"});
            std::fill(buffer->Map().begin(), buffer->Map().end(), std::byte(i));
            buffers.push_back(std::move(buffer));
        }
        for (uint32_t i = 0; i < buffers.size(); ++i)
            Require(
                std::ranges::all_of(buffers[i]->Map(), [=](auto b) { return b == std::byte(i); }),
                "Live suballocations overlapped");
        const auto before = runtime.GetMemoryStats();
        Require(before.allocations == 96 && before.blocks < before.allocations &&
                    before.reserved >= before.used,
                "Resources did not share native blocks");
        runtime.TrimMemory();
        Require(runtime.GetMemoryStats() == before, "Trim retired live backing");
    }
    const auto idle = runtime.GetMemoryStats();
    Require(!idle.used && !idle.allocations && idle.reserved > 0 && idle.blocks > 0,
            "Idle cached blocks were hidden or unexpectedly lost");
    bool rejected = false;
    try
    {
        runtime.SetAllocationBudget(idle.reserved - 1);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Require(rejected, "Budget ignored cached blocks");
    Empty(runtime);
    runtime.SetAllocationBudget(65536);
    {
        auto buffer = runtime.CreateBuffer({1024, rhi::Memory::Upload, "Memory.SmallBudget"});
        const auto before = runtime.GetMemoryStats();
        Require(before.allocations == 1 && before.blocks == 1 && before.reserved <= 65536,
                "Small budget required a full pool block");
        rejected = false;
        try
        {
            runtime.CreateBuffer({65536, rhi::Memory::Upload, "Memory.Reject"});
        }
        catch (const std::runtime_error&)
        {
            rejected = true;
        }
        Require(rejected && runtime.GetMemoryStats() == before,
                "Rejected resource changed charges or peaks");
        auto commands = runtime.CreateCommands();
        commands->Barrier(buffer, State::HostWrite, State::CopySource);
        std::weak_ptr<Buffer> weak = buffer;
        buffer.reset();
        runtime.TrimMemory();
        Require(!weak.expired() && runtime.GetMemoryStats().used > 0,
                "Recorded command lost its backing");
        commands->Cancel();
        Require(weak.expired(), "Cancelled command retained its allocation");
    }
    Empty(runtime);
    runtime.SetAllocationBudget(64ull * 1024 * 1024);
    {
        auto source = runtime.CreateBuffer({256, rhi::Memory::Upload, "Memory.Source"});
        auto target = runtime.CreateBuffer({256, rhi::Memory::Readback, "Memory.Target"});
        std::fill(source->Map().begin(), source->Map().end(), std::byte{73});
        auto commands = runtime.CreateCommands();
        commands->Barrier(source, State::HostWrite, State::CopySource);
        commands->Copy(source, target, 256);
        commands->Barrier(target, State::CopyDest, State::HostRead);
        std::weak_ptr<Buffer> weak = source;
        source.reset();
        const auto point = runtime.Submit(std::move(commands));
        runtime.TrimMemory();
        runtime.Wait(point);
        Require(weak.expired(), "Completed submission pinned its allocation");
        Require(std::ranges::all_of(target->Map(), [](auto b) { return b == std::byte{73}; }),
                "Suballocated GPU copy corrupted content");
    }
    Empty(runtime);
    {
        auto large =
            runtime.CreateBuffer({20ull * 1024 * 1024, rhi::Memory::Device, "Memory.Large"});
        const auto stats = runtime.GetMemoryStats();
        Require(stats.used == stats.reserved && stats.blocks == 1,
                "Large committed resource did not use dedicated accounting");
    }
    Empty(runtime);
}
#endif
} // namespace
int wmain(int argc, wchar_t** argv)
{
    try
    {
        Require(argc == 3 || argc == 4, "Expected backend, output directory, optional --measure");
        const bool measure = argc == 4 && std::wstring_view(argv[3]) == L"--measure";
        Require(argc == 3 || measure, "Unknown measurement option");
        Require(std::wstring_view(argv[1]) == L"dx12" || std::wstring_view(argv[1]) == L"vulkan",
                "Unknown backend");
#ifdef RENDERLAB_MEASURE_ONLY
        Require(measure, "Baseline binary supports measurement only");
#endif
#if defined(RENDERLAB_DX12_MEMORY_ALLOCATOR) && !RENDERLAB_DX12_MEMORY_ALLOCATOR
        Require(measure, "Enable RENDERLAB_DX12_MEMORY_ALLOCATOR to run allocator contracts");
#endif
        const std::filesystem::path output(argv[2]);
        std::filesystem::create_directories(output);
        Logger logger(output / "memory.log");
        DeviceOptions options;
        options.backend = std::wstring_view(argv[1]) == L"dx12" ? Backend::Dx12 : Backend::Vulkan;
        options.validation = measure ? ValidationMode::Off : ValidationMode::On;
        auto device = CreateDevice(options, logger);
        if (measure)
            Measure(*device, output);
#ifndef RENDERLAB_MEASURE_ONLY
        else
        {
            auto runtime = device->CreateRuntime();
            Contracts(*runtime);
        }
#endif
        device->PollDiagnostics();
        device->Shutdown();
        Require(!device->GetValidationErrorCount(), "Memory validation errors");
        std::cout << "GPU memory contracts/measurement passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
