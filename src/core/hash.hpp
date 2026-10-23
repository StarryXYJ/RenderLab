#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace renderlab
{
std::string Sha256(std::span<const std::byte> bytes);
std::string Sha256(std::string_view text);
} // namespace renderlab
