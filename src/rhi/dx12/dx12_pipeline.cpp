#include "rhi/dx12/dx12_objects.hpp"
#include "rhi/dx12/dx12_runtime.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace renderlab::rhi::dx12
{
namespace
{
D3D12_COMPARISON_FUNC Native(CompareOp value)
{
    constexpr std::array values = {
        D3D12_COMPARISON_FUNC_NEVER,         D3D12_COMPARISON_FUNC_LESS,
        D3D12_COMPARISON_FUNC_EQUAL,         D3D12_COMPARISON_FUNC_LESS_EQUAL,
        D3D12_COMPARISON_FUNC_GREATER,       D3D12_COMPARISON_FUNC_NOT_EQUAL,
        D3D12_COMPARISON_FUNC_GREATER_EQUAL, D3D12_COMPARISON_FUNC_ALWAYS};
    return values.at(static_cast<size_t>(value));
}
D3D12_STENCIL_OP Native(StencilOp value)
{
    constexpr std::array values = {D3D12_STENCIL_OP_KEEP,     D3D12_STENCIL_OP_ZERO,
                                   D3D12_STENCIL_OP_REPLACE,  D3D12_STENCIL_OP_INCR_SAT,
                                   D3D12_STENCIL_OP_DECR_SAT, D3D12_STENCIL_OP_INVERT,
                                   D3D12_STENCIL_OP_INCR,     D3D12_STENCIL_OP_DECR};
    return values.at(static_cast<size_t>(value));
}
D3D12_BLEND Native(BlendFactor value)
{
    constexpr std::array values = {
        D3D12_BLEND_ZERO,           D3D12_BLEND_ONE,           D3D12_BLEND_SRC_COLOR,
        D3D12_BLEND_INV_SRC_COLOR,  D3D12_BLEND_DEST_COLOR,    D3D12_BLEND_INV_DEST_COLOR,
        D3D12_BLEND_SRC_ALPHA,      D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_DEST_ALPHA,
        D3D12_BLEND_INV_DEST_ALPHA, D3D12_BLEND_SRC_ALPHA_SAT};
    return values.at(static_cast<size_t>(value));
}
D3D12_BLEND_OP Native(BlendOp value)
{
    constexpr std::array values = {D3D12_BLEND_OP_ADD, D3D12_BLEND_OP_SUBTRACT,
                                   D3D12_BLEND_OP_REV_SUBTRACT, D3D12_BLEND_OP_MIN,
                                   D3D12_BLEND_OP_MAX};
    return values.at(static_cast<size_t>(value));
}
D3D12_DEPTH_STENCILOP_DESC Native(const StencilFaceState& face)
{
    return {Native(face.fail), Native(face.depthFail), Native(face.pass), Native(face.compare)};
}
} // namespace

std::shared_ptr<BindingLayout> DxRuntime::CreateLayout(std::vector<BindingDesc> entries)
{
    auto layout = std::make_shared<DxLayout>(this, std::move(entries));
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges(layout->entries.size());
    std::vector<D3D12_ROOT_PARAMETER> parameters(layout->entries.size());
    for (size_t i = 0; i < ranges.size(); ++i)
    {
        const auto& entry = layout->entries[i];
        if (entry.space >= 4 || layout->entries.size() > 64 ||
            std::ranges::count_if(layout->entries, [&](auto e)
                                  { return e.slot == entry.slot && e.space == entry.space; }) != 1)
            throw std::invalid_argument("Duplicate binding slot");
        auto& range = ranges[i];
        range.RangeType =
            entry.kind == BindingKind::Sampler ? D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER
            : entry.kind == BindingKind::StorageTexture || entry.kind == BindingKind::StorageBuffer
                ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV
            : entry.kind == BindingKind::ConstantBuffer ? D3D12_DESCRIPTOR_RANGE_TYPE_CBV
                                                        : D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        range.BaseShaderRegister = entry.slot;
        range.RegisterSpace = entry.space;
        auto& param = parameters[i];
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        param.DescriptorTable = {1, &range};
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = static_cast<UINT>(parameters.size());
    desc.pParameters = parameters.data();
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> binary, error;
    Check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &binary, &error),
          "SerializeRootSignature");
    Check(device_->CreateRootSignature(0, binary->GetBufferPointer(), binary->GetBufferSize(),
                                       IID_PPV_ARGS(&layout->native)),
          "CreateRootSignature");
    return layout;
}

