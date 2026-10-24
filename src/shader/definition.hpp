#pragma once

#include "rhi/resources.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace renderlab::shader
{
enum class Target
{
    Dxil,
    Spirv
};
enum class Scope : uint32_t
{
    View = 0,
    Material = 1,
    Draw = 2,
    Pass = 3
};
enum class Scalar
{
    Float,
    Int,
    Uint,
    Bool
};
struct SourceLocation
{
    std::string file;
    uint32_t line = 1, column = 1;
    std::string Describe() const;
};
/** @brief Logical type. Matrices are column-major; booleans use 32-bit GPU words. */
struct Type
{
    std::string name;
    Scalar scalar = Scalar::Float;
    uint32_t rows = 1, columns = 1, count = 1;
    bool array = false;
    std::optional<rhi::BindingKind> resource;
    std::string element = "float4";
    bool operator==(const Type&) const = default;
    bool Numeric() const
    {
        return !resource;
    }
};
struct Property
{
    std::string id, symbol;
    Type type;
    std::vector<double> defaults;
    std::string resourceDefault;
    std::string label, group, tooltip, unit, format;
    bool color = false, srgb = false, hdr = false;
    std::optional<double> minimum, maximum;
    std::vector<std::string> enumeration;
    SourceLocation source;
};
struct ParameterBlock
{
    std::string name;
    Scope scope = Scope::Pass;
    std::vector<Property> properties;
    SourceLocation source;
};
struct Entry
{
    rhi::ShaderStage stage;
    std::filesystem::path source;
    std::string function;
};
struct Pass
{
    std::string name;
    std::vector<Entry> entries;
    std::vector<ParameterBlock> blocks;
    bool depth = true, blend = false, cull = true;
    SourceLocation source;
};
struct Variant
{
    std::string name;
    std::map<std::string, std::string> defines;
};
struct Dependency
{
    std::filesystem::path path;
    std::string content;
};
struct Definition
{
    std::string name;
    std::filesystem::path source;
    std::vector<ParameterBlock> blocks;
    std::vector<Pass> passes;
    std::vector<Variant> variants;
    std::vector<Dependency> declarations;
};

/// Parse the sole author format, resolving shared modules relative to the including declaration.
/// Throws a source-positioned error for malformed declarations, cycles or conflicting identities.
Definition ParseDefinition(const std::filesystem::path& path);
Type ParseType(const std::string& name, const SourceLocation& source);
uint32_t ElementBytes(const Type& type);
std::string ReadText(const std::filesystem::path& path);
std::string ToUtf8(const std::filesystem::path& path);
} // namespace renderlab::shader
