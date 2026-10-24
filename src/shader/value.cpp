#include "shader/value.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace renderlab::shader
{
void ValidateValue(const Property& property, std::span<const double> values)
{
    const auto& type = property.type;
    const auto fail = [&](const char* reason)
    {
        throw std::invalid_argument(property.source.Describe() + ": " + reason + std::string(": ") +
                                    property.id);
    };
    if (!type.Numeric() || values.size() != size_t(type.rows) * type.columns * type.count)
        fail("Parameter component count mismatch");
    if (!property.enumeration.empty() &&
        (type.scalar != Scalar::Int && type.scalar != Scalar::Uint))
        fail("enum requires int/uint");
    for (const auto value : values)
    {
        if (!std::isfinite(value) || (property.minimum && value < *property.minimum) ||
            (property.maximum && value > *property.maximum))
            fail("Parameter outside range");
        if (type.scalar == Scalar::Float && std::abs(value) > std::numeric_limits<float>::max())
            fail("Parameter outside float32 range");
        if (type.scalar != Scalar::Float && value != std::floor(value))
            fail("Integer parameter requires integral value");
        if (type.scalar == Scalar::Int && (value < INT32_MIN || value > INT32_MAX))
            fail("int parameter outside range");
        if (type.scalar == Scalar::Uint && (value < 0 || value > UINT32_MAX))
            fail("uint parameter outside range");
        if (type.scalar == Scalar::Bool && value != 0 && value != 1)
            fail("bool parameter requires 0/1");
        if (!property.enumeration.empty() && (value < 0 || value >= property.enumeration.size()))
            fail("enum index outside range");
    }
}
} // namespace renderlab::shader
