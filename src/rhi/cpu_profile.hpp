#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace renderlab::rhi
{
enum class CpuOperation : size_t
{
    BufferCreate,
    GroupCreate,
    ViewCreate,
    NativeSubmit,
    Retire,
    FenceWait,
    DescriptorPageCreate,
    DescriptorHeapBind,
    Count
};
inline constexpr std::array<std::string_view, size_t(CpuOperation::Count)> CpuOperationNames = {
    "rhiBufferCreateMs",
    "rhiGroupCreateMs",
    "rhiViewCreateMs",
    "rhiNativeSubmitMs",
    "rhiRetireMs",
    "rhiFenceWaitMs",
    "rhiDescriptorPageCreateMs",
    "rhiDescriptorHeapBindMs"};
struct CpuMeasurement
{
    uint64_t calls = 0, nanoseconds = 0;
};
using CpuProfileSnapshot = std::array<CpuMeasurement, size_t(CpuOperation::Count)>;
inline CpuProfileSnapshot Difference(const CpuProfileSnapshot& after,
                                     const CpuProfileSnapshot& before)
{
    CpuProfileSnapshot result;
    for (size_t i = 0; i < result.size(); ++i)
        result[i] = {after[i].calls - before[i].calls,
                     after[i].nanoseconds - before[i].nanoseconds};
    return result;
}
/** @brief Optional owner-thread CPU scope; disabled scopes never read the clock.
 * Durations are inclusive operation wall times, not GPU execution times.
 */
class CpuTimer final
{
public:
    explicit CpuTimer(CpuMeasurement* destination) : destination_(destination)
    {
        if (destination_)
            started_ = std::chrono::steady_clock::now();
    }
    ~CpuTimer()
    {
        if (destination_)
        {
            destination_->nanoseconds +=
                uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - started_)
                             .count());
            ++destination_->calls;
        }
    }
    CpuTimer(const CpuTimer&) = delete;
    CpuTimer& operator=(const CpuTimer&) = delete;

private:
    CpuMeasurement* destination_;
    std::chrono::steady_clock::time_point started_;
};
} // namespace renderlab::rhi
