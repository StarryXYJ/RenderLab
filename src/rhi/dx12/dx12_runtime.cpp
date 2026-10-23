#include "rhi/dx12/dx12_runtime.hpp"

#include <stdexcept>

namespace renderlab::rhi::dx12
{
DxRuntime::DxRuntime(ID3D12Device* device, ID3D12CommandQueue* queue, IDXGIFactory6* factory)
    : device_(device), queue_(queue), factory_(factory)
{
    Check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence");
    D3D12_INDIRECT_ARGUMENT_DESC argument{};
    argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    D3D12_COMMAND_SIGNATURE_DESC signature{};
    signature.ByteStride = sizeof(DrawArguments);
    signature.NumArgumentDescs = 1;
    signature.pArgumentDescs = &argument;
    Check(device_->CreateCommandSignature(&signature, nullptr, IID_PPV_ARGS(&indirect_)),
          "CreateCommandSignature");
    Check(queue_->GetTimestampFrequency(&frequency_), "GetTimestampFrequency");
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
                                           IDXGIFactory6* factory)
{
    return std::make_unique<dx12::DxRuntime>(device, queue, factory);
}
} // namespace renderlab::rhi
