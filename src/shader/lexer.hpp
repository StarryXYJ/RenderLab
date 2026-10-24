#pragma once

#include "shader/definition.hpp"

#include <string>
#include <string_view>

namespace renderlab::shader
{
struct Token
{
    std::string text;
    SourceLocation source;
    bool quoted = false;
};
/** @brief Small declaration lexer. HLSL function bodies remain entirely DXC-owned. */
class Lexer
{
public:
    Lexer(std::string text, std::string path);
    const Token& Peek() const
    {
        return token_;
    }
    Token Take();
    bool Accept(std::string_view value);
    Token Expect(std::string_view value);
    Token Identifier();
    std::string String();
    double Number();
    [[noreturn]] void Fail(const std::string& message) const;

private:
    void Advance();
    char Get();
    std::string text_, path_;
    size_t cursor_ = 0;
    uint32_t line_ = 1, column_ = 1;
    Token token_;
};
} // namespace renderlab::shader
