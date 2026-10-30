#include "rhi/resources.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace renderlab::rhi;
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void RejectUnchanged(AllocationLedger& ledger, F&& action)
{
    const auto before = ledger.stats;
    try
    {
        action();
    }
    catch (const std::runtime_error&)
    {
        Require(ledger.stats == before, "Rejected admission changed counters or peaks");
        return;
    }
    throw std::runtime_error("Invalid memory admission succeeded");
}
void Contracts()
{
    AllocationLedger ledger;
    ledger.stats.budget = 1024;
    RejectUnchanged(ledger, [&] { ledger.Add(0); });
    RejectUnchanged(ledger, [&] { ledger.AddAllocation(1); });
    ledger.Add(128);
    Require(ledger.stats.used == 128 && ledger.stats.reserved == 128 &&
                ledger.stats.allocations == 1 && ledger.stats.blocks == 1,
            "Dedicated range/backing was not charged once");
    RejectUnchanged(ledger, [&] { ledger.Add(897); });
    ledger.Remove(128);
    Require(!ledger.stats.used && !ledger.stats.reserved && !ledger.stats.allocations &&
                !ledger.stats.blocks && ledger.stats.highWater == 128 &&
                ledger.stats.reservedHighWater == 128,
            "Dedicated rollback lost live counts or peaks");

    ledger.ReserveBlock(1024);
    ledger.AddAllocation(128);
    ledger.AddAllocation(256);
    Require(ledger.stats.used == 384 && ledger.stats.reserved == 1024 &&
                ledger.stats.allocations == 2 && ledger.stats.blocks == 1,
            "Suballocations charged backing repeatedly");
    RejectUnchanged(ledger, [&] { ledger.AddAllocation(641); });
    RejectUnchanged(ledger, [&] { ledger.ReserveBlock(1); });
    RejectUnchanged(ledger, [&] { ledger.Add(1); });
    ledger.RemoveAllocation(256);
    ledger.RemoveAllocation(128);
    Require(!ledger.stats.used && !ledger.stats.allocations && ledger.stats.reserved == 1024 &&
                ledger.stats.blocks == 1,
            "Idle backing was hidden as zero");
    RejectUnchanged(ledger, [&] { ledger.Add(1); });
    ledger.AddAllocation(1024);
    Require(ledger.stats.highWater == 1024 && ledger.stats.allocationHighWater == 2 &&
                ledger.stats.blockHighWater == 1,
            "Reuse changed backing count or lost peaks");
    ledger.RemoveAllocation(1024);
    ledger.ReleaseBlock(1024);
    ledger.Add(256);
    ledger.ReserveBlock(512);
    ledger.AddAllocation(128);
    Require(ledger.stats.used == 384 && ledger.stats.reserved == 768 &&
                ledger.stats.allocations == 2 && ledger.stats.blocks == 2,
            "Dedicated and suballocated backing did not share the same budget");
    ledger.RemoveAllocation(128);
    ledger.ReleaseBlock(512);
    ledger.Remove(256);
    Require(!ledger.stats.used && !ledger.stats.reserved && !ledger.stats.allocations &&
                !ledger.stats.blocks && ledger.stats.blockHighWater == 2,
            "Mixed backing did not retire completely");

    ledger.stats.budget = UINT64_MAX;
    ledger.Add(UINT64_MAX);
    RejectUnchanged(ledger, [&] { ledger.Add(1); });
    RejectUnchanged(ledger, [&] { ledger.ReserveBlock(1); });
    RejectUnchanged(ledger, [&] { ledger.AddAllocation(1); });
    Require(ledger.stats.highWater == UINT64_MAX && ledger.stats.reservedHighWater == UINT64_MAX,
            "Large admission overflowed");
    ledger.Remove(UINT64_MAX);
    ledger.ReserveBlock(UINT64_MAX);
    ledger.AddAllocation(UINT64_MAX);
    ledger.RemoveAllocation(UINT64_MAX);
    ledger.ReleaseBlock(UINT64_MAX);
    Require(!ledger.stats.used && !ledger.stats.reserved && !ledger.stats.allocations &&
                !ledger.stats.blocks,
            "Large retirement overflowed");
}
} // namespace
int main()
{
    try
    {
        Contracts();
        std::cout << "Memory contracts passed: backing budget, suballocation, rejection, peaks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
