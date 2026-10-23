#include "shader/lexer.hpp"

#include "platform/text.hpp"

#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace renderlab::shader
{
std::string SourceLocation::Describe() const
{
    return file + ":" + std::to_string(line) + ":" + std::to_string(column);
}
std::string ToUtf8(const std::filesystem::path& path)
{
    return platform::WideToUtf8(path.wstring());
}
std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot read " + ToUtf8(path));
    std::string text{std::istreambuf_iterator<char>(file), {}};
    if (text.starts_with("\xef\xbb\xbf"))
        throw std::runtime_error("UTF-8 BOM is not allowed: " + ToUtf8(path));
    platform::Utf8ToWide(text);
    return text;
}
Lexer::Lexer(std::string text, std::string path) : text_(std::move(text)), path_(std::move(path))
{
    Advance();
}
char Lexer::Get()
{
    const char c = text_[cursor_++];
    if (c == '\n')
    {
        ++line_;
        column_ = 1;
    }
    else
        ++column_;
    return c;
}
[[noreturn]] void Lexer::Fail(const std::string& message) const
{
    throw std::runtime_error(token_.source.Describe() + ": " + message);
}
void Lexer::Advance()
{
    while (cursor_ < text_.size())
    {
        if (std::isspace(static_cast<unsigned char>(text_[cursor_])))
        {
            Get();
            continue;
        }
        if (text_.compare(cursor_, 2, "//") == 0)
        {
            while (cursor_ < text_.size() && Get() != '\n')
            {
            }
            continue;
        }
        if (text_.compare(cursor_, 2, "/*") == 0)
        {
            Get();
            Get();
            while (cursor_ < text_.size() && text_.compare(cursor_, 2, "*/") != 0)
                Get();
            if (cursor_ == text_.size())
            {
                token_.source = {path_, line_, column_};
                Fail("Unterminated comment");
            }
            Get();
            Get();
            continue;
        }
        break;
    }
    token_ = {{}, {path_, line_, column_}, false};
    if (cursor_ == text_.size())
        return;
    char c = Get();
    if (c == '"')
    {
        token_.quoted = true;
        while (cursor_ < text_.size())
        {
            c = Get();
            if (c == '"')
                return;
            if (c == '\\')
            {
                if (cursor_ == text_.size())
                    break;
                c = Get();
                if (c == 'n')
                    c = '\n';
                else if (c != '\\' && c != '"')
                    Fail("Unsupported string escape");
            }
            token_.text += c;
        }
        Fail("Unterminated string");
    }
    token_.text = c;
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
    {
        while (cursor_ < text_.size() &&
               (std::isalnum(static_cast<unsigned char>(text_[cursor_])) || text_[cursor_] == '_'))
            token_.text += Get();
    }
    else if (std::isdigit(static_cast<unsigned char>(c)) || c == '.')
    {
        while (cursor_ < text_.size())
        {
            c = text_[cursor_];
            if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == 'e' || c == 'E' ||
                ((c == '+' || c == '-') &&
                 (token_.text.back() == 'e' || token_.text.back() == 'E')))
                token_.text += Get();
            else
                break;
        }
    }
}
Token Lexer::Take()
{
    auto result = token_;
    Advance();
    return result;
}
bool Lexer::Accept(std::string_view value)
{
    if (token_.text != value || token_.quoted)
        return false;
    Take();
    return true;
}
Token Lexer::Expect(std::string_view value)
{
    if (token_.text != value || token_.quoted)
        Fail("Expected '" + std::string(value) + "'");
    return Take();
}
Token Lexer::Identifier()
{
    if (token_.quoted || token_.text.empty() ||
        !(std::isalpha(static_cast<unsigned char>(token_.text[0])) || token_.text[0] == '_'))
        Fail("Expected identifier");
    return Take();
}
std::string Lexer::String()
{
    if (!token_.quoted)
        Fail("Expected quoted string");
    return Take().text;
}
double Lexer::Number()
{
    const bool negative = Accept("-");
    auto token = Take();
    size_t end = 0;
    try
    {
        const double value = std::stod(token.text, &end);
        if (!token.quoted && end == token.text.size() && std::isfinite(value))
            return negative ? -value : value;
    }
    catch (const std::exception&)
    {
    }
    throw std::runtime_error(token.source.Describe() + ": Expected finite number");
}
} // namespace renderlab::shader
