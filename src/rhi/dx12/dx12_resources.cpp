#include "platform/text.hpp"
#include "rhi/dx12/dx12_objects.hpp"
#include "rhi/dx12/dx12_runtime.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace renderlab::rhi::dx12
{
namespace
{
D3D12_CLEAR_VALUE TextureClearValue(const TextureDesc& desc)
{
    D3D12_CLEAR_VALUE clear{};
    clear.Format = NativeFormat(desc.format);
    if (!IsDepthFormat(desc.format))
    {
        clear.Color[0] = clear.Color[1] = clear.Color[2] = .02f;
        clear.Color[3] = 1;
    }
    return clear;
}
} // namespace
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
    if (desc.storage || desc.accelerationStructureStorage)
        resource.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    buffer->allocation.Set(ledger_,
                           device_->GetResourceAllocationInfo(0, 1, &resource).SizeInBytes);
    const auto initial = desc.accelerationStructureStorage
                             ? D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE
                         : desc.memory == Memory::Upload   ? D3D12_RESOURCE_STATE_GENERIC_READ
                         : desc.memory == Memory::Readback ? D3D12_RESOURCE_STATE_COPY_DEST
                                                           : D3D12_RESOURCE_STATE_COMMON;
    Check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &resource, initial, nullptr,
                                           IID_PPV_ARGS(&buffer->native)),
          "CreateBuffer");
    Check(buffer->native->SetName(platform::Utf8ToWide(desc.name).c_str()), "Buffer SetName");
    return buffer;
}

AccelerationStructureSizes
DxRuntime::QueryAccelerationStructureSizesNative(const AccelerationStructureDesc& desc)
{
    if (!accelerationStructureEnabled_ || !rayDevice_)
        throw std::runtime_error("DX12 acceleration structures are not supported by this device");
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries;
    if (!desc.topLevel)
    {
        geometries.resize(desc.geometries.size());
        for (size_t i = 0; i < desc.geometries.size(); ++i)
        {
            const auto& source = desc.geometries[i];
            auto& geometry = geometries[i];
            geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
            geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            geometry.Triangles.VertexCount = source.vertexCount;
            geometry.Triangles.VertexBuffer = {
                As<DxBuffer>(source.vertices).native->GetGPUVirtualAddress() + source.vertexOffset,
                source.vertexStride};
            if (source.indices)
            {
                geometry.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
                geometry.Triangles.IndexCount = source.indexCount;
                geometry.Triangles.IndexBuffer =
                    As<DxBuffer>(source.indices).native->GetGPUVirtualAddress() +
                    source.indexOffset;
            }
            else
            {
                geometry.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
                geometry.Triangles.IndexCount = 0;
                geometry.Triangles.IndexBuffer = 0;
            }
        }
    }
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = desc.topLevel ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL
                                : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    if (desc.allowUpdate)
        inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
    inputs.NumDescs = desc.topLevel ? static_cast<UINT>(desc.instances.size())
                                    : static_cast<UINT>(geometries.size());
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.pGeometryDescs = geometries.empty() ? nullptr : geometries.data();
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
    rayDevice_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
    return {info.ResultDataMaxSizeInBytes, info.ScratchDataSizeInBytes,
            info.UpdateScratchDataSizeInBytes};
}

std::shared_ptr<AccelerationStructure>
DxRuntime::CreateAccelerationStructureNative(AccelerationStructureDesc desc,
                                             AccelerationStructureSizes sizes)
{
    const auto storage = CreateBuffer(
        {sizes.storage, Memory::Device, desc.name + ".Storage", false, true, false, true});
    const auto scratchBytes = std::max<uint64_t>(256, std::max(sizes.scratch, sizes.updateScratch));
    const auto scratch =
        CreateBuffer({scratchBytes, Memory::Device, desc.name + ".Scratch", true, true});
    std::shared_ptr<Buffer> instances;
    if (desc.topLevel)
    {
        const auto bytes =
            std::max<size_t>(1, desc.instances.size()) * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
        instances = CreateBuffer(
            {bytes, Memory::Upload, desc.name + ".Instances", false, true, true, false});
        auto mapped = instances->Map();
        std::memset(mapped.data(), 0, mapped.size());
        auto* native = reinterpret_cast<D3D12_RAYTRACING_INSTANCE_DESC*>(mapped.data());
        for (size_t i = 0; i < desc.instances.size(); ++i)
        {
            const auto& source = desc.instances[i];
            auto& target = native[i];
            std::memcpy(target.Transform, source.transform.data(), sizeof(target.Transform));
            target.InstanceID = source.instanceId;
            target.InstanceMask = source.mask;
            target.InstanceContributionToHitGroupIndex = 0;
            target.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
            target.AccelerationStructure =
                As<DxBuffer>(source.bottomLevel->storage).native->GetGPUVirtualAddress();
        }
    }
    return std::make_shared<DxAccelerationStructure>(this, std::move(desc), sizes, storage, scratch,
                                                     instances);
}

