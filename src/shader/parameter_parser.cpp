#include "shader/detail/parser.hpp"
#include "shader/value.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace renderlab::shader::detail
{
Property DeclarationParser::Attributes(Lexer& lexer)
{
    Property property;
    property.source = lexer.Peek().source;
    std::set<std::string> attributes;
    while (lexer.Accept("["))
    {
        do
        {
            const auto attribute = lexer.Identifier().text;
            if (!attributes.insert(attribute).second)
                lexer.Fail("Duplicate property attribute " + attribute);
            if (attribute == "color")
                property.color = true;
            else if (attribute == "srgb")
                property.srgb = true;
            else if (attribute == "hdr")
                property.hdr = true;
            else if (attribute == "range")
            {
                lexer.Expect("(");
                property.minimum = lexer.Number();
                lexer.Expect(",");
                property.maximum = lexer.Number();
                lexer.Expect(")");
            }
            else if (attribute == "enum")
            {
                lexer.Expect("(");
                do
                {
                    property.enumeration.push_back(lexer.String());
                } while (lexer.Accept(","));
                lexer.Expect(")");
            }
            else
            {
                lexer.Expect("(");
                std::string value = lexer.Peek().quoted ? lexer.String() : lexer.Identifier().text;
                lexer.Expect(")");
                if (attribute == "semantic")
                {
                    const auto types = MaterialSemantics();
                    const auto semantic =
                        std::ranges::find(types, value, &MaterialSemanticInfo::name);
                    if (semantic == types.end())
                        lexer.Fail("Unknown material semantic " + value);
                    property.semantic = semantic->semantic;
                }
                else if (attribute == "binding")
                {
                    if (value == "EnvironmentAtlas")
                        property.binding = Property::BindingSemantic::EnvironmentAtlas;
                    else if (value == "BrdfLut")
                        property.binding = Property::BindingSemantic::BrdfLut;
                    else if (value == "MainLightShadow")
                        property.binding = Property::BindingSemantic::MainLightShadow;
                    else
                        lexer.Fail("Unknown resource binding semantic " + value);
                }
                else if (attribute == "keyword")
                    property.keyword = value;
                else if (attribute == "id")
                    property.id = value;
                else if (attribute == "label")
                    property.label = value;
                else if (attribute == "group")
                    property.group = value;
                else if (attribute == "tooltip")
                    property.tooltip = value;
                else if (attribute == "unit")
                    property.unit = value;
                else if (attribute == "format")
                    property.format = value;
                else
                    lexer.Fail("Unknown property attribute " + attribute);
            }
        } while (lexer.Accept(","));
        lexer.Expect("]");
    }
    return property;
}
void DeclarationParser::Values(Lexer& lexer, std::vector<double>& values, uint32_t depth)
{
    if (depth > 32)
        lexer.Fail("Initializer nesting limit exceeded");
    if (lexer.Peek().text == "(" || lexer.Peek().text == "[")
    {
        const auto opening = lexer.Take().text;
        const auto closing = opening == "(" ? ")" : "]";
        if (lexer.Peek().text == closing)
            lexer.Fail("Empty parameter initializer");
        do
        {
            Values(lexer, values, depth + 1);
        } while (lexer.Accept(","));
        lexer.Expect(closing);
    }
    else if (lexer.Accept("true"))
        values.push_back(1);
    else if (lexer.Accept("false"))
        values.push_back(0);
    else
        values.push_back(lexer.Number());
}
ParameterBlock DeclarationParser::Block(Lexer& lexer, std::string name, Scope scope)
{
    ParameterBlock block{std::move(name), scope, {}, lexer.Peek().source};
    lexer.Expect("{");
    std::set<std::string> ids, symbols;
    std::set<MaterialSemantic> semantics;
    while (!lexer.Accept("}"))
    {
        auto property = Attributes(lexer);
        auto type = lexer.Identifier();
        const auto enumeration =
            std::ranges::find(definition.enums, type.text, &EnumDefinition::name);
        property.type =
            ParseType(enumeration == definition.enums.end() ? type.text : "uint", type.source);
        if (enumeration != definition.enums.end())
        {
            property.type.name = type.text;
            property.type.enumType = true;
            property.enumItems = enumeration->items;
        }
        if (lexer.Accept("<"))
        {
            if (!property.type.resource || property.type.resource == rhi::BindingKind::Sampler)
                lexer.Fail("Only texture/buffer resources have element types");
            property.type.element = lexer.Identifier().text;
            lexer.Expect(">");
            auto element = ParseType(property.type.element, type.source);
            if (!element.Numeric() || element.columns != 1 || element.scalar == Scalar::Bool ||
                element.rows == 3)
                lexer.Fail(
                    "Resource element requires numeric scalar/2/4-vector (portable alignment)");
        }
        if (property.type.resource &&
            (*property.type.resource == rhi::BindingKind::SampledTexture ||
             *property.type.resource == rhi::BindingKind::StorageTexture) &&
            property.type.element != "float4")
            lexer.Fail("M2 texture interfaces require float4 elements");
        property.symbol = lexer.Identifier().text;
        if (property.id.empty())
            property.id = property.symbol;
        if (!ids.insert(property.id).second || !symbols.insert(property.symbol).second)
            lexer.Fail("Duplicate property ID or symbol");
        if (lexer.Accept("["))
        {
            const double size = lexer.Number();
            lexer.Expect("]");
            if (size < 1 || size > 256 || size != std::floor(size) || !property.type.Numeric())
                lexer.Fail("Only numeric arrays of 1..256 elements are supported");
            property.type.count = static_cast<uint32_t>(size);
            property.type.array = true;
        }
        if (lexer.Accept("="))
        {
            if (property.type.enumType)
            {
                auto itemName = lexer.Identifier().text;
                const auto item = std::ranges::find(property.enumItems, itemName, &EnumItem::name);
                if (item == property.enumItems.end())
                    lexer.Fail("Unknown enum initializer " + itemName);
                property.defaults.push_back(item->value);
            }
            else if (property.type.Numeric())
                Values(lexer, property.defaults);
            else
                property.resourceDefault =
                    lexer.Peek().quoted ? lexer.String() : lexer.Identifier().text;
        }
        lexer.Expect(";");
        if (property.type.enumType && property.defaults.empty())
            property.defaults.push_back(property.enumItems.front().value);
        if (property.type.Numeric())
        {
            const size_t count =
                size_t(property.type.rows) * property.type.columns * property.type.count;
            if (property.defaults.empty())
                property.defaults.resize(count);
            if (property.defaults.size() == 1 && count > 1)
                property.defaults.resize(count, property.defaults.front());
            if (property.defaults.size() != count)
                lexer.Fail("Initializer component count mismatch");
            if (property.minimum && (!property.maximum || *property.minimum > *property.maximum))
                lexer.Fail("Invalid property range");
            ValidateValue(property, property.defaults);
        }
        if (!property.type.Numeric() && (property.minimum || !property.enumeration.empty()))
            lexer.Fail("range/enum requires numeric properties");
        if (property.type.resource == rhi::BindingKind::StorageTexture &&
            property.format != "rgba8_unorm")
            lexer.Fail("Storage texture requires [format(rgba8_unorm)]");
        if (property.srgb && property.type.resource != rhi::BindingKind::SampledTexture)
            lexer.Fail("srgb applies only to sampled textures");
        if (property.color && (!property.type.Numeric() || property.type.scalar != Scalar::Float ||
                               property.type.columns != 1 || property.type.rows < 3))
            lexer.Fail("color requires float3/float4");
        if (scope == Scope::Material &&
            (property.type.resource == rhi::BindingKind::StorageTexture ||
             property.type.resource == rhi::BindingKind::StorageBuffer))
            lexer.Fail("Writable outputs belong to pass parameters, not saved materials");
        if (!property.keyword.empty())
        {
            const auto& macro = property.keyword;
            const bool identifier =
                (std::isalpha(static_cast<unsigned char>(macro.front())) || macro.front() == '_') &&
                std::ranges::all_of(macro,
                                    [](unsigned char c) { return std::isalnum(c) || c == '_'; });
            if (!identifier || scope != Scope::Material || property.type.array ||
                property.type.rows != 1 || property.type.columns != 1 ||
                (!property.type.enumType && property.type.scalar != Scalar::Bool))
                lexer.Fail(
                    "keyword requires a material bool/enum scalar and a valid macro identifier");
        }
        if (property.binding != Property::BindingSemantic::None &&
            (scope == Scope::Material ||
             property.type.resource != rhi::BindingKind::SampledTexture))
            lexer.Fail("Standard binding semantics require external sampled textures");
        if (property.semantic != MaterialSemantic::None)
        {
            const auto types = MaterialSemantics();
            const auto semantic =
                std::ranges::find(types, property.semantic, &MaterialSemanticInfo::semantic);
            if (scope != Scope::Material || property.type.name != semantic->type ||
                property.type.array || !semantics.insert(property.semantic).second)
                lexer.Fail("Material semantic requires its exact type, material scope and a unique "
                           "mapping");
        }
        block.properties.push_back(std::move(property));
    }
    lexer.Accept(";");
    return block;
}
} // namespace renderlab::shader::detail
