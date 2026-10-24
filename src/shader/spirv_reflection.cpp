#include "shader/dxc_compiler.hpp"

#include <spirv_reflect.h>

#include <algorithm>
#include <stdexcept>

namespace renderlab::shader
{
std::vector<ReflectedBinding> ReflectSpirv(const std::vector<std::byte>& code)
{
    struct Module
    {
        SpvReflectShaderModule value{};
        ~Module()
        {
            spvReflectDestroyShaderModule(&value);
        }
    } module;
    if (spvReflectCreateShaderModule(code.size(), code.data(), &module.value) !=
        SPV_REFLECT_RESULT_SUCCESS)
        throw std::runtime_error("Invalid SPIR-V reflection module");
    std::vector<ReflectedBinding> result;
    for (uint32_t i = 0; i < module.value.descriptor_binding_count; ++i)
    {
        const auto& input = module.value.descriptor_bindings[i];
        ReflectedBinding binding;
        binding.name = input.name ? input.name : "";
        binding.slot = input.binding;
        binding.space = input.set;
        binding.count = input.count;
        switch (input.descriptor_type)
        {
        case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            binding.kind = rhi::BindingKind::ConstantBuffer;
            break;
        case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            binding.kind = rhi::BindingKind::SampledTexture;
            break;
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            binding.kind = rhi::BindingKind::StorageTexture;
            break;
        case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
            binding.kind = rhi::BindingKind::Sampler;
            break;
        case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            binding.kind =
                (input.decoration_flags & SPV_REFLECT_DECORATION_NON_WRITABLE) ||
                        (input.block.decoration_flags & SPV_REFLECT_DECORATION_NON_WRITABLE) ||
                        (input.block.member_count && (input.block.members[0].decoration_flags &
                                                      SPV_REFLECT_DECORATION_NON_WRITABLE))
                    ? rhi::BindingKind::ReadOnlyBuffer
                    : rhi::BindingKind::StorageBuffer;
            break;
        default:
            throw std::runtime_error("Unsupported SPIR-V descriptor " + binding.name);
        }
        if (binding.kind == rhi::BindingKind::ConstantBuffer)
        {
            for (uint32_t m = 0; m < input.block.member_count; ++m)
            {
                const auto& member = input.block.members[m];
                ReflectedField field;
                field.name = member.name ? member.name : "";
                field.offset = member.offset;
                field.size = member.size;
                // DXC represents HLSL matrices transposed in SPIR-V and reverses major decoration.
                field.rows = member.numeric.matrix.column_count
                                 ? member.numeric.matrix.column_count
                                 : std::max(member.numeric.vector.component_count, 1u);
                field.columns = std::max(member.numeric.matrix.row_count, 1u);
                field.columnMajor = !member.numeric.matrix.column_count ||
                                    (member.decoration_flags & SPV_REFLECT_DECORATION_ROW_MAJOR);
                field.count = member.array.dims_count ? member.array.dims[0] : 1;
                field.arrayStride = member.array.stride;
                field.matrixStride = member.numeric.matrix.stride;
                const auto flags = member.type_description->type_flags;
                field.scalar = (flags & SPV_REFLECT_TYPE_FLAG_INT)
                                   ? (member.numeric.scalar.signedness ? Scalar::Int : Scalar::Uint)
                               : (flags & SPV_REFLECT_TYPE_FLAG_BOOL) ? Scalar::Bool
                                                                      : Scalar::Float;
                binding.fields.push_back(std::move(field));
            }
        }
        result.push_back(std::move(binding));
    }
    return result;
}
} // namespace renderlab::shader
