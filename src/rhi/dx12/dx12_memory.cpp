#include "rhi/dx12/dx12_memory.hpp"

#include <D3D12MemAlloc.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

namespace renderlab::rhi::dx12
{
namespace
{
constexpr uint64_t BlockBytes = 16ull * 1024 * 1024;
struct Pool
{
    ComPtr<D3D12MA::Allocator> allocator;
    std::shared_ptr<AllocationLedger> ledger;
    ComPtr<D3D12MA::Pool> native;
    D3D12_HEAP_TYPE heap;
    D3D12_HEAP_FLAGS flags;
    uint64_t blocks = 0;
    Pool(ComPtr<D3D12MA::Allocator> allocator, std::shared_ptr<AllocationLedger> ledger,
         D3D12_HEAP_TYPE heap, D3D12_HEAP_FLAGS flags)
        : allocator(std::move(allocator)), ledger(std::move(ledger)), heap(heap), flags(flags)
    {
        D3D12MA::POOL_DESC desc{};
        desc.HeapProperties.Type = heap;
        desc.HeapFlags = flags;
        desc.BlockSize = BlockBytes;
        Check(this->allocator->CreatePool(&desc, &native), "D3D12MA CreatePool");
    }
    ~Pool()
    {
        native.Reset();
        while (blocks)
        {
            ledger->ReleaseBlock(BlockBytes);
            --blocks;
        }
    }
    D3D12MA::Statistics Stats() const
    {
        D3D12MA::Statistics stats{};
        native->GetStatistics(&stats);
        return stats;
    }
    void Reserve()
    {
        ledger->ReserveBlock(BlockBytes);
        ++blocks;
    }
    void Sync()
    {
        const auto stats = Stats();
        // Fixed-size pools never grow without prior admission by this adapter.
        if (stats.BlockCount > blocks ||
            stats.BlockBytes != uint64_t(stats.BlockCount) * BlockBytes)
            std::terminate();
        while (blocks > stats.BlockCount)
        {
            ledger->ReleaseBlock(BlockBytes);
            --blocks;
        }
    }
};
} // namespace

struct DxMemoryAllocation
{
    ComPtr<D3D12MA::Allocator> allocator;
    std::shared_ptr<AllocationLedger> ledger;
    std::shared_ptr<Pool> pool;
    ComPtr<D3D12MA::Allocation> native;
    uint64_t bytes = 0;
    ~DxMemoryAllocation()
    {
        native.Reset();
        if (bytes)
        {
            if (pool)
                ledger->RemoveAllocation(bytes);
            else
                ledger->Remove(bytes);
        }
        if (pool)
            pool->Sync();
    }
};
struct DxMemoryAllocator
{
    ID3D12Device* device;
    ComPtr<D3D12MA::Allocator> native;
    std::shared_ptr<AllocationLedger> ledger;
    std::vector<std::shared_ptr<Pool>> pools;
    DxMemoryAllocator(ID3D12Device* device, IDXGIFactory6* factory,
                      std::shared_ptr<AllocationLedger> ledger)
        : device(device), ledger(std::move(ledger))
    {
        ComPtr<IDXGIAdapter> adapter;
        Check(factory->EnumAdapterByLuid(device->GetAdapterLuid(), IID_PPV_ARGS(&adapter)),
              "D3D12MA adapter");
        D3D12MA::ALLOCATOR_DESC desc{};
        desc.pDevice = device;
        desc.pAdapter = adapter.Get();
        desc.Flags = static_cast<D3D12MA::ALLOCATOR_FLAGS>(
            D3D12MA::ALLOCATOR_FLAG_SINGLETHREADED |
            D3D12MA::ALLOCATOR_FLAG_DONT_PREFER_SMALL_BUFFERS_COMMITTED |
            D3D12MA::ALLOCATOR_FLAG_DONT_USE_TIGHT_ALIGNMENT);
        Check(D3D12MA::CreateAllocator(&desc, &native), "D3D12MA CreateAllocator");
    }
    void Trim(const Pool* keep = nullptr)
    {
        std::erase_if(pools, [=](const auto& pool)
                      { return pool.get() != keep && pool->Stats().AllocationCount == 0; });
    }
    std::shared_ptr<Pool> FindPool(D3D12_HEAP_TYPE heap, D3D12_HEAP_FLAGS flags)
    {
        for (const auto& pool : pools)
            if (pool->heap == heap && pool->flags == flags)
                return pool;
        auto pool = std::make_shared<Pool>(native, ledger, heap, flags);
        pools.push_back(pool);
        return pool;
    }
};
std::shared_ptr<DxMemoryAllocator> CreateMemoryAllocator(ID3D12Device* device,
                                                         IDXGIFactory6* factory,
                                                         std::shared_ptr<AllocationLedger> ledger)
{
    return std::make_shared<DxMemoryAllocator>(device, factory, std::move(ledger));
}
void TrimMemoryAllocator(DxMemoryAllocator& allocator)
{
    allocator.Trim();
}
std::shared_ptr<DxMemoryAllocation>
AllocateResource(DxMemoryAllocator& allocator, const D3D12_RESOURCE_DESC& resource,
                 D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES initial,
                 const D3D12_CLEAR_VALUE* clear, ComPtr<ID3D12Resource>& native)
{
    const auto required = allocator.device->GetResourceAllocationInfo(0, 1, &resource).SizeInBytes;
    if (!required || required == UINT64_MAX)
        throw std::runtime_error("Invalid D3D12MA resource requirements");
    const auto flags =
        resource.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ? D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS
        : resource.Flags &
                (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
            ? D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES
            : D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
    auto result = std::make_shared<DxMemoryAllocation>();
    result->allocator = allocator.native;
    result->ledger = allocator.ledger;
    D3D12MA::ALLOCATION_DESC desc{};
    desc.HeapType = heap;
    if (required <= BlockBytes)
    {
        result->pool = allocator.FindPool(heap, flags);
        desc.CustomPool = result->pool->native.Get();
        desc.Flags = D3D12MA::ALLOCATION_FLAG_NEVER_ALLOCATE;
        auto hr = allocator.native->CreateResource(&desc, &resource, initial, clear,
                                                   &result->native, IID_PPV_ARGS(&native));
        if (hr == E_OUTOFMEMORY)
        {
            allocator.Trim(result->pool.get());
            const auto& stats = allocator.ledger->stats;
            if (BlockBytes <= stats.budget - stats.reserved)
            {
                result->pool->Reserve();
                desc.Flags = D3D12MA::ALLOCATION_FLAG_NONE;
                hr = allocator.native->CreateResource(&desc, &resource, initial, clear,
                                                      &result->native, IID_PPV_ARGS(&native));
            }
            else
            {
                // A small application limit must not require a full 16 MiB cache block.
                result->pool.reset();
                allocator.Trim();
                desc.CustomPool = nullptr;
                hr = E_OUTOFMEMORY;
            }
        }
        if (desc.CustomPool)
        {
            Check(hr, "D3D12MA placed resource");
            result->pool->Sync();
            const auto bytes = result->native->GetSize();
            allocator.ledger->AddAllocation(bytes);
            result->bytes = bytes;
            return result;
        }
    }
    allocator.Trim();
    allocator.ledger->Add(required);
    result->bytes = required;
    desc.Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED;
    Check(allocator.native->CreateResource(&desc, &resource, initial, clear, &result->native,
                                           IID_PPV_ARGS(&native)),
          "D3D12MA committed resource");
    const auto bytes = result->native->GetSize();
    if (!bytes || bytes > required)
        throw std::runtime_error("D3D12MA committed size exceeds admitted requirements");
    if (bytes != required)
    {
        allocator.ledger->Remove(required);
        allocator.ledger->Add(bytes);
        result->bytes = bytes;
    }
    return result;
}
} // namespace renderlab::rhi::dx12
