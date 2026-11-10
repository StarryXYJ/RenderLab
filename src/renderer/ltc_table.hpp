#pragma once

#include "renderer/environment.hpp"

#include <cstddef>
#include <span>

namespace renderlab::renderer
{
/// @brief Decode a 64x64 A16B16G16R16F legacy DDS LTC lookup table to linear RGBA32F.
/// Rejects unexpected layout, truncation and non-finite values; owns the returned pixels.
EnvironmentImage DecodeLtcTable(std::span<const std::byte> bytes);
} // namespace renderlab::renderer
