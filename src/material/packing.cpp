#include "material/asset.hpp"
#include "shader/value.hpp"

#include <cstring>
#include <stdexcept>

namespace renderlab::material
{
std::vector<std::byte> Pack(const shader::BlockLayout& block,
                            const std::map<std::string, Value>& values)
{
    std::vector<std::byte> bytes((block.size + 255u) & ~255u);
    for (const auto& field : block.fields)
    {
        const auto found = values.find(field.property.id);
        const auto& source =
            found == values.end() ? field.property.defaults : found->second.numbers;
        if (found != values.end() && (found->second.type != field.property.type.name ||
                                      found->second.count != field.property.type.count ||
                                      found->second.array != field.property.type.array))
            throw std::invalid_argument("Packed value type mismatch: " + field.property.id);
        shader::ValidateValue(field.property, source);
        const auto& type = field.property.type;
        for (uint32_t element = 0; element < type.count; ++element)
            for (uint32_t row = 0; row < type.rows; ++row)
                for (uint32_t column = 0; column < type.columns; ++column)
                {
                    const auto value = source[size_t(element) * type.rows * type.columns +
                                              row * type.columns + column];
                    const uint32_t offset =
                        field.offset + element * field.arrayStride +
                        (type.columns > 1 ? column * field.matrixStride + row * 4 : row * 4);
                    if (type.scalar == shader::Scalar::Float)
                    {
                        const auto v = static_cast<float>(value);
                        std::memcpy(bytes.data() + offset, &v, 4);
                    }
                    else if (type.scalar == shader::Scalar::Int)
                    {
                        const auto v = static_cast<int32_t>(value);
                        std::memcpy(bytes.data() + offset, &v, 4);
                    }
                    else
                    {
                        const auto v = static_cast<uint32_t>(value);
                        std::memcpy(bytes.data() + offset, &v, 4);
                    }
                }
    }
    return bytes;
}
} // namespace renderlab::material
