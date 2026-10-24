#pragma once

#include "shader/definition.hpp"

#include <span>

namespace renderlab::shader
{
/// Validate logical components before serialization or packing; throws invalid_argument.
void ValidateValue(const Property& property, std::span<const double> values);
} // namespace renderlab::shader
