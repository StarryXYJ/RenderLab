#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace renderlab
{
enum class LogLevel
{
    Info,
    Warning,
    Error
};
struct LogEvent
{
    uint64_t sequence = 0, unixMilliseconds = 0;
    LogLevel level = LogLevel::Info;
    std::string message;
    bool truncated = false;
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
    /// Borrow an output stream; its lifetime must exceed the logger, including callbacks.
    explicit Logger(std::ostream& output) : output_(&output) {}
    /// Flush each message. Strict hosts propagate I/O failures; editor mirrors keep recording.
    void Write(LogLevel level, std::string_view message);
    /// Optional bounded editor mirror; never calls a managed callback from the producer thread.
    void EnableMirror();
    std::vector<LogEvent> DrainMirror(uint32_t capacity, uint64_t& dropped);
    bool FileFailed() const;

private:
    mutable std::mutex mutex_;
    std::ofstream file_;
    std::ostream* output_ = &file_;
    bool fileFailed_ = false;
    bool mirrorEnabled_ = false;
    uint64_t sequence_ = 0, dropped_ = 0;
    size_t mirrorBytes_ = 0;
    std::deque<LogEvent> mirror_;
};
} // namespace renderlab
