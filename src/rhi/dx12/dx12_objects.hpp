#pragma once

#include "rhi/dx12/dx12_common.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace renderlab::rhi::dx12
{
struct Allocation
{
    std::shared_ptr<AllocationLedger> ledger;
    uint64_t bytes = 0;
    ~Allocation()
    {
        if (ledger)
            ledger->Remove(bytes);
    }
    void Set(std::shared_ptr<AllocationLedger> value, uint64_t size)
    {
        value->Add(size);
        ledger = std::move(value);
        bytes = size;
    }
};
struct DxBuffer : Buffer
{
    using Buffer::Buffer;
    Allocation allocation;
    ComPtr<ID3D12Resource> native;
    void* mapped = nullptr;
    ~DxBuffer() override
    {
        if (mapped)
            native->Unmap(0, nullptr);
    }
    std::span<std::byte> Map() override
    {
        if (desc.memory == Memory::Device)
            throw std::invalid_argument("Cannot map device buffer");
        if (!mapped)
        {
            D3D12_RANGE read{0,
                             desc.memory == Memory::Readback ? static_cast<SIZE_T>(desc.size) : 0};
            Check(native->Map(0, &read, &mapped), "Map");
        }
        return {static_cast<std::byte*>(mapped), static_cast<size_t>(desc.size)};
    }
};
struct DxTexture : Texture
{
    using Texture::Texture;
    Allocation allocation;
    ComPtr<ID3D12Resource> native;
};
struct DxView : TextureView
{
    using TextureView::TextureView;
    ComPtr<ID3D12DescriptorHeap> heap;
    D3D12_CPU_DESCRIPTOR_HANDLE handle{};
};
struct DxLayout : BindingLayout
{
    using BindingLayout::BindingLayout;
    ComPtr<ID3D12RootSignature> native;
};
struct DxDescriptorPage
{
    ComPtr<ID3D12DescriptorHeap> native;
    D3D12_DESCRIPTOR_HEAP_TYPE type;
    std::vector<uint8_t> occupied;
    std::shared_ptr<DescriptorStats> stats;
    ~DxDescriptorPage()
    {
        if (stats)
        {
            --stats->pages;
            stats->capacity -= occupied.size();
        }
    }
};
struct DxDescriptorRange
{
    std::shared_ptr<DxDescriptorPage> page;
    uint32_t first, count;
    DxDescriptorRange(std::shared_ptr<DxDescriptorPage> page, uint32_t first, uint32_t count)
        : page(std::move(page)), first(first), count(count)
    {
        std::fill_n(this->page->occupied.begin() + first, count, uint8_t{1});
        auto& stats = *this->page->stats;
        stats.inUse += count;
        stats.highWater = std::max(stats.highWater, stats.inUse);
    }
    ~DxDescriptorRange()
    {
        std::fill_n(page->occupied.begin() + first, count, uint8_t{0});
        page->stats->inUse -= count;
    }
    DxDescriptorRange(const DxDescriptorRange&) = delete;
    DxDescriptorRange& operator=(const DxDescriptorRange&) = delete;
};
struct DxGroup : BindingGroup
{
    using BindingGroup::BindingGroup;
    ComPtr<ID3D12DescriptorHeap> resources, samplers;
    std::shared_ptr<DxDescriptorRange> resourceRange, samplerRange;
    std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> handles;
};
struct DxPipeline : Pipeline
{
    using Pipeline::Pipeline;
    ComPtr<ID3D12PipelineState> native;
};
} // namespace renderlab::rhi::dx12
