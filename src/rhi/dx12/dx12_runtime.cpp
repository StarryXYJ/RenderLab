#include "rhi/dx12/dx12_runtime.hpp"
#include "rhi/dx12/dx12_memory.hpp"

#include <stdexcept>

namespace renderlab::rhi::dx12
{
void DxRuntime::TrimMemoryNative()
{
#ifdef RENDERLAB_DX12_MEMORY_ALLOCATOR
    TrimMemoryAllocator(*memoryAllocator_);
#endif
}
void DxRuntime::EnableAsyncCompute()
{
    if (computeQueue_)
        return;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> compute, graphics;
    D3D12_COMMAND_QUEUE_DESC desc{};
    desc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    Check(device_->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)), "Create Compute Queue");
    Check(queue->SetName(L"RenderLab.ComputeQueue"), "Name Compute Queue");
    Check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&compute)), "Compute Fence");
    Check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&graphics)),
          "Graphics segment Fence");
    Check(queue->GetTimestampFrequency(&computeFrequency_), "Compute timestamp frequency");
    computeQueue_ = std::move(queue);
    computeFence_ = std::move(compute);
    graphicsFence_ = std::move(graphics);
}
QueueClock DxRuntime::TimestampClock(QueueType type) const
{
    auto* queue = type == QueueType::Compute ? computeQueue_.Get() : queue_;
    if (!queue)
        throw std::invalid_argument("Compute clock requires enabled Compute Queue");
    UINT64 gpu = 0, cpu = 0;
    Check(queue->GetClockCalibration(&gpu, &cpu), "Calibrate queue timestamp");
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency))
        throw std::runtime_error("QueryPerformanceFrequency failed");
    return {gpu, double(cpu) * 1e9 / double(frequency.QuadPart),
            1e9 / double(type == QueueType::Compute ? computeFrequency_ : frequency_), type};
}
void DxRuntime::DrainNative()
{
    for (auto* queue : {queue_, computeQueue_.Get()})
    {
        if (!queue)
            continue;
        ComPtr<ID3D12Fence> fence;
        Check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "Drain Fence");
        Check(queue->Signal(fence.Get(), 1), "Drain Queue Signal");
        Check(fence->SetEventOnCompletion(1, event_), "Drain Queue Event");
        if (WaitForSingleObject(event_, INFINITE) != WAIT_OBJECT_0)
            throw std::runtime_error("Drain Queue wait failed");
    }
}
DxRuntime::DxRuntime(ID3D12Device* device, ID3D12CommandQueue* queue, IDXGIFactory6* factory,
                     bool accelerationStructureEnabled, bool rayQueryEnabled,
                     bool rayTracingPipelineEnabled)
    : device_(device), queue_(queue), factory_(factory),
      accelerationStructureEnabled_(accelerationStructureEnabled),
      rayQueryEnabled_(rayQueryEnabled), rayTracingPipelineEnabled_(rayTracingPipelineEnabled)
{
    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{D3D_SHADER_MODEL_6_8};
    indirectSupport_.shaderInstanceIndex =
        SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel,
                                               sizeof(shaderModel))) &&
        shaderModel.HighestShaderModel >= D3D_SHADER_MODEL_6_8;
    device_->QueryInterface(IID_PPV_ARGS(&rayDevice_));
    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    if (SUCCEEDED(
            device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))) &&
        options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_2)
        sampledTextureArrayLimit_ = 64;
    Check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence");
    D3D12_INDIRECT_ARGUMENT_DESC argument{};
    argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    D3D12_COMMAND_SIGNATURE_DESC signature{};
    signature.ByteStride = sizeof(DrawArguments);
    signature.NumArgumentDescs = 1;
    signature.pArgumentDescs = &argument;
    Check(device_->CreateCommandSignature(&signature, nullptr, IID_PPV_ARGS(&indirect_)),
          "CreateCommandSignature");
    argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    signature.ByteStride = sizeof(DrawIndexedArguments);
    Check(device_->CreateCommandSignature(&signature, nullptr, IID_PPV_ARGS(&indexedIndirect_)),
          "Create indexed command signature");
    Check(queue_->GetTimestampFrequency(&frequency_), "GetTimestampFrequency");
#ifdef RENDERLAB_DX12_MEMORY_ALLOCATOR
    memoryAllocator_ = CreateMemoryAllocator(device_, factory_, ledger_);
#endif
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event_)
        throw std::runtime_error("CreateEventW failed");
}

DxRuntime::~DxRuntime()
{
    try
    {
        WaitIdle();
    }
    catch (...)
    { /* Device loss: no recovery in M1. */
    }
    if (event_)
        CloseHandle(event_);
}

uint64_t DxRuntime::Completed() const
{
    const auto value = fence_->GetCompletedValue();
    if (value == UINT64_MAX)
        throw std::runtime_error("DX12 device removed while polling fence");
    return value;
}

double DxRuntime::TimestampPeriodNs() const
{
    return 1e9 / static_cast<double>(frequency_);
}

void DxRuntime::WaitNative(uint64_t point)
{
    Check(fence_->SetEventOnCompletion(point, event_), "SetEventOnCompletion");
    if (WaitForSingleObject(event_, 30000) != WAIT_OBJECT_0)
        throw std::runtime_error("DX12 fence wait timed out (possible device loss)");
}
} // namespace renderlab::rhi::dx12

namespace renderlab::rhi
{
std::unique_ptr<Runtime> CreateDx12Runtime(ID3D12Device* device, ID3D12CommandQueue* queue,
                                           IDXGIFactory6* factory, bool accelerationStructure,
                                           bool rayQuery, bool rayTracingPipeline)
{
    return std::make_unique<dx12::DxRuntime>(device, queue, factory, accelerationStructure,
                                             rayQuery, rayTracingPipeline);
}
} // namespace renderlab::rhi
