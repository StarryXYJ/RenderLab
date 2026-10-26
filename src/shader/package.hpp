#pragma once

#include "shader/definition.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace renderlab::shader
{
struct Package;
/** @brief Selection re-resolves per package generation; Custom uses a local pass name. */
using PassSelection = std::variant<std::string, PassRole>;
struct PassHandle
{
    const Package* package = nullptr;
    uint32_t index = UINT32_MAX;
    auto operator<=>(const PassHandle&) const = default;
};
struct BindingHandle
{
    PassHandle pass;
    uint32_t index = UINT32_MAX;
    bool constant = false;
    auto operator<=>(const BindingHandle&) const = default;
};
struct FieldLayout
{
    Property property;
    uint32_t offset = 0, size = 0, arrayStride = 0, matrixStride = 0;
};
struct BlockLayout
{
    std::string name;
    Scope scope;
    uint32_t slot = 0, size = 0;
    std::vector<FieldLayout> fields;
    std::string schemaId, typeName, layoutHash;
    uint32_t schemaVersion = 0;
};
struct BlockContract
{
    std::string_view id;
    uint32_t version;
    std::string_view layoutHash;
    Scope scope;
};
struct ResourceLayout
{
    Property property;
    Scope scope;
    uint32_t slot = 0;
};
struct Interface
{
    std::vector<EnumDefinition> enums;
    std::vector<BlockLayout> blocks;
    std::vector<ResourceLayout> resources;
    std::vector<rhi::BindingDesc> Bindings() const;
    const BlockLayout* Material() const;
};
struct ReflectedField
{
    std::string name;
    uint32_t offset = 0, size = 0, rows = 1, columns = 1, count = 1;
    uint32_t arrayStride = 0, matrixStride = 0;
    Scalar scalar = Scalar::Float;
    bool columnMajor = true;
};
struct ReflectedBinding
{
    std::string name;
    uint32_t slot = 0, space = 0, count = 1;
    rhi::BindingKind kind;
    std::vector<ReflectedField> fields;
};
struct Program
{
    std::string pass, variant, entry;
    rhi::ShaderStage stage;
    Target target;
    std::vector<std::byte> code;
    std::vector<ReflectedBinding> reflection;
    VariantKey axes;
};
/** @brief Immutable compiled generation; contains CPU data only and never owns RHI objects. */
struct Package
{
    PassHandle ResolvePass(const PassSelection& selection) const;
    const Pass& GetPass(PassHandle handle) const;
    bool HasRole(PassRole role) const;
    BindingHandle BlockBinding(PassHandle pass, std::string_view localName) const;
    BindingHandle ResourceBinding(PassHandle pass, std::string_view localName) const;
    std::optional<BindingHandle> SchemaBinding(PassHandle pass,
                                               const BlockContract& contract) const;
    std::optional<BindingHandle> SemanticBinding(PassHandle pass,
                                                 Property::BindingSemantic semantic) const;
    const BlockLayout& GetBlock(BindingHandle handle) const;
    const ResourceLayout& GetResource(BindingHandle handle) const;
    uint64_t generation = 0;
    std::string key;
    Definition definition;
    std::vector<Interface> interfaces;
    std::vector<Program> programs;
    std::vector<std::filesystem::path> dependencies;
    bool cacheHit = false;
    const Interface& Layout(const std::string& pass) const;
    const Program& Find(const std::string& pass, const VariantKey& key, rhi::ShaderStage stage,
                        Target target) const;
};
/// Non-owning read-only projection; retains its package to keep schema pointers valid.
class MaterialTemplate
{
public:
    explicit MaterialTemplate(std::shared_ptr<const Package> package);
    const std::vector<FieldLayout>& Fields() const;
    /// @brief Logical material properties, including static axes without GPU offsets.
    const std::vector<Property>& Properties() const;
    std::vector<ResourceLayout> Resources() const;
    const Package& GetPackage() const
    {
        return *package_;
    }

private:
    std::shared_ptr<const Package> package_;
};
Interface BuildInterface(const Definition& definition, const Pass& pass);
std::string GenerateHeader(const Interface& interface, Target target);
std::string GenerateTypesHeader(const Interface& interface);
std::string GenerateCppHeader(const Package& package);
void ValidateReflection(const Interface& interface, const std::vector<ReflectedBinding>& bindings);
std::string SchemaToJson(const Package& package);
} // namespace renderlab::shader
