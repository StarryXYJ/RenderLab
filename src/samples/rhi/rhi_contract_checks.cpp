#include "samples/rhi/rhi_contract_checks.hpp"

#include "rhi/commands.hpp"
#include "rhi/runtime.hpp"
#include "rhi/upload.hpp"
#include "samples/common/check.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <memory>
#include <stdexcept>
#include <vector>

namespace renderlab::samples
{
using namespace rhi;
using detail::Require;

namespace
{
void CheckTransfers(Runtime& runtime)
{
    // Sizes on both sides of D3D row-pitch boundaries, including single-pixel and odd rows.
    for (const uint32_t width : {1u, 63u, 64u, 65u, 257u})
    {
        constexpr uint32_t height = 5;
        const auto pitch = TextureRowPitch(width);
        std::vector<std::byte> data(pitch * height, std::byte{0xcd});
        for (uint32_t y = 0; y < height; ++y)
            for (uint32_t x = 0; x < width * 4; ++x)
                data[y * pitch + x] = std::byte((x * 37 + y * 11) & 255);
        UploadArena upload(runtime, data.size() + 1024);
        const std::array<std::byte, 1> padding{std::byte{1}};
        upload.Write(padding);
        auto slice = upload.Write(data);
        Require(slice.offset == 512, "nonzero texture upload offset alignment");
        auto texture =
            runtime.CreateTexture({width, height, Format::Rgba8, false, false, "Transfer.Pattern"});
        auto readback = runtime.CreateBuffer({data.size(), Memory::Readback, "Transfer.Readback"});
        auto buffer = runtime.CreateBuffer({data.size(), Memory::Device, "Transfer.Buffer"});
        auto bufferReadback =
            runtime.CreateBuffer({data.size(), Memory::Readback, "Buffer.Readback"});
        auto commands = runtime.CreateCommands();
        commands->Barrier(slice.buffer, State::HostWrite, State::CopySource);
        commands->Barrier(texture, State::Undefined, State::CopyDest);
        commands->CopyTexture(slice.buffer, texture, pitch, true, slice.offset);
        commands->Barrier(texture, State::CopyDest, State::CopySource);
        commands->CopyTexture(readback, texture, pitch, false);
        commands->Barrier(readback, State::CopyDest, State::HostRead);
        commands->Barrier(buffer, State::Undefined, State::CopyDest);
        commands->Copy(slice.buffer, buffer, data.size(), slice.offset);
        commands->Barrier(buffer, State::CopyDest, State::CopySource);
        commands->Copy(buffer, bufferReadback, data.size());
        commands->Barrier(bufferReadback, State::CopyDest, State::HostRead);
        runtime.Wait(runtime.Submit(std::move(commands)));
        const auto actual = readback->Map();
        for (uint32_t y = 0; y < height; ++y)
            Require(std::memcmp(actual.data() + y * pitch, data.data() + y * pitch, width * 4) == 0,
                    std::format("texture round trip width={} row={}", width, y));
        Require(std::memcmp(bufferReadback->Map().data(), data.data(), data.size()) == 0,
                "buffer round trip");
        upload.Reset();
        Require(upload.Used() == 0 && upload.HighWater() > 512, "upload reset/high water");
        bool exhausted = false;
        try
        {
            upload.Write(std::vector<std::byte>(data.size() + 2048));
        }
        catch (const std::invalid_argument&)
        {
            exhausted = true;
        }
        Require(exhausted, "bounded upload capacity");
    }
}
void CheckLifetime(Runtime& runtime)
{
    const auto baseline = runtime.GetMemoryStats();
    const auto sameLiveMemory = [&]
    {
        const auto stats = runtime.GetMemoryStats();
        return stats.used == baseline.used && stats.allocations == baseline.allocations &&
               stats.reserved >= stats.used && (stats.blocks > 0 || stats.reserved == 0);
    };
    auto buffer = runtime.CreateBuffer({512, Memory::Device, "Lifetime.Cancel"});
    std::weak_ptr<Buffer> weak = buffer;
    auto cancel = runtime.CreateCommands();
    cancel->Barrier(buffer, State::Undefined, State::CopyDest);
    buffer.reset();
    Require(!weak.expired(), "recorded command did not retain resource");
    cancel->Cancel();
    Require(weak.expired(), "cancel did not release resource");
    Require(sameLiveMemory(), "cancel leaked allocation/backing counts");

    auto source = runtime.CreateBuffer({512, Memory::Upload, "Lifetime.DelayedSource"});
    std::fill(source->Map().begin(), source->Map().end(), std::byte{0x73});
    auto readback = runtime.CreateBuffer({512, Memory::Readback, "Lifetime.DelayedReadback"});
    auto commands = runtime.CreateCommands();
    commands->Barrier(source, State::HostWrite, State::CopySource);
    commands->Copy(source, readback, 512);
    commands->Barrier(readback, State::CopyDest, State::HostRead);
    weak = source;
    source.reset();
    Require(!weak.expired(), "delayed submission lost upload source");
    const auto point = runtime.Submit(std::move(commands));
    // Submission may already have completed; never assume the GPU is slower than this CPU.
    runtime.Wait(point);
    Require(weak.expired(), "completed submission retained upload source");
    Require(std::ranges::all_of(readback->Map(), [](auto b) { return b == std::byte{0x73}; }),
            "delayed submission corrupted source");
    readback.reset();
    Require(sameLiveMemory() && runtime.PendingCount() == 0,
            "completion did not retire allocations");
}
void CheckUploadRing(Runtime& runtime)
{
    UploadRing ring(runtime, 1024);
    std::array<std::shared_ptr<Buffer>, 6> results;
    for (uint32_t frame = 0; frame < results.size(); ++frame)
    {
        std::array<std::byte, 257> data;
        data.fill(std::byte(frame + 1));
        auto& arena = ring.BeginFrame();
        auto slice = arena.Write(data);
        results[frame] = runtime.CreateBuffer({data.size(), Memory::Readback, "Ring.Readback"});
        auto commands = runtime.CreateCommands();
        commands->Barrier(slice.buffer, State::HostWrite, State::CopySource);
        commands->Copy(slice.buffer, results[frame], data.size(), slice.offset);
        commands->Barrier(results[frame], State::CopyDest, State::HostRead);
        ring.EndFrame(runtime.Submit(std::move(commands)));
    }
    runtime.WaitIdle();
    for (uint32_t frame = 0; frame < results.size(); ++frame)
        Require(std::ranges::all_of(results[frame]->Map(),
                                    [&](auto b) { return b == std::byte(frame + 1); }),
                "upload ring overwrote an in-flight frame");
}
void CheckUploadPool(Runtime& runtime)
{
    UploadPool pool(runtime, 512, 2);
    std::array<std::byte, 256> data;
    data.fill(std::byte{0x73});
    UploadArena::Slice retained;
    auto delayed = runtime.CreateCommands();
    auto readback = runtime.CreateBuffer({256, Memory::Readback, "Pool.DelayedReadback"});
    {
        auto batch = pool.Begin();
        retained = batch.Write(data);
        const auto second = batch.Write(data);
        Require(second.buffer == retained.buffer && second.offset == 256 && second.size == 256,
                "upload pool must pack nonzero offsets into one page");
        delayed->Barrier(second.buffer, State::HostWrite, State::CopySource);
        delayed->Copy(second.buffer, readback, second.size, second.offset);
        delayed->Barrier(readback, State::CopyDest, State::HostRead);
        // No submission: destroying the batch cancels, but references must still pin its page.
    }
    for (uint32_t i = 0; i < 8; ++i)
    {
        auto batch = pool.Begin();
        data.fill(std::byte(i + 1));
        const auto slice = batch.Write(data);
        Require(slice.buffer != retained.buffer, "CPU snapshot/recording page was overwritten");
        auto result = runtime.CreateBuffer({256, Memory::Readback, "Pool.FrameReadback"});
        auto commands = runtime.CreateCommands();
        commands->Barrier(slice.buffer, State::HostWrite, State::CopySource);
        commands->Copy(slice.buffer, result, slice.size, slice.offset);
        commands->Barrier(result, State::CopyDest, State::HostRead);
        const auto completion = runtime.Submit(std::move(commands));
        batch.Seal(completion);
        runtime.Wait(completion);
        Require(std::memcmp(result->Map().data(), data.data(), data.size()) == 0,
                "upload pool corrupted frame data");
    }
    runtime.Wait(runtime.Submit(std::move(delayed)));
    Require(std::ranges::all_of(readback->Map(), [](auto b) { return b == std::byte{0x73}; }),
            "upload pool corrupted a later-submitted recording");
    Require(retained.buffer->Map()[retained.offset] == std::byte{0x73},
            "upload pool corrupted a retained CPU snapshot after GPU retirement");
    {
        auto batch = pool.Begin();
        const auto slice = batch.Write(data);
        bool exhausted = false;
        try
        {
            batch.Write(data);
            batch.Write(data);
        }
        catch (const std::invalid_argument&)
        {
            exhausted = true;
        }
        Require(exhausted, "upload pool must enforce its page budget");
    }
    retained = {};
    {
        auto batch = pool.Begin();
        batch.Write(data);
        bool invalid = false;
        try
        {
            batch.Seal(0);
        }
        catch (const std::invalid_argument&)
        {
            invalid = true;
        }
        Require(invalid, "upload pool accepted an unsubmitted completion");
    }
    Require(pool.Stats().pages == 2 && pool.Stats().reservedBytes == 1024 &&
                pool.Stats().highWaterBytes == 512,
            "upload pool reuse or bounded statistics");
}
void CheckDescriptorBudget(Runtime& runtime)
{
    const auto originalUse = runtime.GetDescriptorStats().inUse;
    std::vector<BindingDesc> entries;
    std::vector<Binding> bindings;
    const auto sampler = runtime.CreateSampler();
    for (uint32_t i = 0; i < 32; ++i)
    {
        entries.push_back({i, BindingKind::Sampler, 0});
        Binding binding{i};
        binding.sampler = sampler;
        bindings.push_back(binding);
    }
    auto layout = runtime.CreateLayout(entries);
    std::vector<std::shared_ptr<BindingGroup>> retained;
    bool exhausted = false;
    for (uint32_t i = 0; i < 257; ++i)
        try
        {
            retained.push_back(runtime.CreateGroup(layout, bindings));
        }
        catch (const std::runtime_error&)
        {
            exhausted = true;
            break;
        }
    Require(exhausted && !retained.empty(), "descriptor allocator must enforce its budget");
    const auto creations = runtime.GetDescriptorStats().creations;
    retained.clear();
    Require(runtime.GetDescriptorStats().inUse == originalUse,
            "descriptor leases leaked after cancellation");
    retained.push_back(runtime.CreateGroup(layout, bindings));
    Require(runtime.GetDescriptorStats().creations == creations,
            "descriptor allocator must reuse released capacity after exhaustion");
}
void CheckDefaultView(Runtime& runtime)
{
    auto texture = runtime.CreateTexture({8, 8, Format::Rgba8, false, true, "View.Cache"});
    auto view = runtime.CreateView(texture);
    Require(runtime.CreateView(texture) == view, "Default View must reuse its immutable object");
    std::weak_ptr<Texture> weak = texture;
    texture.reset();
    Require(!weak.expired(), "View must retain its texture");
    view.reset();
    Require(weak.expired(), "Default View cache must not retain a texture cycle");
}
} // namespace

void CheckResourceContracts(Runtime& runtime)
{
    CheckTransfers(runtime);
    CheckLifetime(runtime);
    CheckUploadRing(runtime);
    CheckUploadPool(runtime);
    CheckDescriptorBudget(runtime);
    CheckDefaultView(runtime);
}
} // namespace renderlab::samples
