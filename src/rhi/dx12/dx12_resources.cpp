#include "platform/text.hpp"
#include "rhi/dx12/dx12_objects.hpp"
#include "rhi/dx12/dx12_runtime.hpp"

#include <stdexcept>

namespace renderlab::rhi::dx12
{
std::shared_ptr<Buffer> DxRuntime::CreateBuffer(const BufferDesc& desc)
{
    auto cpuTimer = ProfileCpu(CpuOperation::BufferCreate);
    Validate(desc);
    auto buffer = std::make_shared<DxBuffer>(this, desc);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = desc.memory == Memory::Upload     ? D3D12_HEAP_TYPE_UPLOAD
                : desc.memory == Memory::Readback ? D3D12_HEAP_TYPE_READBACK
                                                  : D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC resource{};
    resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource.Width = desc.size;
    resource.Height = 1;
    resource.DepthOrArraySize = 1;
    resource.MipLevels = 1;
    resource.SampleDesc.Count = 1;
    resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (desc.storage)
        resource.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    buffer->allocation.Set(ledger_,
                           device_->GetResourceAllocationInfo(0, 1, &resource).SizeInBytes);
    const auto initial = desc.memory == Memory::Upload     ? D3D12_RESOURCE_STATE_GENERIC_READ
                         : desc.memory == Memory::Readback ? D3D12_RESOURCE_STATE_COPY_DEST
                                                           : D3D12_RESOURCE_STATE_COMMON;
    Check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &resource, initial, nullptr,
                                           IID_PPV_ARGS(&buffer->native)),
          "CreateBuffer");
    Check(buffer->native->SetName(platform::Utf8ToWide(desc.name).c_str()), "Buffer SetName");
    return buffer;
}

std::shared_ptr<Texture> DxRuntime::CreateTexture(const TextureDesc& desc)
{
    Validate(desc);
    auto texture = std::make_shared<DxTexture>(this, desc);
    D3D12_RESOURCE_DESC resource{};
    resource.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource.Width = desc.width;
    resource.Height = desc.height;
    resource.DepthOrArraySize = 1;
    resource.MipLevels = 1;
    resource.Format = NativeFormat(desc.format);
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
    support.Format = resource.Format;
    Check(device_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)),
          "Texture format support");
    const auto required = IsDepthFormat(desc.format)
                              ? D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL
                              : D3D12_FORMAT_SUPPORT1_SHADER_LOAD |
                                    (desc.attachment ? D3D12_FORMAT_SUPPORT1_RENDER_TARGET
                                                     : D3D12_FORMAT_SUPPORT1_NONE);
    if ((support.Support1 & required) != required)
        throw std::runtime_error("Texture format lacks requested usage");
    resource.SampleDesc.Count = 1;
    if (desc.storage)
        resource.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (desc.attachment)
        resource.Flags |= IsDepthFormat(desc.format) ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                                                     : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    texture->allocation.Set(ledger_,
                            device_->GetResourceAllocationInfo(0, 1, &resource).SizeInBytes);
    D3D12_CLEAR_VALUE clear{};
    clear.Format = resource.Format;
    if (!IsDepthFormat(desc.format))
    {
        clear.Color[0] = clear.Color[1] = clear.Color[2] = .02f;
        clear.Color[3] = 1;
    }
    Check(device_->CreateCommittedResource(
              &heap, D3D12_HEAP_FLAG_NONE, &resource, D3D12_RESOURCE_STATE_COMMON,
              desc.attachment ? &clear : nullptr, IID_PPV_ARGS(&texture->native)),
          "CreateTexture");
    Check(texture->native->SetName(platform::Utf8ToWide(desc.name).c_str()), "Texture SetName");
    return texture;
}

std::shared_ptr<TextureView> DxRuntime::CreateView(std::shared_ptr<Texture> texture)
{
    auto cpuTimer = ProfileCpu(CpuOperation::ViewCreate);
    CheckOwner(texture);
    auto view = std::make_shared<DxView>(texture);
    if (texture->desc.attachment)
    {
        const bool depth = IsDepthFormat(texture->desc.format);
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.NumDescriptors = 1;
        heap.Type = depth ? D3D12_DESCRIPTOR_HEAP_TYPE_DSV : D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        Check(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&view->heap)), "CreateView heap");
        view->handle = view->heap->GetCPUDescriptorHandleForHeapStart();
        if (depth)
            device_->CreateDepthStencilView(As<DxTexture>(texture).native.Get(), nullptr,
                                            view->handle);
        else
        {
            D3D12_RENDER_TARGET_VIEW_DESC desc{};
            desc.Format = NativeFormat(texture->desc.format);
            desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            device_->CreateRenderTargetView(As<DxTexture>(texture).native.Get(), &desc,
                                            view->handle);
        }
    }
    return view;
}

std::shared_ptr<Sampler> DxRuntime::CreateSampler()
{
    return std::make_shared<Sampler>(this);
}
} // namespace renderlab::rhi::dx12
