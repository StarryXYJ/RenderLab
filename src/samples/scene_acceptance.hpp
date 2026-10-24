#pragma once

#include "shader/definition.hpp"
#include <filesystem>
namespace renderlab
{
class Logger;
namespace rhi
{
class Runtime;
}
} // namespace renderlab
namespace renderlab::samples
{
void VerifyScene(rhi::Runtime&, shader::Target, const std::filesystem::path& project,
                 const std::filesystem::path& engine, const std::filesystem::path& output, Logger&,
                 bool validate);
}
