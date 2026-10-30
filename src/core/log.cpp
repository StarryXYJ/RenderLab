#include "core/log.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>
#include <iostream>
#include <stdexcept>
#include <utility>

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
    if (mirrorEnabled_)
    {
        try
        {
            auto length = std::min<size_t>(message.size(), 16 * 1024);
            // Never split a UTF-8 codepoint in the bounded projection.
            while (length < message.size() && length &&
                   (static_cast<unsigned char>(message[length]) & 0xC0) == 0x80)
                --length;
            while (!mirror_.empty() &&
                   (mirror_.size() >= 4096 || mirrorBytes_ + length > 8 * 1024 * 1024))
            {
                mirrorBytes_ -= mirror_.front().message.size();
                mirror_.pop_front();
                ++dropped_;
            }
            mirror_.push_back(
                {++sequence_,
                 static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::system_clock::now().time_since_epoch())
                                           .count()),
                 level, std::string(message.substr(0, length)), length < message.size()});
            mirrorBytes_ += length;
        }
        catch (...)
        {
            ++dropped_;
        }
    }
}
void Logger::EnableMirror()
{
    const std::scoped_lock lock(mutex_);
    mirrorEnabled_ = true;
}
std::vector<LogEvent> Logger::DrainMirror(uint32_t capacity, uint64_t& dropped)
{
    const std::scoped_lock lock(mutex_);
    std::vector<LogEvent> result;
    const auto count = std::min<size_t>(capacity, mirror_.size());
    result.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        mirrorBytes_ -= mirror_.front().message.size();
        result.push_back(std::move(mirror_.front()));
        mirror_.pop_front();
    }
    dropped = dropped_;
    return result;
}
} // namespace renderlab
