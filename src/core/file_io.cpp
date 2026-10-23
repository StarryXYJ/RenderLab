#include "core/file_io.hpp"

#include <Windows.h>

#include <atomic>
#include <fstream>
#include <stdexcept>
#include <string>

namespace renderlab
{
void WriteFileAtomically(const std::filesystem::path& path, std::span<const std::byte> bytes)
{
    static std::atomic<uint64_t> sequence{0};
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += ".tmp." + std::to_string(GetCurrentProcessId()) + "." + std::to_string(++sequence);
    try
    {
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()));
            file.flush();
            if (!file)
                throw std::runtime_error("Cannot write temporary file");
            file.close();
            if (!file)
                throw std::runtime_error("Cannot close temporary file");
        }
        if (!MoveFileExW(temporary.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace destination file");
    }
    catch (...)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}
} // namespace renderlab
