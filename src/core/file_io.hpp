#pragma once

#include <cstddef>
#include <filesystem>
#include <span>

namespace renderlab
{
/// Write a same-directory temporary file, then replace destination; throws on failure.
void WriteFileAtomically(const std::filesystem::path& path, std::span<const std::byte> bytes);
} // namespace renderlab
