#pragma once

#include "shader/package.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace renderlab::shader
{
struct ProgramRequest
{
    Entry entry;
    Target target;
    std::map<std::string, std::string> defines;
    std::vector<std::filesystem::path> includeDirectories;
    std::string generated;
    std::string sourceSnapshot;
    std::string generatedTypes;
    /// Optional DXC profile override for features introduced after the default stage target.
    std::string targetProfile;
};
struct Preprocessed
{
    std::string text;
    std::vector<Dependency> dependencies;
};
class DxcCompiler final
{
public:
    DxcCompiler();
    ~DxcCompiler();
    DxcCompiler(const DxcCompiler&) = delete;
    DxcCompiler& operator=(const DxcCompiler&) = delete;
    std::string Version() const;
    Preprocessed Preprocess(const ProgramRequest& request);
    std::vector<std::byte> Compile(const ProgramRequest& request);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::vector<ReflectedBinding> ReflectDxil(const std::vector<std::byte>& code);
std::vector<ReflectedBinding> ReflectSpirv(const std::vector<std::byte>& code);
} // namespace renderlab::shader
