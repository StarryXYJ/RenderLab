#pragma once

#include "shader/lexer.hpp"

#include <set>

namespace renderlab::shader::detail
{
struct DeclarationParser
{
    Definition definition;
    std::vector<std::filesystem::path> includeDirectories;
    std::set<std::filesystem::path> active, loaded;
    bool renderDeclared = false;
    Property Attributes(Lexer& lexer);
    void Values(Lexer& lexer, std::vector<double>& values, uint32_t depth = 0);
    ParameterBlock Block(Lexer& lexer, std::string name, Scope scope);
    void AddBlock(ParameterBlock block);
    void Enum(Lexer& lexer);
    StateValue StateSource(Lexer& lexer, std::string_view type, int64_t maximum = UINT32_MAX);
    void State(Lexer& lexer, StateDefinition& state, const std::string& prefix = {});
    void Render(Lexer& lexer);
    void ValidateStateSources();
    void Load(const std::filesystem::path& input, bool module);
};
} // namespace renderlab::shader::detail
