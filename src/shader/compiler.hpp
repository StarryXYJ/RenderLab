#pragma once

#include "shader/package.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace renderlab::shader
{
struct CompileOptions
{
    std::filesystem::path cacheDirectory;
    std::vector<std::filesystem::path> includeDirectories;
    std::vector<Target> targets{Target::Dxil, Target::Spirv};
    uint64_t generation = 1;
    bool useCache = true;
};
struct BuildResult
{
    std::shared_ptr<const Package> package;
    std::string diagnostics;
    explicit operator bool() const
    {
        return bool(package);
    }
};
/** @brief CPU-only synchronous build. Safe to invoke in independent background jobs. */
BuildResult CompileShader(const std::filesystem::path& source, const CompileOptions& options);
} // namespace renderlab::shader
