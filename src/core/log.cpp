#include "core/log.hpp"

#include <chrono>
#include <ctime>
#include <format>
#include <iostream>
#include <stdexcept>

namespace renderlab
{
Logger::Logger(const std::filesystem::path& path)
{
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path());
    }
    file_.open(path, std::ios::binary | std::ios::trunc);
    if (!file_)
    {
        throw std::runtime_error("Cannot open log file");
    }
}

void Logger::Write(LogLevel level, std::string_view message)
{
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
    gmtime_s(&utc, &now);
    const auto* label = level == LogLevel::Error     ? "ERROR"
                        : level == LogLevel::Warning ? "WARN"
                                                     : "INFO";
    const auto line = std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z [{}] {}\n",
                                  utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour,
                                  utc.tm_min, utc.tm_sec, label, message);
    const std::scoped_lock lock(mutex_);
    std::clog << line;
    file_ << line;
    file_.flush();
    if (!file_)
    {
        throw std::runtime_error("Writing log file failed");
    }
}
} // namespace renderlab
