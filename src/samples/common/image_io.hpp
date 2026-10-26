#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace renderlab::samples
{
void SaveImage(const std::filesystem::path& path, std::span<const std::byte> pixels, uint32_t width,
               uint32_t height, uint32_t pitch);
}
