#include "platform/text.hpp"

#include <Windows.h>
#include <limits>
#include <stdexcept>

namespace renderlab::platform
{
std::string WideToUtf8(std::wstring_view text)
{
    if (text.empty())
    {
        return {};
    }
    if (text.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        throw std::runtime_error("Wide string exceeds Win32 length limit");
    }
    const auto length = static_cast<int>(text.size());
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length,
                                          nullptr, 0, nullptr, nullptr);
    if (size == 0)
    {
        throw std::runtime_error("Invalid UTF-16 string");
    }
    std::string result(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length, result.data(), size,
                            nullptr, nullptr) != size)
    {
        throw std::runtime_error("UTF-16 to UTF-8 conversion failed");
    }
    return result;
}

std::wstring Utf8ToWide(std::string_view text)
{
    if (text.empty())
    {
        return {};
    }
    if (text.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        throw std::runtime_error("UTF-8 string exceeds Win32 length limit");
    }
    const auto length = static_cast<int>(text.size());
    const auto size =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, nullptr, 0);
    if (size == 0)
    {
        throw std::runtime_error("Invalid UTF-8 string");
    }
    std::wstring result(static_cast<size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, result.data(),
                            size) != size)
    {
        throw std::runtime_error("UTF-8 to UTF-16 conversion failed");
    }
    return result;
}

void ConfigureConsoleUtf8()
{
    SetConsoleOutputCP(CP_UTF8);
}
} // namespace renderlab::platform