std::shared_ptr<BindingGroup> DxRuntime::CreateGroup(std::shared_ptr<BindingLayout> layout,
                                                     std::vector<Binding> bindings)
{
    Validate(layout, bindings);
    auto group = std::make_shared<DxGroup>(layout, std::move(bindings));
    const auto samplerCount = static_cast<UINT>(std::ranges::count_if(
        layout->entries, [](auto e) { return e.kind == BindingKind::Sampler; }));
    const auto resourceCount = static_cast<UINT>(layout->entries.size()) - samplerCount;
    const auto createHeap = [&](UINT count, D3D12_DESCRIPTOR_HEAP_TYPE type, auto& heap)
    {
        if (!count)
            return;
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.NumDescriptors = count;
        desc.Type = type;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Check(device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)), "Binding heap");
    };
    createHeap(resourceCount, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, group->resources);
    createHeap(samplerCount, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, group->samplers);
    UINT resourceIndex = 0, samplerIndex = 0;
    for (auto entry : layout->entries)
    {
        const auto& binding =
            *std::ranges::find_if(group->bindings, [&](const Binding& b)
                                  { return b.slot == entry.slot && b.space == entry.space; });
        const bool sampler = entry.kind == BindingKind::Sampler;
        auto* heap = sampler ? group->samplers.Get() : group->resources.Get();
        const auto type =
            sampler ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        const auto offset = (sampler ? samplerIndex++ : resourceIndex++) *
                            device_->GetDescriptorHandleIncrementSize(type);
        auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += offset;
        auto gpu = heap->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr += offset;
        group->handles.push_back(gpu);
        if (sampler)
        {
            D3D12_SAMPLER_DESC desc{};
            desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
            desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            desc.MaxLOD = D3D12_FLOAT32_MAX;
            desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NONE;
            device_->CreateSampler(&desc, cpu);
        }
        else if (entry.kind == BindingKind::ConstantBuffer)
        {
            D3D12_CONSTANT_BUFFER_VIEW_DESC desc{};
            desc.BufferLocation =
                As<DxBuffer>(binding.buffer).native->GetGPUVirtualAddress() + binding.offset;
            desc.SizeInBytes = static_cast<UINT>(binding.size);
            device_->CreateConstantBufferView(&desc, cpu);
        }
        else if (entry.kind == BindingKind::ReadOnlyBuffer)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
            desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            desc.Buffer.FirstElement = binding.offset / binding.stride;
            desc.Buffer.NumElements = static_cast<UINT>(binding.size / binding.stride);
            desc.Buffer.StructureByteStride = binding.stride;
            device_->CreateShaderResourceView(As<DxBuffer>(binding.buffer).native.Get(), &desc,
                                              cpu);
        }
        else if (entry.kind == BindingKind::StorageBuffer)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
            desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            desc.Buffer.FirstElement = binding.offset / binding.stride;
            desc.Buffer.NumElements = static_cast<UINT>(binding.size / binding.stride);
            desc.Buffer.StructureByteStride = binding.stride;
            device_->CreateUnorderedAccessView(As<DxBuffer>(binding.buffer).native.Get(), nullptr,
                                               &desc, cpu);
        }
        else if (entry.kind == BindingKind::StorageTexture)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
            desc.Format = NativeFormat(binding.view->texture->desc.format);
            desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            device_->CreateUnorderedAccessView(As<DxTexture>(binding.view->texture).native.Get(),
                                               nullptr, &desc, cpu);
        }
        else
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
            desc.Format = NativeFormat(binding.view->texture->desc.format);
            desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            desc.Texture2D.MipLevels = 1;
            device_->CreateShaderResourceView(As<DxTexture>(binding.view->texture).native.Get(),
                                              &desc, cpu);
        }
    }
    return group;
}

