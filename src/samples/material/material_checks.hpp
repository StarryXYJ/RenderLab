#pragma once

#include "material/live_material.hpp"
#include "rhi/commands.hpp"
#include "shader/compiler.hpp"

#include <array>
#include <filesystem>
#include <span>

namespace renderlab::samples::detail
{
struct MaterialReadback
{
    std::shared_ptr<rhi::Buffer> pixels;
    std::unique_ptr<rhi::CommandList> commands;
};
void MaterialConstants(rhi::CommandList& commands, const material::Prepared& prepared);
MaterialReadback RecordMaterial(rhi::Runtime& runtime, const material::Prepared& prepared,
                                const std::shared_ptr<rhi::Buffer>& vertices);
void CheckMaterialColor(const std::shared_ptr<rhi::Buffer>& pixels, std::array<float, 4> expected);
void SaveMaterialImage(const std::filesystem::path& path,
                       const std::shared_ptr<rhi::Buffer>& pixels);
void VerifyMaterialCompute(rhi::Runtime& runtime, material::Runtime& materials,
                           std::shared_ptr<const shader::Package> package,
                           const material::Asset& asset, const material::ResourceResolver& resolve);
void VerifyMaterialReload(rhi::Runtime& runtime, material::Runtime& materials,
                          const std::filesystem::path& source, const std::filesystem::path& output,
                          shader::CompileOptions options, const material::Asset& asset,
                          const material::ResourceResolver& resolve,
                          const std::shared_ptr<rhi::Buffer>& vertices,
                          std::span<const material::Preparation> required);
} // namespace renderlab::samples::detail
