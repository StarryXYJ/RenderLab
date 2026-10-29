#include "rhi/commands.hpp"
#include "rhi/recording_workers.hpp"

#include <array>
#include <atomic>
#include <barrier>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void Fails(F function)
{
    try
    {
        function();
    }
    catch (const std::invalid_argument&)
    {
        return;
    }
    throw std::runtime_error("Expected recording contract rejection");
}
} // namespace
int main()
{
    try
    {
        using namespace renderlab::rhi;
        RecordingWorkers workers;
        std::array<std::thread::id, 16> identities;
        std::barrier rendezvous(16);
        workers.Run(16,
                    [&](size_t index)
                    {
                        identities[index] = std::this_thread::get_id();
                        rendezvous.arrive_and_wait();
                    });
        Require(identities[0] == std::this_thread::get_id(), "Job zero left the owner thread");
        for (size_t i = 0; i < identities.size(); ++i)
            for (size_t j = 0; j < i; ++j)
                Require(identities[i] != identities[j], "Native recording jobs did not overlap");
        std::atomic_uint32_t finished = 0;
        Fails(
            [&]
            {
                workers.Run(16,
                            [&](size_t index)
                            {
                                ++finished;
                                if (index == 3 || index == 0)
                                    throw std::invalid_argument("Injected recording failure");
                            });
            });
        Require(finished == 16, "Exception escaped before every recorder joined");
        for (const size_t count : {1u, 2u, 4u, 16u, 1u, 16u})
            workers.Run(count,
                        [&](size_t index)
                        {
                            Require(std::this_thread::get_id() == identities[index],
                                    "Recording threads were respawned or failed to recover");
                        });
        Fails([&] { workers.Run(0, [](size_t) {}); });
        Fails([&] { workers.Run(17, [](size_t) {}); });
        CommandList commands(nullptr);
        Fails([&] { commands.SetRecordingThreads(0); });
        Fails([&] { commands.SetRecordingThreads(17); });
        commands.SetRecordingThreads(4);
        for (size_t i = 0; i < 9; ++i)
        {
            commands.BeginMarker("Pass");
            Fails([&] { commands.RecordingBoundary(); });
            commands.EndMarker();
            commands.RecordingBoundary();
            commands.RecordingBoundary();
        }
        auto ranges = commands.RecordingRanges();
        Require(ranges.size() == 4, "Packet partition failed to use four independent ranges");
        size_t previous = 0;
        for (auto [first, last] : ranges)
        {
            Require(first == previous && last > first && last % 2 == 0,
                    "Partition duplicated/skipped packets or split an open marker");
            previous = last;
        }
        Require(previous == commands.GetCommands().size(), "Partition omitted the tail");
        commands.SetRecordingThreads(1);
        Require(commands.RecordingRanges().size() == 1, "Serial recording split commands");
        commands.Cancel();
        Fails([&] { commands.RecordingBoundary(); });
        Fails([&] { commands.SetRecordingThreads(1); });
        std::cout
            << "Recording contracts passed: overlap, persistence, exceptions and boundaries\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
