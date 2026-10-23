#pragma once

#include "rhi/runtime.hpp"

#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

namespace renderlab::rendergraph
{
namespace detail
{
struct Allocation
{
    std::shared_ptr<rhi::Texture> texture;
    std::shared_ptr<rhi::Buffer> buffer;
    rhi::State state = rhi::State::Undefined;
    uint64_t completion = 0, estimatedBytes = 0;
};
} // namespace detail
struct PoolStats
{
    size_t allocations = 0, reuses = 0, cached = 0;
    uint64_t estimatedBytes = 0;
};
/** @brief Bounded compatible-resource cache. Leases, command references and completions prevent
 * reuse. */
class Pool final
{
public:
    explicit Pool(rhi::Runtime& runtime, size_t maxResources = 64,
                  uint64_t maxEstimatedBytes = 64ull * 1024 * 1024);
    std::shared_ptr<detail::Allocation> Acquire(const rhi::TextureDesc& desc);
    std::shared_ptr<detail::Allocation> Acquire(const rhi::BufferDesc& desc);
    void Trim();
    PoolStats Stats() const;
    rhi::Runtime& Device() const
    {
        CheckOwner();
        return runtime_;
    }

private:
    bool Available(const std::shared_ptr<detail::Allocation>& entry) const;
    void CheckOwner() const;
    void MakeRoom(uint64_t bytes);
    rhi::Runtime& runtime_;
    size_t maxResources_;
    uint64_t maxBytes_;
    PoolStats stats_;
    std::vector<std::shared_ptr<detail::Allocation>> entries_;
    std::thread::id owner_ = std::this_thread::get_id();
};
} // namespace renderlab::rendergraph
