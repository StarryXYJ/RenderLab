#pragma once

#include "rhi/dx12/dx12_common.hpp"
#include "rhi/runtime.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace renderlab::rhi::dx12
{
struct DxDescriptorPage;
struct DxDescriptorRange;
/** @brief Backend device services; owns queue completion and native command contexts. */
class DxRuntime final : public Runtime
{
public:
    DxRuntime(ID3D12Device* device, ID3D12CommandQueue* queue, IDXGIFactory6* factory);
    ~DxRuntime() override;
    uint64_t Completed() const override;
    double TimestampPeriodNs() const override;
    std::shared_ptr<Buffer> CreateBuffer(const BufferDesc& desc) override;
    std::shared_ptr<Texture> CreateTexture(const TextureDesc& desc) override;
    std::shared_ptr<TextureView> CreateView(std::shared_ptr<Texture> texture) override;
    std::shared_ptr<Sampler> CreateSampler() override;
    std::shared_ptr<BindingLayout> CreateLayout(std::vector<BindingDesc> entries) override;
    std::shared_ptr<BindingGroup> CreateGroup(std::shared_ptr<BindingLayout> layout,
                                              std::vector<Binding> bindings) override;
    std::unique_ptr<Swapchain> CreateSwapchain(platform::NativeSurface surface, uint32_t width,
                                               uint32_t height) override;

protected:
    std::shared_ptr<Pipeline> CreatePipelineNative(PipelineDesc desc) override;
    void WaitNative(uint64_t point) override;
    struct Work : Submission
    {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        ComPtr<ID3D12QueryHeap> queries;
    };
    std::shared_ptr<Submission> Execute(const CommandList& commands, uint64_t point,
                                        Swapchain*) override;

private:
    std::shared_ptr<DxDescriptorRange> AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type,
                                                           uint32_t count);
    std::vector<std::shared_ptr<DxDescriptorPage>> descriptorPages_;
    ID3D12Device* device_;
    ID3D12CommandQueue* queue_;
    IDXGIFactory6* factory_;
    ComPtr<ID3D12Fence> fence_;
    ComPtr<ID3D12CommandSignature> indirect_;
    HANDLE event_ = nullptr;
    uint64_t frequency_ = 0;
    std::vector<std::shared_ptr<Work>> contexts_;
};
} // namespace renderlab::rhi::dx12

namespace renderlab::rhi
{
std::unique_ptr<Runtime> CreateDx12Runtime(ID3D12Device*, ID3D12CommandQueue*, IDXGIFactory6*);
} // namespace renderlab::rhi
