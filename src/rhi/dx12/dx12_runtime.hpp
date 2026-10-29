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
    bool SupportsAsyncCompute() const override
    {
        return true;
    }
    void EnableAsyncCompute() override;
    QueueClock TimestampClock(QueueType queue) const override;
    uint32_t SampledTextureArrayLimit() const override
    {
        return sampledTextureArrayLimit_;
    }
    IndirectSupport GetIndirectSupport() const override
    {
        return indirectSupport_;
    }
    DxRuntime(ID3D12Device* device, ID3D12CommandQueue* queue, IDXGIFactory6* factory,
              bool accelerationStructureEnabled, bool rayQueryEnabled,
              bool rayTracingPipelineEnabled);
    ~DxRuntime() override;
    uint64_t Completed() const override;
    double TimestampPeriodNs() const override;
    bool SupportsAccelerationStructures() const override
    {
        return accelerationStructureEnabled_;
    }
    bool SupportsRayQuery() const override
    {
        return rayQueryEnabled_;
    }
    bool SupportsRayTracingPipeline() const override
    {
        return rayTracingPipelineEnabled_;
    }
    std::shared_ptr<Buffer> CreateBuffer(const BufferDesc& desc) override;
    std::shared_ptr<Texture> CreateTexture(const TextureDesc& desc) override;
    std::shared_ptr<TextureView> CreateView(std::shared_ptr<Texture> texture,
                                            TextureViewUsage usage) override;
    std::shared_ptr<Sampler> CreateSampler() override;
    std::shared_ptr<BindingLayout> CreateLayout(std::vector<BindingDesc> entries) override;
    std::shared_ptr<BindingGroup> CreateGroup(std::shared_ptr<BindingLayout> layout,
                                              std::vector<Binding> bindings) override;
    std::unique_ptr<Swapchain> CreateSwapchain(platform::NativeSurface surface, uint32_t width,
                                               uint32_t height, bool vsync = true) override;

protected:
    std::shared_ptr<ShaderTable> CreateShaderTableNative(std::shared_ptr<Pipeline>) override;
    std::shared_ptr<Pipeline> CreatePipelineNative(PipelineDesc desc) override;
    AccelerationStructureSizes
    QueryAccelerationStructureSizesNative(const AccelerationStructureDesc& desc) override;
    std::shared_ptr<AccelerationStructure>
    CreateAccelerationStructureNative(AccelerationStructureDesc desc,
                                      AccelerationStructureSizes sizes) override;
    void WaitNative(uint64_t point) override;
    void DrainNative() override;
    struct Work : Submission
    {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        ComPtr<ID3D12QueryHeap> queries;
        QueueType queue = QueueType::Graphics;
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
    ComPtr<ID3D12CommandQueue> computeQueue_;
    ComPtr<ID3D12Fence> computeFence_, graphicsFence_;
    uint64_t nativeSequence_ = 0, computeFrequency_ = 0;
    ComPtr<ID3D12Device5> rayDevice_;
    bool accelerationStructureEnabled_, rayQueryEnabled_, rayTracingPipelineEnabled_;
    ComPtr<ID3D12CommandSignature> indirect_, indexedIndirect_;
    IndirectSupport indirectSupport_{UINT32_MAX, true, true, false};
    uint32_t sampledTextureArrayLimit_ = 0;
    HANDLE event_ = nullptr;
    uint64_t frequency_ = 0;
    std::vector<std::shared_ptr<Work>> contexts_;
};
} // namespace renderlab::rhi::dx12

namespace renderlab::rhi
{
std::unique_ptr<Runtime> CreateDx12Runtime(ID3D12Device*, ID3D12CommandQueue*, IDXGIFactory6*, bool,
                                           bool, bool);
} // namespace renderlab::rhi
