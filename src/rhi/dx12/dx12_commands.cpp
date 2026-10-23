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
        queryDesc.Count = 2;
        Check(device_->CreateQueryHeap(&queryDesc, IID_PPV_ARGS(&work->queries)),
              "Timestamp QueryHeap");
        contexts_.push_back(work);
    }
    auto* list = work->list.Get();
    bool compute = false;
    uint32_t vertexStride = 0;
    const auto barrier = [&](ID3D12Resource* resource, State before, State after)
    {
        D3D12_RESOURCE_BARRIER b{};
        if (before == State::Storage && after == State::Storage)
        {
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            b.UAV.pResource = resource;
        }
        else
        {
            if (NativeState(before) == NativeState(after))
                return;
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, NativeState(before),
                            NativeState(after)};
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
                        barrier(As<DxBuffer>(c.buffer).native.Get(), c.before, c.after);
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
                else if constexpr (std::is_same_v<T, BeginRendering>)
                {
                    const auto rtv = As<DxView>(c.color).handle;
                    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
                    if (c.depth)
                        dsv = As<DxView>(c.depth).handle;
                    list->OMSetRenderTargets(1, &rtv, FALSE, c.depth ? &dsv : nullptr);
                    list->ClearRenderTargetView(rtv, c.clear.data(), 0, nullptr);
                    if (c.depth)
                        list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0, 0, 0, nullptr);
                    const auto& desc = c.color->texture->desc;
                    D3D12_VIEWPORT viewport{0, 0, float(desc.width), float(desc.height), 0, 1};
                    D3D12_RECT scissor{0, 0, static_cast<LONG>(desc.width),
                                       static_cast<LONG>(desc.height)};
                    list->RSSetViewports(1, &viewport);
                    list->RSSetScissorRects(1, &scissor);
                }
                else if constexpr (std::is_same_v<T, SetPipeline>)
                {
                    compute = bool(c.pipeline->desc.compute);
                    vertexStride = c.pipeline->desc.vertexStride;
                    list->SetPipelineState(As<DxPipeline>(c.pipeline).native.Get());
                    auto* root = As<DxLayout>(c.pipeline->desc.layout).native.Get();
                    if (compute)
                        list->SetComputeRootSignature(root);
                    else
                        list->SetGraphicsRootSignature(root);
                }
                else if constexpr (std::is_same_v<T, SetBindings>)
                {
                    auto& group = As<DxGroup>(c.group);
                    ID3D12DescriptorHeap* heaps[2];
                    UINT count = 0;
                    if (group.resources)
                        heaps[count++] = group.resources.Get();
                    if (group.samplers)
                        heaps[count++] = group.samplers.Get();
                    list->SetDescriptorHeaps(count, heaps);
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
                        list->DrawIndexedInstanced(c.count, 1, c.first, 0, 0);
                    else
                        list->DrawInstanced(c.count, 1, c.first, 0);
                }
                else if constexpr (std::is_same_v<T, DrawIndirect>)
                    list->ExecuteIndirect(indirect_.Get(), 1,
                                          As<DxBuffer>(c.arguments).native.Get(), c.offset, nullptr,
                                          0);
                else if constexpr (std::is_same_v<T, Dispatch>)
                    list->Dispatch(c.x, c.y, c.z);
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
                    list->ResolveQueryData(work->queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2,
                                           As<DxBuffer>(c.destination).native.Get(), 0);
            },
            command);
    Check(list->Close(), "Close command list");
    ID3D12CommandList* lists[] = {list};
    queue_->ExecuteCommandLists(1, lists);
    Check(queue_->Signal(fence_.Get(), point), "Signal graphics fence");
    return work;
}
} // namespace renderlab::rhi::dx12
