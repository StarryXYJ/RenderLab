#include "shader/definition.hpp"

#include <stdexcept>
#include <utility>

namespace renderlab::shader
{
Type ParseType(const std::string& name, const SourceLocation& source)
{
    Type result;
    result.name = name;
    const std::vector<std::pair<std::string, rhi::BindingKind>> resources = {
        {"Texture2D", rhi::BindingKind::SampledTexture},
        {"RWTexture2D", rhi::BindingKind::StorageTexture},
        {"SamplerState", rhi::BindingKind::Sampler},
        {"StructuredBuffer", rhi::BindingKind::ReadOnlyBuffer},
        {"RWStructuredBuffer", rhi::BindingKind::StorageBuffer}};
    for (const auto& [token, kind] : resources)
        if (name == token)
        {
            result.resource = kind;
            return result;
        }
    for (const auto& [prefix, scalar] : {std::pair{"float", Scalar::Float},
                                         {"uint", Scalar::Uint},
                                         {"int", Scalar::Int},
                                         {"bool", Scalar::Bool}})
    {
        if (!name.starts_with(prefix))
            continue;
        result.scalar = scalar;
        const auto suffix = name.substr(std::string(prefix).size());
        if (suffix.empty())
            return result;
        if (suffix.size() == 1 && suffix[0] >= '2' && suffix[0] <= '4')
        {
            result.rows = uint32_t(suffix[0] - '0');
            return result;
        }
        if (scalar == Scalar::Float && suffix.size() == 3 && suffix[1] == 'x' && suffix[0] >= '2' &&
            suffix[0] <= '4' && suffix[2] >= '2' && suffix[2] <= '4')
        {
            result.rows = uint32_t(suffix[0] - '0');
            result.columns = uint32_t(suffix[2] - '0');
            return result;
        }
    }
    throw std::runtime_error(source.Describe() + ": Unsupported parameter type " + name);
}
uint32_t ElementBytes(const Type& type)
{
    return type.rows * type.columns * 4;
}
} // namespace renderlab::shader