std::shared_ptr<Pipeline> DxRuntime::CreatePipelineNative(PipelineDesc desc)
{
    auto pipeline = std::make_shared<DxPipeline>(this, desc);
    const auto bytecode = [](const auto& module) -> D3D12_SHADER_BYTECODE
    { return {module->code.data(), module->code.size()}; };
    auto* root = As<DxLayout>(desc.layout).native.Get();
    if (desc.compute)
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC state{};
        state.pRootSignature = root;
        state.CS = bytecode(desc.compute);
        Check(device_->CreateComputePipelineState(&state, IID_PPV_ARGS(&pipeline->native)),
              "Compute PSO");
    }
    else
    {
        std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
        for (const auto& attribute : desc.attributes)
        {
            const auto format = attribute.format == VertexFormat::Float2 ? DXGI_FORMAT_R32G32_FLOAT
                                : attribute.format == VertexFormat::Float3
                                    ? DXGI_FORMAT_R32G32B32_FLOAT
                                    : DXGI_FORMAT_R32G32B32A32_FLOAT;
            elements.push_back({attribute.semantic.c_str(), 0, format, 0, attribute.offset,
                                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC state{};
        state.pRootSignature = root;
        state.VS = bytecode(desc.vertex);
        state.PS = bytecode(desc.pixel);
        state.InputLayout = {elements.data(), static_cast<UINT>(elements.size())};
        state.SampleMask = UINT_MAX;
        state.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        state.RasterizerState.CullMode = desc.raster.cull == CullMode::Back ? D3D12_CULL_MODE_BACK
                                         : desc.raster.cull == CullMode::Front
                                             ? D3D12_CULL_MODE_FRONT
                                             : D3D12_CULL_MODE_NONE;
        state.RasterizerState.FrontCounterClockwise =
            desc.raster.frontFace == FrontFace::CounterClockwise;
        state.RasterizerState.DepthClipEnable = TRUE;
        auto& blend = state.BlendState.RenderTarget[0];
        blend.BlendEnable = desc.blend.enabled;
        blend.SrcBlend = Native(desc.blend.srcColor);
        blend.DestBlend = Native(desc.blend.dstColor);
        blend.BlendOp = Native(desc.blend.colorOp);
        blend.SrcBlendAlpha = Native(desc.blend.srcAlpha);
        blend.DestBlendAlpha = Native(desc.blend.dstAlpha);
        blend.BlendOpAlpha = Native(desc.blend.alphaOp);
        blend.LogicOp = D3D12_LOGIC_OP_NOOP;
        blend.RenderTargetWriteMask = desc.raster.colorWriteMask;
        state.DepthStencilState.DepthEnable = desc.depth.testEnabled || desc.depth.writeEnabled;
        state.DepthStencilState.DepthWriteMask =
            desc.depth.writeEnabled ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        state.DepthStencilState.DepthFunc =
            Native(desc.depth.testEnabled ? desc.depth.compare : CompareOp::Always);
        state.DepthStencilState.StencilEnable = desc.stencil.enabled;
        state.DepthStencilState.FrontFace = Native(desc.stencil.front);
        state.DepthStencilState.BackFace = Native(desc.stencil.back);
        state.DepthStencilState.StencilReadMask = desc.stencil.readMask;
        state.DepthStencilState.StencilWriteMask = desc.stencil.writeMask;
        state.NumRenderTargets = 1;
        state.RTVFormats[0] = NativeFormat(desc.color);
        state.DSVFormat =
            desc.depthStencilFormat ? NativeFormat(*desc.depthStencilFormat) : DXGI_FORMAT_UNKNOWN;
        state.SampleDesc.Count = 1;
        state.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        Check(device_->CreateGraphicsPipelineState(&state, IID_PPV_ARGS(&pipeline->native)),
              "Graphics PSO");
    }
    Check(pipeline->native->SetName(desc.compute ? L"M1.Compute" : L"M1.Graphics"), "PSO SetName");
    return pipeline;
}
} // namespace renderlab::rhi::dx12
