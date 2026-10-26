#include "rhi/dx12/dx12_common.hpp"

#include <format>
#include <stdexcept>

namespace renderlab::rhi::dx12
{
void Check(HRESULT result, const char* operation)
{
    if (FAILED(result))
        throw std::runtime_error(
            std::format("{} failed (0x{:08X})", operation, static_cast<uint32_t>(result)));
}
DXGI_FORMAT NativeFormat(Format format)
{
    switch (format)
    {
    case Format::Rgba8:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case Format::Bgra8:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case Format::Rgba8Srgb:
        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case Format::Bgra8Srgb:
        return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case Format::D32:
        return DXGI_FORMAT_D32_FLOAT;
    case Format::Rgba32Float:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    }
    throw std::invalid_argument("Unknown format");
}
D3D12_RESOURCE_STATES NativeState(State state)
{
    switch (state)
    {
    case State::Undefined:
    case State::Present:
        return D3D12_RESOURCE_STATE_COMMON;
    case State::HostWrite:
        return D3D12_RESOURCE_STATE_GENERIC_READ;
    case State::HostRead:
    case State::CopyDest:
        return D3D12_RESOURCE_STATE_COPY_DEST;
    case State::CopySource:
        return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case State::Vertex:
    case State::UniformRead:
        return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    case State::Index:
        return D3D12_RESOURCE_STATE_INDEX_BUFFER;
    case State::Indirect:
        return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    case State::Storage:
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case State::Sampled:
    case State::ShaderRead:
        return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case State::ColorAttachment:
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case State::DepthWrite:
        return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }
    throw std::invalid_argument("Unknown state");
}
} // namespace renderlab::rhi::dx12
