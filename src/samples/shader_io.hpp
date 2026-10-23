#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace renderlab::samples
{
std::vector<std::byte> LoadShader(const std::string& path);
} // namespace renderlab::samples
