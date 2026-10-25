#include "shader/detail/parser.hpp"
#include "platform/text.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace renderlab::shader
{
namespace
{
Scope ParseScope(const std::string& text, const SourceLocation& source)
{
    if (text == "view" || text == "frame")
        return Scope::View;
    if (text == "draw")
        return Scope::Draw;
    if (text == "pass")
        return Scope::Pass;
    throw std::runtime_error(source.Describe() +
                             ": Explicit cbuffer scope must be view/frame/draw/pass");
}
} // namespace
void detail::DeclarationParser::AddBlock(ParameterBlock block)
{
    if (std::ranges::any_of(definition.blocks, [&](const auto& b) { return b.name == block.name; }))
        throw std::runtime_error(block.source.Describe() + ": Duplicate parameter block " +
                                 block.name);
    definition.blocks.push_back(std::move(block));
}
void detail::DeclarationParser::Load(const std::filesystem::path& input, bool module)
{
    auto path = std::filesystem::weakly_canonical(input);
    if (active.contains(path))
        throw std::runtime_error(ToUtf8(path) + ": Shared declaration cycle");
    if (loaded.contains(path))
        return;
    if (loaded.size() + active.size() >= 128)
        throw std::runtime_error("Too many shared declaration modules");
    auto text = ReadText(path);
    definition.declarations.push_back({path, text});
    active.insert(path);
    Lexer lexer(std::move(text), ToUtf8(path));
    if (!module)
    {
        lexer.Expect("shader");
        definition.name = lexer.String();
        lexer.Expect("{");
    }
    while (!lexer.Peek().text.empty() && (module || lexer.Peek().text != "}"))
    {
        if (lexer.Accept("use"))
        {
            auto include = lexer.String();
            lexer.Expect(";");
            auto modulePath = path.parent_path() / platform::Utf8ToWide(include);
            if (!std::filesystem::is_regular_file(modulePath))
                for (const auto& directory : includeDirectories)
                {
                    const auto candidate = directory / platform::Utf8ToWide(include);
                    if (std::filesystem::is_regular_file(candidate))
                    {
                        modulePath = candidate;
                        break;
                    }
                }
            Load(modulePath, true);
        }
        else if (lexer.Accept("material"))
        {
            if (module)
                lexer.Fail("Shared modules only declare cbuffers");
            AddBlock(Block(lexer, "Material", Scope::Material));
        }
        else if (lexer.Peek().text == "[" || lexer.Peek().text == "cbuffer")
        {
            Scope scope = Scope::Pass;
            if (lexer.Accept("["))
            {
                lexer.Expect("scope");
                lexer.Expect("(");
                auto t = lexer.Identifier();
                scope = ParseScope(t.text, t.source);
                lexer.Expect(")");
                lexer.Expect("]");
            }
            lexer.Expect("cbuffer");
            auto name = lexer.Identifier().text;
            auto block = Block(lexer, name, scope);
            if (std::ranges::any_of(block.properties,
                                    [](const auto& p) { return !p.type.Numeric(); }))
                lexer.Fail("cbuffer members must be numeric");
            AddBlock(std::move(block));
        }
        else if (!module && lexer.Accept("variant"))
        {
            Variant variant;
            variant.name = lexer.Identifier().text;
            lexer.Expect("{");
            while (!lexer.Accept("}"))
            {
                auto key = lexer.Identifier().text;
                lexer.Expect("=");
                std::string value;
                if (lexer.Peek().text == "-" ||
                    (!lexer.Peek().text.empty() &&
                     std::isdigit(static_cast<unsigned char>(lexer.Peek().text.front()))))
                {
                    const bool negative = lexer.Accept("-");
                    value = (negative ? "-" : "") + lexer.Take().text;
                }
                else
                    value = lexer.Identifier().text;
                lexer.Expect(";");
                if (!variant.defines.emplace(key, value).second)
                    lexer.Fail("Duplicate variant define");
            }
            if (definition.variants.size() >= 16 ||
                std::ranges::any_of(definition.variants,
                                    [&](auto& v) { return v.name == variant.name; }))
                lexer.Fail("Duplicate variant or variant limit (16) exceeded");
            definition.variants.push_back(std::move(variant));
        }
        else if (!module && lexer.Accept("pass"))
        {
            Pass pass;
            auto token = lexer.Identifier();
            pass.name = token.text;
            pass.source = token.source;
            if (definition.passes.size() >= 16 ||
                std::ranges::any_of(definition.passes,
                                    [&](auto& p) { return p.name == pass.name; }))
                lexer.Fail("Duplicate pass or pass limit exceeded");
            lexer.Expect("{");
            while (!lexer.Accept("}"))
            {
                auto key = lexer.Identifier().text;
                if (key == "parameters")
                {
                    pass.blocks.push_back(Block(lexer, pass.name + "Parameters", Scope::Pass));
                    continue;
                }
                lexer.Expect("=");
                if (key == "vertex" || key == "pixel" || key == "compute")
                {
                    auto entry = lexer.String();
                    auto colon = entry.rfind(':');
                    if (colon == std::string::npos || colon + 1 == entry.size())
                        lexer.Fail("Entry must be file.hlsl:Function");
                    const auto stage = key == "vertex"  ? rhi::ShaderStage::Vertex
                                       : key == "pixel" ? rhi::ShaderStage::Pixel
                                                        : rhi::ShaderStage::Compute;
                    if (std::ranges::any_of(pass.entries,
                                            [&](auto& e) { return e.stage == stage; }))
                        lexer.Fail("Duplicate stage entry");
                    pass.entries.push_back(
                        {stage, path.parent_path() / platform::Utf8ToWide(entry.substr(0, colon)),
                         entry.substr(colon + 1)});
                }
                else
                {
                    const bool value = lexer.Accept("true");
                    if (!value)
                        lexer.Expect("false");
                    if (key == "depth")
                        pass.depth = value;
                    else if (key == "blend")
                        pass.blend = value;
                    else if (key == "cull")
                        pass.cull = value;
                    else
                        lexer.Fail("Unknown pass option " + key);
                }
                lexer.Expect(";");
            }
            const auto computes = std::ranges::count_if(
                pass.entries, [](auto& e) { return e.stage == rhi::ShaderStage::Compute; });
            if (!((computes == 1 && pass.entries.size() == 1) ||
                  (computes == 0 && pass.entries.size() == 2)))
                lexer.Fail("Pass needs compute or vertex+pixel entries");
            definition.passes.push_back(std::move(pass));
        }
        else
            lexer.Fail("Unknown declaration " + lexer.Peek().text);
    }
    if (!module)
        lexer.Expect("}");
    if (!lexer.Peek().text.empty())
        lexer.Fail("Unexpected trailing tokens");
    active.erase(path);
    loaded.insert(path);
}
Definition ParseDefinition(const std::filesystem::path& path,
                           const std::vector<std::filesystem::path>& includeDirectories)
{
    detail::DeclarationParser parser;
    parser.includeDirectories = includeDirectories;
    parser.definition.source = std::filesystem::weakly_canonical(path);
    parser.Load(path, false);
    if (parser.definition.passes.empty())
        throw std::runtime_error(ToUtf8(path) + ": Shader needs at least one pass");
    if (parser.definition.variants.empty())
        parser.definition.variants.push_back({"Default", {}});
    return std::move(parser.definition);
}
} // namespace renderlab::shader
