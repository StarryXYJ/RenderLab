#pragma once

#include "rhi/resources.hpp"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <memory>

namespace renderlab::rhi::dx12
{
using Microsoft::WRL::ComPtr;
void Check(HRESULT result, const char* operation);
DXGI_FORMAT NativeFormat(Format format);
D3D12_RESOURCE_STATES NativeState(State state);

template <class T, class U> T& As(const std::shared_ptr<U>& object)
{
    return static_cast<T&>(*object);
}
} // namespace renderlab::rhi::dx12