D3D12_RESOURCE_DESC DxRuntime::TextureDescription(const TextureDesc& desc) const
{
    Validate(desc);
    D3D12_RESOURCE_DESC resource{};
    resource.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource.Width = desc.width;
    resource.Height = desc.height;
    resource.DepthOrArraySize = 1;
    resource.MipLevels = 1;
    resource.Format =
        IsDepthFormat(desc.format)
            ? desc.format == Format::D32 ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_R32G8X24_TYPELESS
            : NativeFormat(desc.format);
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
    support.Format = NativeFormat(desc.format);
    Check(device_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)),
          "Texture format support");
    const auto required = IsDepthFormat(desc.format)
                              ? D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL
                              : D3D12_FORMAT_SUPPORT1_SHADER_LOAD |
                                    (desc.attachment ? D3D12_FORMAT_SUPPORT1_RENDER_TARGET
                                                     : D3D12_FORMAT_SUPPORT1_NONE);
    if ((support.Support1 & required) != required)
        throw std::runtime_error("Texture format lacks requested usage: " + desc.name +
                                 " (format=" + std::to_string(static_cast<uint32_t>(desc.format)) +
                                 ", support=" + std::to_string(support.Support1) +
                                 ", required=" + std::to_string(required) + ")");
    if (IsDepthFormat(desc.format))
    {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT sampled{};
        sampled.Format = desc.format == Format::D32 ? DXGI_FORMAT_R32_FLOAT
                                                    : DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        Check(device_->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &sampled, sizeof(sampled)),
              "Depth sampled format support");
        if ((sampled.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) == 0)
            throw std::runtime_error("Depth format cannot be sampled: " + desc.name);
    }
    resource.SampleDesc.Count = 1;
    if (desc.storage)
        resource.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (desc.attachment)
        resource.Flags |= IsDepthFormat(desc.format) ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                                                     : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    return resource;
}

std::shared_ptr<Texture> DxRuntime::CreateTexture(const TextureDesc& desc)
{
    const auto resource = TextureDescription(desc);
    auto texture = std::make_shared<DxTexture>(this, desc);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    texture->allocation.Set(ledger_,
                            device_->GetResourceAllocationInfo(0, 1, &resource).SizeInBytes);
    const auto clear = TextureClearValue(desc);
    Check(device_->CreateCommittedResource(
              &heap, D3D12_HEAP_FLAG_NONE, &resource, D3D12_RESOURCE_STATE_COMMON,
              desc.attachment ? &clear : nullptr, IID_PPV_ARGS(&texture->native)),
          "CreateTexture");
    Check(texture->native->SetName(platform::Utf8ToWide(desc.name).c_str()), "Texture SetName");
    return texture;
}

uint64_t DxRuntime::QueryAliasedTextureBytes(std::span<const TextureDesc> descs)
{
    ValidateAliasDescs(descs);
    uint64_t bytes = 0;
    for (const auto& desc : descs)
    {
        const auto resource = TextureDescription(desc);
        const auto requirements = device_->GetResourceAllocationInfo(0, 1, &resource);
        if (requirements.SizeInBytes == UINT64_MAX)
            throw std::runtime_error("Invalid placed texture requirements");
        bytes = std::max(bytes, requirements.SizeInBytes);
    }
    return bytes;
}
std::vector<std::shared_ptr<Texture>>
DxRuntime::CreateAliasedTextures(std::span<const TextureDesc> descs)
{
    const auto bytes = QueryAliasedTextureBytes(descs);
    struct Backing
    {
        Allocation allocation;
        ComPtr<ID3D12Heap> heap;
    };
    auto backing = std::make_shared<Backing>();
    backing->allocation.Set(ledger_, bytes);
    D3D12_HEAP_DESC heap{};
    heap.SizeInBytes = bytes;
    heap.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
    Check(device_->CreateHeap(&heap, IID_PPV_ARGS(&backing->heap)), "Create alias heap");
    std::vector<std::shared_ptr<Texture>> textures;
    textures.reserve(descs.size());
    for (const auto& desc : descs)
    {
        const auto resource = TextureDescription(desc);
        auto texture = std::make_shared<DxTexture>(this, desc);
        texture->aliasBacking = backing;
        const auto clear = TextureClearValue(desc);
        Check(device_->CreatePlacedResource(backing->heap.Get(), 0, &resource,
                                            D3D12_RESOURCE_STATE_COMMON, &clear,
                                            IID_PPV_ARGS(&texture->native)),
              "CreatePlacedResource alias texture");
        Check(texture->native->SetName(platform::Utf8ToWide(desc.name).c_str()), "Alias SetName");
        textures.push_back(std::move(texture));
    }
    return textures;
}

std::shared_ptr<TextureView> DxRuntime::CreateView(std::shared_ptr<Texture> texture,
                                                   TextureViewUsage usage)
{
    CheckOwner(texture);
    if (usage == TextureViewUsage::Sampled)
    {
        if (auto cached = texture->sampledView.lock())
            return cached;
    }
    else if (auto cached = texture->defaultView.lock())
        return cached;
    auto cpuTimer = ProfileCpu(CpuOperation::ViewCreate);
    auto view = std::make_shared<DxView>(texture, usage);
    if (texture->desc.attachment && usage == TextureViewUsage::Default)
    {
        const bool depth = IsDepthFormat(texture->desc.format);
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.NumDescriptors = 1;
        heap.Type = depth ? D3D12_DESCRIPTOR_HEAP_TYPE_DSV : D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        Check(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&view->heap)), "CreateView heap");
        view->handle = view->heap->GetCPUDescriptorHandleForHeapStart();
        if (depth)
        {
            D3D12_DEPTH_STENCIL_VIEW_DESC desc{};
            desc.Format = NativeFormat(texture->desc.format);
            desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            device_->CreateDepthStencilView(As<DxTexture>(texture).native.Get(), &desc,
                                            view->handle);
        }
        else
        {
            D3D12_RENDER_TARGET_VIEW_DESC desc{};
            desc.Format = NativeFormat(texture->desc.format);
            desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            device_->CreateRenderTargetView(As<DxTexture>(texture).native.Get(), &desc,
                                            view->handle);
        }
    }
    if (usage == TextureViewUsage::Sampled)
        texture->sampledView = view;
    else
        texture->defaultView = view;
    return view;
}

std::shared_ptr<Sampler> DxRuntime::CreateSampler()
{
    return std::make_shared<Sampler>(this);
}
} // namespace renderlab::rhi::dx12
