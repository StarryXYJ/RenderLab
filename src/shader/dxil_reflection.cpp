#include "shader/dxc_compiler.hpp"

#include <Windows.h>
#include <d3d12shader.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <format>
#include <stdexcept>

namespace renderlab::shader
{
namespace
{
using Microsoft::WRL::ComPtr;
void Check(HRESULT result, const char* operation)
{
    if (FAILED(result))
        throw std::runtime_error(std::format("{} failed (0x{:08X})", operation, uint32_t(result)));
}
} // namespace
std::vector<ReflectedBinding> ReflectDxil(const std::vector<std::byte>& code)
{
    ComPtr<IDxcUtils> utils;
    Check(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils)), "Reflection utils");
    ComPtr<IDxcBlobEncoding> blob;
    Check(utils->CreateBlob(code.data(), static_cast<UINT32>(code.size()), DXC_CP_ACP, &blob),
          "DXIL blob");
    ComPtr<IDxcContainerReflection> container;
    Check(DxcCreateInstance(CLSID_DxcContainerReflection, IID_PPV_ARGS(&container)),
          "DXIL container reflection");
    Check(container->Load(blob.Get()), "Load DXIL reflection");
    UINT32 part{};
    Check(container->FindFirstPartKind(DXC_PART_DXIL, &part), "DXIL part");
    ComPtr<ID3D12ShaderReflection> reflection;
    Check(container->GetPartReflection(part, IID_PPV_ARGS(&reflection)),
          "DXIL reflection interface");
    D3D12_SHADER_DESC desc{};
    Check(reflection->GetDesc(&desc), "ShaderDesc");
    std::vector<ReflectedBinding> result;
    for (UINT i = 0; i < desc.BoundResources; ++i)
    {
        D3D12_SHADER_INPUT_BIND_DESC input{};
        Check(reflection->GetResourceBindingDesc(i, &input), "ResourceBindingDesc");
        ReflectedBinding binding;
        binding.name = input.Name;
        binding.slot = input.BindPoint;
        binding.space = input.Space;
        binding.count = input.BindCount;
        switch (input.Type)
        {
        case D3D_SIT_CBUFFER:
            binding.kind = rhi::BindingKind::ConstantBuffer;
            break;
        case D3D_SIT_TEXTURE:
            binding.kind = rhi::BindingKind::SampledTexture;
            break;
        case D3D_SIT_SAMPLER:
            binding.kind = rhi::BindingKind::Sampler;
            break;
        case D3D_SIT_UAV_RWTYPED:
            binding.kind = rhi::BindingKind::StorageTexture;
            break;
        case D3D_SIT_STRUCTURED:
            binding.kind = rhi::BindingKind::ReadOnlyBuffer;
            break;
        case D3D_SIT_UAV_RWSTRUCTURED:
            binding.kind = rhi::BindingKind::StorageBuffer;
            break;
        default:
            throw std::runtime_error("Unsupported DXIL resource: " + binding.name);
        }
        if (binding.kind == rhi::BindingKind::ConstantBuffer)
        {
            auto* buffer = reflection->GetConstantBufferByName(input.Name);
            D3D12_SHADER_BUFFER_DESC block{};
            Check(buffer->GetDesc(&block), "CBufferDesc");
            const auto append = [&](const char* name, ID3D12ShaderReflectionType* type,
                                    uint32_t offset, uint32_t size)
            {
                D3D12_SHADER_TYPE_DESC t{};
                Check(type->GetDesc(&t), "MemberType");
                ReflectedField field;
                field.name = name;
                field.offset = offset;
                field.size = size;
                field.rows = t.Class == D3D_SVC_MATRIX_COLUMNS || t.Class == D3D_SVC_MATRIX_ROWS
                                 ? t.Rows
                                 : t.Columns;
                field.columns = t.Class == D3D_SVC_MATRIX_COLUMNS || t.Class == D3D_SVC_MATRIX_ROWS
                                    ? t.Columns
                                    : 1;
                field.count = std::max(t.Elements, 1u);
                field.matrixStride = field.columns > 1 ? 16u : 0u;
                field.scalar = t.Type == D3D_SVT_INT    ? Scalar::Int
                               : t.Type == D3D_SVT_UINT ? Scalar::Uint
                               : t.Type == D3D_SVT_BOOL ? Scalar::Bool
                                                        : Scalar::Float;
                field.columnMajor = t.Class != D3D_SVC_MATRIX_ROWS;
                binding.fields.push_back(std::move(field));
            };
            for (UINT v = 0; v < block.Variables; ++v)
            {
                auto* variable = buffer->GetVariableByIndex(v);
                D3D12_SHADER_VARIABLE_DESC var{};
                Check(variable->GetDesc(&var), "VariableDesc");
                auto* type = variable->GetType();
                D3D12_SHADER_TYPE_DESC t{};
                Check(type->GetDesc(&t), "VariableType");
                if (t.Class == D3D_SVC_STRUCT)
                {
                    for (UINT m = 0; m < t.Members; ++m)
                    {
                        auto* member = type->GetMemberTypeByIndex(m);
                        D3D12_SHADER_TYPE_DESC mt{};
                        member->GetDesc(&mt);
                        append(type->GetMemberTypeName(m), member, var.StartOffset + mt.Offset, 0);
                    }
                }
                else
                    append(var.Name, type, var.StartOffset, var.Size);
            }
        }
        result.push_back(std::move(binding));
    }
    return result;
}
} // namespace renderlab::shader
