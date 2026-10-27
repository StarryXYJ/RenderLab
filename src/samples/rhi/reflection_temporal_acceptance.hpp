#pragma once

#include "shader/package.hpp"

#include <filesystem>

namespace renderlab
{
class Logger;
namespace rhi
{
class Runtime;
}
namespace samples
{
/// Exercise the actual temporal shader with synthetic reprojection and rejection fixtures.
void VerifyReflectionTemporal(rhi::Runtime&, shader::Target, const std::filesystem::path&, Logger&);
} // namespace samples
} // namespace renderlab
