#include "core/log.hpp"
#include "rendergraph/graph.hpp"
#include "rendergraph/pool.hpp"
#include "rhi/commands.hpp"
#include "rhi/device.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace renderlab;
using namespace rendergraph;
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void Reject(F&& action)
{
    try
    {
        action();
    }
    catch (const std::invalid_argument&)
    {
        return;
    }
    throw std::runtime_error("Expected alias diagnostic");
}
const std::array<rhi::TextureDesc, 2> Descs = {
    rhi::TextureDesc{257, 193, rhi::Format::Rgba8, false, true, "Alias.A"},
    rhi::TextureDesc{129, 97, rhi::Format::Rgba8, false, true, "Alias.B"}};

std::unique_ptr<Graph> Build(rhi::Runtime& runtime, bool alias, uint32_t threads,
                             bool cancel = false)
{
    auto graph = std::make_unique<Graph>();
    PassHandle previous;
    for (uint32_t i = 0; i < Descs.size(); ++i)
    {
        const auto& desc = Descs[i];
        auto texture = graph->CreateTexture(desc.name, desc);
        auto clear = graph->AddRasterPass(desc.name + ".Clear");
        texture = clear.ColorTarget(texture, rhi::LoadOp::Clear,
                                    i == 0 ? std::array<float, 4>{1, 0, 0, 1}
                                           : std::array<float, 4>{0, 1, 0, 1});
        const auto pass = clear.Execute([](RasterContext&) {});
        if (i)
            graph->After(pass, previous);
        const auto pitch = (desc.width * 4 + 255) / 256 * 256;
        auto buffer = graph->ImportBuffer(
            desc.name + ".Readback",
            runtime.CreateBuffer({uint64_t(pitch) * desc.height, rhi::Memory::Readback}),
            {rhi::State::CopyDest, false});
        auto copy = graph->AddCopyPass(desc.name + ".Copy");
        copy.ReadTexture(texture);
        buffer = copy.WriteBuffer(buffer);
        previous = copy.Execute([=](CopyContext& context)
                                { context.CopyTexture(buffer, texture, pitch, false); });
        graph->Export(buffer, rhi::State::HostRead);
    }
    if (cancel)
    {
        auto pass = graph->AddComputePass("Cancel");
        pass.SideEffect();
        pass.Execute([](ComputeContext&)
                     { throw std::invalid_argument("Alias recording cancelled"); });
    }
    const auto& plan =
        graph->Compile({.profile = true, .recordingThreads = threads, .memoryAlias = alias});
    Require(plan.aliasGroups.size() == (alias ? 1u : 0u), "Alias lifetimes were not grouped");
    return graph;
}
std::vector<std::byte> Pixels(rhi::Runtime& runtime, const Submission& submission)
{
    runtime.Wait(submission.completion);
    Require(submission.profile.Read(runtime).size() == 4, "Alias profiling lost passes");
    std::vector<std::byte> result;
    for (uint32_t i = 0; i < Descs.size(); ++i)
    {
        const auto& desc = Descs[i];
        const auto bytes = submission.exports[i].buffer->Map();
        const auto pitch = (desc.width * 4 + 255) / 256 * 256;
        for (uint32_t y = 0; y < desc.height; ++y)
            for (uint32_t x = 0; x < desc.width; ++x)
            {
                const auto pixel = bytes.subspan(size_t(y) * pitch + x * 4, 4);
                Require(pixel[0] == std::byte(i == 0 ? 255 : 0) &&
                            pixel[1] == std::byte(i == 1 ? 255 : 0) && pixel[2] == std::byte(0) &&
                            pixel[3] == std::byte(255),
                        "Alias clear/readback contents differ");
                result.insert(result.end(), pixel.begin(), pixel.end());
            }
    }
    return result;
}
void Native(rhi::Runtime& runtime)
{
    const auto bytes = runtime.QueryAliasedTextureBytes(Descs);
    uint64_t ordinary = 0;
    {
        std::vector<std::shared_ptr<rhi::Texture>> textures;
        for (const auto& desc : Descs)
            textures.push_back(runtime.CreateTexture(desc));
        ordinary = runtime.GetMemoryStats().used;
        const auto stats = runtime.GetMemoryStats();
        Require(stats.reserved >= ordinary && stats.allocations == 2 && stats.blocks > 0 &&
                    stats.blocks <= 2,
                "Dedicated textures lost allocation/backing counts");
    }
    runtime.TrimMemory();
    {
        auto textures = runtime.CreateAliasedTextures(Descs);
        const auto stats = runtime.GetMemoryStats();
        Require(runtime.GetMemoryStats().used == bytes && bytes < ordinary &&
                    stats.reserved == bytes && stats.allocations == 1 && stats.blocks == 1 &&
                    textures[0]->aliasBacking &&
                    textures[0]->aliasBacking == textures[1]->aliasBacking,
                "Alias backing must charge native bytes exactly once and save memory");
        auto pin = textures.back();
        textures.clear();
        Require(runtime.GetMemoryStats().used == bytes, "Alias member did not pin shared memory");
        Graph imported;
        Reject([&] { imported.ImportTexture("ExternalAlias", pin, {}); });
    }
    Require(runtime.GetMemoryStats().used == 0, "Alias backing leaked");
    Require(runtime.GetMemoryStats().reserved == 0 && runtime.GetMemoryStats().blocks == 0 &&
                runtime.GetMemoryStats().allocations == 0,
            "Alias backing/counts leaked");
    const auto limit = runtime.GetMemoryStats().budget;
    runtime.SetAllocationBudget(bytes - 1);
    const auto before = runtime.GetMemoryStats();
    bool rejected = false;
    try
    {
        runtime.CreateAliasedTextures(Descs);
    }
    catch (const std::runtime_error&)
    {
        rejected = true;
    }
    Require(rejected && runtime.GetMemoryStats() == before,
            "Rejected Alias backing changed ledger or escaped its budget");
    runtime.SetAllocationBudget(limit);
    auto incompatible = Descs;
    incompatible[1].format = rhi::Format::R32Float;
    Reject([&] { runtime.CreateAliasedTextures(incompatible); });
    Reject([&] { runtime.CreateAliasedTextures(std::span(Descs).first(1)); });
    std::cout << "ordinary texture bytes=" << ordinary << " aliased bytes=" << bytes << '\n';
    for (const auto format : {rhi::Format::D32, rhi::Format::D32S8, rhi::Format::Rgba8})
    {
        const bool depth = rhi::IsDepthFormat(format);
        const std::array<rhi::TextureDesc, 2> descs = {
            rhi::TextureDesc{8, 8, format, !depth, true, "Alias.Metadata.A"},
            rhi::TextureDesc{8, 8, format, !depth, true, "Alias.Metadata.B"}};
        auto textures = runtime.CreateAliasedTextures(descs);
        auto color =
            runtime.CreateTexture({8, 8, rhi::Format::Rgba8, false, true, "Metadata.Color"});
        auto colorView = runtime.CreateView(color);
        std::shared_ptr<rhi::Texture> previous;
        for (uint32_t frame = 0; frame < 3; ++frame)
        {
            auto commands = runtime.CreateCommands();
            commands->Barrier(color, frame ? rhi::State::ColorAttachment : rhi::State::Undefined,
                              rhi::State::ColorAttachment);
            for (auto& texture : textures)
            {
                commands->ActivateAlias(
                    previous, texture, frame ? rhi::State::Sampled : rhi::State::Undefined,
                    depth ? rhi::State::DepthWrite : rhi::State::ColorAttachment);
                auto view = runtime.CreateView(texture);
                commands->Begin(depth ? colorView : view, depth ? view : nullptr, {1, 0, 0, 1},
                                rhi::LoadOp::Clear, rhi::LoadOp::Clear, rhi::LoadOp::Clear, 7);
                commands->End();
                commands->Barrier(texture,
                                  depth ? rhi::State::DepthWrite : rhi::State::ColorAttachment,
                                  rhi::State::Sampled);
                previous = texture;
            }
            runtime.Wait(runtime.Submit(std::move(commands)));
        }
    }
    Require(runtime.GetMemoryStats().used == 0, "Alias attachment metadata test leaked");
    runtime.TrimMemory();
}
void Graphs(rhi::Runtime& runtime)
{
    std::vector<std::byte> reference;
    for (uint32_t threads : {1u, 4u})
        for (bool alias : {false, true})
        {
            Pool pool(runtime, 16, 32ull * 1024 * 1024);
            // Both live graphs reserve disjoint groups, even if the GPU finishes very quickly.
            auto a = Build(runtime, alias, threads), b = Build(runtime, alias, threads);
            auto first = a->Execute(pool);
            auto second = b->Execute(pool);
            const auto pixels = Pixels(runtime, first);
            if (reference.empty())
                reference = pixels;
            Require(pixels == reference && Pixels(runtime, second) == reference,
                    "Serial/parallel or in-flight alias output differs");
            a.reset();
            b.reset();
            first = {};
            second = {};
            // Reuse must reactivate from the previous frame's last member and native states.
            const auto allocations = pool.Stats().allocations;
            for (uint32_t frame = 0; frame < 3; ++frame)
            {
                auto graph = Build(runtime, alias, threads);
                Require(Pixels(runtime, graph->Execute(pool)) == reference,
                        "Cached alias reactivation differs");
            }
            Require(pool.Stats().allocations == allocations,
                    "Completed alias groups did not reuse");
            {
                auto cancelled = Build(runtime, alias, threads, true);
                Reject([&] { cancelled->Execute(pool); });
            }
            {
                auto graph = Build(runtime, alias, threads);
                Require(Pixels(runtime, graph->Execute(pool)) == reference,
                        "Cancelled recording published alias state");
            }
            pool.Trim();
            Require(pool.Stats().cached == 0 && pool.Stats().estimatedBytes == 0,
                    "Alias pool did not trim whole retired groups");
        }
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "Expected backend and output directory");
        const std::filesystem::path output(argv[2]);
        std::filesystem::create_directories(output);
        Logger logger(output / "alias.log");
        rhi::DeviceOptions options;
        options.backend =
            std::string(argv[1]) == "dx12" ? rhi::Backend::Dx12 : rhi::Backend::Vulkan;
        options.validation = rhi::ValidationMode::On;
        auto device = rhi::CreateDevice(options, logger);
        const auto budgets = device->QueryMemoryBudget();
        for (const auto& budget : budgets)
        {
            Require(!budget.name.empty() && budget.usage.has_value() == budget.budget.has_value(),
                    "Driver budget omitted its identity or one half of the estimate");
            std::cout << "Driver segment=" << budget.name << " known=" << budget.usage.has_value()
                      << '\n';
        }
        {
            auto runtime = device->CreateRuntime();
            Native(*runtime);
            Graphs(*runtime);
            runtime->WaitIdle();
            runtime->TrimMemory();
            Require(runtime->GetMemoryStats().used == 0, "Alias tests leaked native memory");
        }
        device->PollDiagnostics();
        device->Shutdown();
        Require(device->QueryMemoryBudget().empty(), "Shutdown device queried driver memory");
        Require(device->GetValidationErrorCount() == 0, "Alias validation errors");
        std::cout << "Alias contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
