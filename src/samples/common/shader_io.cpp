#include "samples/common/shader_io.hpp"

#include "platform/text.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace renderlab::samples
{
std::vector<std::byte> LoadShader(const std::string& path)
{
    std::ifstream file(std::filesystem::path(platform::Utf8ToWide(path)),
                       std::ios::binary | std::ios::ate);
    if (!file || file.tellg() <= 0)
        throw std::runtime_error("Cannot read shader: " + path);
    std::vector<std::byte> bytes(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file)
        throw std::runtime_error("Incomplete shader read: " + path);
    return bytes;
}
} // namespace renderlab::samples
