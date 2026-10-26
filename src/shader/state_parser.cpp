#include "shader/detail/parser.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace renderlab::shader::detail
{
StateValue DeclarationParser::StateSource(Lexer& lexer, std::string_view type, int64_t maximum)
{
    StateValue source;
    if (lexer.Accept("$"))
        source.property = lexer.Identifier().text;
    else if (type == "bool")
    {
        source.literal = lexer.Accept("true") ? 1 : 0;
        if (!source.literal)
            lexer.Expect("false");
    }
    else if (type == "uint" || type == "int")
    {
        const auto value = lexer.Number();
        const auto minimum = type == "int" ? -maximum : 0;
        if (value < minimum || value > maximum || value != std::floor(value))
            lexer.Fail("State literal outside range");
        source.literal = static_cast<int64_t>(value);
    }
    else
    {
        const auto enumeration = std::ranges::find(definition.enums, type, &EnumDefinition::name);
        if (enumeration == definition.enums.end())
            lexer.Fail("Unknown state value type");
        const auto name = lexer.Identifier().text;
        const auto item = std::ranges::find(enumeration->items, name, &EnumItem::name);
        if (item == enumeration->items.end())
            lexer.Fail("Unknown state enum value " + name);
        source.literal = item->value;
    }
    return source;
}
void DeclarationParser::State(Lexer& lexer, StateDefinition& state, const std::string& prefix)
{
    lexer.Expect("{");
    std::set<std::string> keys;
    const auto fields = StateFields();
    while (!lexer.Accept("}"))
    {
        const auto key = lexer.Identifier().text;
        if (!keys.insert(key).second)
            lexer.Fail("Duplicate state field " + key);
        const auto path = prefix.empty() ? key : prefix + "." + key;
        if (lexer.Peek().text == "{")
        {
            if (path != "depth" && path != "blend" && path != "stencil" &&
                path != "stencil.front" && path != "stencil.back")
                lexer.Fail("Unknown state group " + path);
            State(lexer, state, path);
            continue;
        }
        lexer.Expect("=");
        if (path == "preset")
        {
            const auto preset = lexer.Identifier().text;
            if (preset == "Explicit")
                state.preset = StatePreset::Explicit;
            else if (preset == "SurfaceForward")
                state.preset = StatePreset::SurfaceForward;
            else if (preset == "ShadowCaster")
                state.preset = StatePreset::ShadowCaster;
            else
                lexer.Fail("Unknown fixed state preset " + preset);
        }
        else
        {
            const bool commonFace = prefix == "stencil" && (key == "compare" || key == "fail" ||
                                                            key == "depthFail" || key == "pass");
            const auto firstPath = commonFace ? "stencil.front." + key : path;
            const auto info = std::ranges::find(fields, firstPath, &StateFieldInfo::path);
            if (info == fields.end())
                lexer.Fail("Unknown state field " + path);
            const auto value = StateSource(lexer, info->type, info->maximum);
            const auto append = [&](uint32_t index, bool specific)
            {
                const auto found = std::ranges::find(state.overrides, index, &StateOverride::field);
                if (found == state.overrides.end())
                    state.overrides.push_back({index, value, specific});
                else if (specific || !found->faceSpecific)
                    *found = {index, value, specific};
            };
            append(static_cast<uint32_t>(info - fields.begin()), !commonFace);
            if (commonFace)
            {
                const auto back =
                    std::ranges::find(fields, "stencil.back." + key, &StateFieldInfo::path);
                append(static_cast<uint32_t>(back - fields.begin()), false);
            }
        }
        lexer.Expect(";");
    }
    lexer.Accept(";");
}
void DeclarationParser::Render(Lexer& lexer)
{
    if (renderDeclared)
        lexer.Fail("Duplicate render policy");
    renderDeclared = true;
    lexer.Expect("{");
    std::set<std::string> keys;
    while (!lexer.Accept("}"))
    {
        auto key = lexer.Identifier().text;
        if (!keys.insert(key).second)
            lexer.Fail("Duplicate render policy field");
        lexer.Expect("=");
        if (key == "surface")
            definition.render.surface = StateSource(lexer, "SurfaceType");
        else if (key == "blend")
            definition.render.blend = StateSource(lexer, "BlendMode");
        else if (key == "alphaClip")
            definition.render.alphaClip = StateSource(lexer, "bool");
        else if (key == "queueOffset")
            definition.render.queueOffset = StateSource(lexer, "int", 99);
        else
            lexer.Fail("Unknown render policy field " + key);
        lexer.Expect(";");
    }
    lexer.Accept(";");
}
void DeclarationParser::ValidateStateSources()
{
    const auto resolve = [&](StateValue& source, std::string_view type)
    {
        if (source.property.empty())
            return;
        for (const auto& block : definition.blocks)
            if (block.scope == Scope::Material)
                for (const auto& property : block.properties)
                    if (property.symbol == source.property)
                    {
                        if (!property.type.Numeric() || property.type.array ||
                            property.type.rows != 1 || property.type.columns != 1 ||
                            property.type.name != type)
                            throw std::invalid_argument(
                                property.source.Describe() +
                                ": State property type mismatch: " + source.property);
                        source.property = property.id;
                        return;
                    }
        throw std::invalid_argument("Unknown state property " + source.property);
    };
    resolve(definition.render.surface, "SurfaceType");
    resolve(definition.render.blend, "BlendMode");
    resolve(definition.render.alphaClip, "bool");
    resolve(definition.render.queueOffset, "int");
    for (auto& pass : definition.passes)
    {
        resolve(pass.castShadows, "bool");
        for (auto& field : pass.state.overrides)
            resolve(field.value, StateFields()[field.field].type);
    }
}
} // namespace renderlab::shader::detail
