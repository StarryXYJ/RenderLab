#pragma once

#include "shader/package.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace renderlab::material
{
struct Value
{
    std::string type;
    uint32_t count = 1;
    std::vector<double> numbers;
    bool array = false;
};
struct ResourceReference
{
    std::string type, reference;
};
/** @brief Editable CPU values only; physical offsets, descriptors and GPU pointers are never saved.
 */
struct Asset
{
    std::string shader;
    std::string variant = "Default";
    std::map<std::string, Value> values;
    std::map<std::string, ResourceReference> resources;
};
Asset Create(const shader::MaterialTemplate& materialTemplate);
Asset Migrate(const Asset& previous, const shader::MaterialTemplate& materialTemplate);
void Set(Asset& asset, const shader::MaterialTemplate& materialTemplate, const std::string& id,
         std::span<const double> values);
void SetResource(Asset& asset, const shader::MaterialTemplate& materialTemplate,
                 const std::string& id, std::string reference);
void SelectVariant(Asset& asset, const shader::MaterialTemplate& materialTemplate,
                   const std::string& variant);
void ValidateResourceReference(const std::string& reference);
void Save(const Asset& asset, const std::filesystem::path& path);
Asset Load(const std::filesystem::path& path, const shader::MaterialTemplate& materialTemplate);
/// Logical matrices are supplied row-by-row and packed into generated column-major GPU layout.
std::vector<std::byte> Pack(const shader::BlockLayout& block,
                            const std::map<std::string, Value>& values);
} // namespace renderlab::material
