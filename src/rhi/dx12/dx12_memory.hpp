#pragma once

#include "rhi/dx12/dx12_common.hpp"

#include <memory>

namespace renderlab::rhi::dx12
{
struct DxMemoryAllocator;
struct DxMemoryAllocation;
std::shared_ptr<DxMemoryAllocator> CreateMemoryAllocator(ID3D12Device* device,
                                                         IDXGIFactory6* factory,
                                                         std::shared_ptr<AllocationLedger> ledger);
std::shared_ptr<DxMemoryAllocation>
AllocateResource(DxMemoryAllocator& allocator, const D3D12_RESOURCE_DESC& resource,
                 D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES initial,
                 const D3D12_CLEAR_VALUE* clear, ComPtr<ID3D12Resource>& native);
void TrimMemoryAllocator(DxMemoryAllocator& allocator);
} // namespace renderlab::rhi::dx12
