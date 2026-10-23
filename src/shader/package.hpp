#pragma once

#include "shader/definition.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace renderlab::shader
{
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
};
struct ResourceLayout
{
    Property property;
    Scope scope;
    uint32_t slot = 0;
};
struct Interface
{
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
};
/** @brief Immutable compiled generation; contains CPU data only and never owns RHI objects. */
struct Package
{
    uint64_t generation = 0;
    std::string key;
    Definition definition;
    std::vector<Interface> interfaces;
    std::vector<Program> programs;
    std::vector<std::filesystem::path> dependencies;
    bool cacheHit = false;
    const Interface& Layout(const std::string& pass) const;
    const Program& Find(const std::string& pass, const std::string& variant, rhi::ShaderStage stage,
                        Target target) const;
};
/// Non-owning read-only projection; retains its package to keep schema pointers valid.
class MaterialTemplate
{
public:
    explicit MaterialTemplate(std::shared_ptr<const Package> package);
    const std::vector<FieldLayout>& Fields() const;
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
void ValidateReflection(const Interface& interface, const std::vector<ReflectedBinding>& bindings);
std::string SchemaToJson(const Package& package);
} // namespace renderlab::shader
