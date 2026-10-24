#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>

namespace renderlab
{
enum class LogLevel
{
    Info,
    Warning,
    Error
};

/**
 * @brief Serializes UTF-8 messages to the terminal and an LF-delimited file with UTC timestamps.
 *
 * Write is thread-safe. Keep this object alive until all backend diagnostic callbacks have stopped.
 */
class Logger final
{
public:
    /// Create parent directories and truncate the destination; opening failures propagate.
    explicit Logger(const std::filesystem::path& path);
    /// Flush each message to the file; write failures propagate to the caller.
    void Write(LogLevel level, std::string_view message);

private:
    std::mutex mutex_;
    std::ofstream file_;
};
} // namespace renderlab
