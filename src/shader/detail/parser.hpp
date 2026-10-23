#pragma once

#include "shader/lexer.hpp"

#include <set>

namespace renderlab::shader::detail
{
struct DeclarationParser
{
    Definition definition;
    std::set<std::filesystem::path> active, loaded;
    Property Attributes(Lexer& lexer);
    void Values(Lexer& lexer, std::vector<double>& values, uint32_t depth = 0);
    ParameterBlock Block(Lexer& lexer, std::string name, Scope scope);
    void AddBlock(ParameterBlock block);
    void Load(const std::filesystem::path& input, bool module);
};
} // namespace renderlab::shader::detail
