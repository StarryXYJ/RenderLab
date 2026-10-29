#include "rhi/dx12/dx12_runtime.hpp"

#include "rhi/commands.hpp"
#include "rhi/dx12/dx12_objects.hpp"

#include <algorithm>
#include <pix3.h>
#include <type_traits>

namespace renderlab::rhi::dx12
{
std::shared_ptr<Runtime::Submission> DxRuntime::Execute(const CommandList& commands, uint64_t point,
                                                        Swapchain*)
{
    std::shared_ptr<Work> work;
    const auto reusable =
        std::ranges::find_if(contexts_, [](const auto& w) { return w.use_count() == 1; });
    if (reusable != contexts_.end())
    {
        work = *reusable;
        Check(work->allocator->Reset(), "Reset retired command allocator");
        Check(work->list->Reset(work->allocator.Get(), nullptr), "Reset retired command list");
    }
    else
    {
        work = std::make_shared<Work>();
        Check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(&work->allocator)),
              "CommandAllocator");
        Check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, work->allocator.Get(),
                                         nullptr, IID_PPV_ARGS(&work->list)),
              "CommandList");
        D3D12_QUERY_HEAP_DESC queryDesc{};
        queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        queryDesc.Count = MaxTimestampSlots;
        Check(device_->CreateQueryHeap(&queryDesc, IID_PPV_ARGS(&work->queries)),
              "Timestamp QueryHeap");
        contexts_.push_back(work);
    }
    auto* list = work->list.Get();
    bool compute = false;
    uint32_t vertexStride = 0;
    ID3D12DescriptorHeap* resourceHeap = nullptr;
    ID3D12DescriptorHeap* samplerHeap = nullptr;
    const auto barrier = [&](ID3D12Resource* resource, State before, State after,
                             bool accelerationStructureStorage = false)
    {
        const auto nativeState = [&](State state)
        {
            if (accelerationStructureStorage)
                return D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
            return NativeState(state);
        };
        D3D12_RESOURCE_BARRIER b{};
        if ((before == State::Storage || before == State::AccelerationStructureBuildWrite) &&
            after == before)
        {
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            b.UAV.pResource = resource;
        }
        else
        {
            if (nativeState(before) == nativeState(after))
            {
                if (accelerationStructureStorage &&
                    before == State::AccelerationStructureBuildWrite &&
                    (after == State::AccelerationStructureBuildWrite ||
                     after == State::AccelerationStructureRead))
                {
                    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                    b.UAV.pResource = resource;
                    list->ResourceBarrier(1, &b);
                }
                return;
            }
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, nativeState(before),
                            nativeState(after)};
        }
        list->ResourceBarrier(1, &b);
    };
    for (const auto& command : commands.GetCommands())
        std::visit(
            [&](const auto& c)
            {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, BufferBarrier>)
                {
                    if (c.buffer->desc.memory == Memory::Device)
                        barrier(As<DxBuffer>(c.buffer).native.Get(), c.before, c.after,
                                c.buffer->desc.accelerationStructureStorage);
                }
                else if constexpr (std::is_same_v<T, TextureBarrier>)
                    barrier(As<DxTexture>(c.texture).native.Get(), c.before, c.after);
                else if constexpr (std::is_same_v<T, BufferCopy>)
                    list->CopyBufferRegion(As<DxBuffer>(c.destination).native.Get(),
                                           c.destinationOffset, As<DxBuffer>(c.source).native.Get(),
                                           c.sourceOffset, c.size);
                else if constexpr (std::is_same_v<T, TextureCopy>)
                {
                    D3D12_TEXTURE_COPY_LOCATION buffer{}, texture{};
                    buffer.pResource = As<DxBuffer>(c.buffer).native.Get();
                    buffer.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    buffer.PlacedFootprint.Offset = c.offset;
                    buffer.PlacedFootprint.Footprint = {NativeFormat(c.texture->desc.format),
                                                        c.texture->desc.width,
                                                        c.texture->desc.height, 1, c.rowPitch};
                    texture.pResource = As<DxTexture>(c.texture).native.Get();
                    texture.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    list->CopyTextureRegion(c.upload ? &texture : &buffer, 0, 0, 0,
                                            c.upload ? &buffer : &texture, nullptr);
                }
                else if constexpr (std::is_same_v<T, TextureToTextureCopy>)
                {
                    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
                    src.pResource = As<DxTexture>(c.source).native.Get();
                    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    dst.pResource = As<DxTexture>(c.destination).native.Get();
                    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                }
                else if constexpr (std::is_same_v<T, BeginRendering>)
                {
                    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs;
                    rtvs.reserve(c.colors.size());
                    for (const auto& color : c.colors)
                        rtvs.push_back(As<DxView>(color).handle);
                    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
                    if (c.depth)
                        dsv = As<DxView>(c.depth).handle;
                    list->OMSetRenderTargets(static_cast<UINT>(rtvs.size()), rtvs.data(), FALSE,
                                             c.depth ? &dsv : nullptr);
                    if (c.colorLoad == LoadOp::Clear)
                        for (const auto& rtv : rtvs)
                            list->ClearRenderTargetView(rtv, c.clear.data(), 0, nullptr);
                    if (c.depth)
                    {
                        UINT flags = c.depthLoad == LoadOp::Clear ? D3D12_CLEAR_FLAG_DEPTH : 0;
                        if (HasStencil(c.depth->texture->desc.format) &&
                            c.stencilLoad == LoadOp::Clear)
                            flags |= D3D12_CLEAR_FLAG_STENCIL;
                        if (flags)
                            list->ClearDepthStencilView(dsv, static_cast<D3D12_CLEAR_FLAGS>(flags),
                                                        0, static_cast<UINT8>(c.clearStencil), 0,
                                                        nullptr);
                    }
                    const auto& desc = c.colors.front()->texture->desc;
                    D3D12_VIEWPORT viewport{0, 0, float(desc.width), float(desc.height), 0, 1};
                    D3D12_RECT scissor{0, 0, static_cast<LONG>(desc.width),
                                       static_cast<LONG>(desc.height)};
                    list->RSSetViewports(1, &viewport);
                    list->RSSetScissorRects(1, &scissor);
                }
                else if constexpr (std::is_same_v<T, SetPipeline>)
                {
                    compute = bool(c.pipeline->desc.compute || c.pipeline->desc.rayGeneration);
                    vertexStride = c.pipeline->desc.vertexStride;
                    if (c.pipeline->desc.rayGeneration)
                    {
                        ComPtr<ID3D12GraphicsCommandList4> rayList;
                        Check(list->QueryInterface(IID_PPV_ARGS(&rayList)),
                              "RayTracing command list");
                        rayList->SetPipelineState1(As<DxPipeline>(c.pipeline).rayState.Get());
                    }
                    else
                        list->SetPipelineState(As<DxPipeline>(c.pipeline).native.Get());
                    auto* root = As<DxLayout>(c.pipeline->desc.layout).native.Get();
                    if (compute)
                        list->SetComputeRootSignature(root);
                    else
                    {
                        list->SetGraphicsRootSignature(root);
                        list->OMSetStencilRef(0);
                    }
                }
                else if constexpr (std::is_same_v<T, SetStencilReference>)
                    list->OMSetStencilRef(c.reference);
                else if constexpr (std::is_same_v<T, SetBindings>)
                {
                    auto& group = As<DxGroup>(c.group);
                    ID3D12DescriptorHeap* heaps[2];
                    UINT count = 0;
                    if (group.resources)
                        heaps[count++] = group.resources.Get();
                    if (group.samplers)
                        heaps[count++] = group.samplers.Get();
                    if (resourceHeap != group.resources.Get() ||
                        samplerHeap != group.samplers.Get())
                    {
                        auto cpuTimer = ProfileCpu(CpuOperation::DescriptorHeapBind);
                        list->SetDescriptorHeaps(count, heaps);
                        resourceHeap = group.resources.Get();
                        samplerHeap = group.samplers.Get();
                    }
                    for (UINT i = 0; i < group.handles.size(); ++i)
                        if (compute)
                            list->SetComputeRootDescriptorTable(i, group.handles[i]);
                        else
                            list->SetGraphicsRootDescriptorTable(i, group.handles[i]);
                }
                else if constexpr (std::is_same_v<T, SetGeometry>)
                {
                    D3D12_VERTEX_BUFFER_VIEW vb{
                        As<DxBuffer>(c.vertex).native->GetGPUVirtualAddress(),
                        static_cast<UINT>(c.vertex->desc.size), vertexStride};
                    list->IASetVertexBuffers(0, 1, &vb);
                    if (c.index)
                    {
                        D3D12_INDEX_BUFFER_VIEW ib{
                            As<DxBuffer>(c.index).native->GetGPUVirtualAddress(),
                            static_cast<UINT>(c.index->desc.size), DXGI_FORMAT_R32_UINT};
                        list->IASetIndexBuffer(&ib);
                    }
                    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                }
                else if constexpr (std::is_same_v<T, Draw>)
                {
                    if (c.indexed)
                        list->DrawIndexedInstanced(c.count, c.instances, c.first, c.vertexOffset,
                                                   c.firstInstance);
                    else
                        list->DrawInstanced(c.count, c.instances, c.first, c.firstInstance);
                }
                else if constexpr (std::is_same_v<T, DrawIndirect>)
                    list->ExecuteIndirect(
                        c.indexed ? indexedIndirect_.Get() : indirect_.Get(), c.maxCount,
                        As<DxBuffer>(c.arguments).native.Get(), c.offset,
                        c.countBuffer ? As<DxBuffer>(c.countBuffer).native.Get() : nullptr,
                        c.countOffset);
                else if constexpr (std::is_same_v<T, Dispatch>)
                    list->Dispatch(c.x, c.y, c.z);
                else if constexpr (std::is_same_v<T, TraceRays>)
                {
                    ComPtr<ID3D12GraphicsCommandList4> rayList;
                    Check(list->QueryInterface(IID_PPV_ARGS(&rayList)), "TraceRays command list");
                    auto address = As<DxBuffer>(c.table->buffer).native->GetGPUVirtualAddress() +
                                   c.table->offset;
                    const auto stride = c.table->stride;
                    D3D12_DISPATCH_RAYS_DESC desc{};
                    desc.RayGenerationShaderRecord = {address, stride};
                    desc.MissShaderTable = {address + stride, stride, stride};
                    desc.HitGroupTable = {address + stride * 2, stride, stride};
                    desc.Width = c.width;
                    desc.Height = c.height;
                    desc.Depth = c.depth;
                    rayList->DispatchRays(&desc);
                }
                else if constexpr (std::is_same_v<T, BuildAccelerationStructure>)
                {
                    ComPtr<ID3D12GraphicsCommandList4> rayList;
                    Check(list->QueryInterface(IID_PPV_ARGS(&rayList)),
                          "Query ID3D12GraphicsCommandList4");
                    const auto& accelerationStructure = *c.accelerationStructure;
                    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries;
                    if (!accelerationStructure.desc.topLevel)
                    {
                        geometries.resize(accelerationStructure.desc.geometries.size());
                        for (size_t i = 0; i < geometries.size(); ++i)
                        {
                            const auto& source = accelerationStructure.desc.geometries[i];
                            auto& geometry = geometries[i];
                            geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
                            geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
                            geometry.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
                            geometry.Triangles.VertexCount = source.vertexCount;
                            geometry.Triangles.VertexBuffer = {
                                As<DxBuffer>(source.vertices).native->GetGPUVirtualAddress() +
                                    source.vertexOffset,
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
                    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
                    build.Inputs.Type =
                        accelerationStructure.desc.topLevel
                            ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL
                            : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
                    build.Inputs.Flags =
                        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
                    if (accelerationStructure.desc.allowUpdate)
                        build.Inputs.Flags |=
                            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
                    if (c.update)
                    {
                        build.Inputs.Flags |=
                            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
                        build.SourceAccelerationStructureData =
                            As<DxBuffer>(accelerationStructure.storage)
                                .native->GetGPUVirtualAddress();
                    }
                    build.Inputs.NumDescs =
                        accelerationStructure.desc.topLevel
                            ? static_cast<UINT>(accelerationStructure.desc.instances.size())
                            : static_cast<UINT>(geometries.size());
                    build.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
                    build.Inputs.pGeometryDescs = geometries.empty() ? nullptr : geometries.data();
                    if (accelerationStructure.desc.topLevel)
                        build.Inputs.InstanceDescs =
                            As<DxBuffer>(accelerationStructure.instanceData)
                                .native->GetGPUVirtualAddress();
                    build.DestAccelerationStructureData =
                        As<DxBuffer>(accelerationStructure.storage).native->GetGPUVirtualAddress();
                    build.ScratchAccelerationStructureData =
                        As<DxBuffer>(c.scratch).native->GetGPUVirtualAddress();
                    rayList->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
                }
                else if constexpr (std::is_same_v<T, Marker>)
                {
                    if (c.begin)
                        PIXBeginEvent(list, PIX_COLOR_DEFAULT, "%s", c.name.c_str());
                    else
                        PIXEndEvent(list);
                }
                else if constexpr (std::is_same_v<T, Timestamp>)
                    list->EndQuery(work->queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, c.index);
                else if constexpr (std::is_same_v<T, ResolveTimestamps>)
                    list->ResolveQueryData(work->queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0,
                                           c.count, As<DxBuffer>(c.destination).native.Get(), 0);
            },
            command);
    Check(list->Close(), "Close command list");
    ID3D12CommandList* lists[] = {list};
    queue_->ExecuteCommandLists(1, lists);
    Check(queue_->Signal(fence_.Get(), point), "Signal graphics fence");
    return work;
}
} // namespace renderlab::rhi::dx12
