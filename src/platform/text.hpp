#pragma once

#include <string>
#include <string_view>

namespace renderlab::platform
{
std::string WideToUtf8(std::wstring_view text);
std::wstring Utf8ToWide(std::string_view text);
void ConfigureConsoleUtf8();
} // namespace renderlab::platform
