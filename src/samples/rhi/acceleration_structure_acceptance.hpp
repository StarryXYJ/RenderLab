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
/// Verify BLAS/TLAS construction, graph declarations and Ray Query on the active backend.
void VerifyAccelerationStructures(rhi::Runtime& runtime, shader::Target target,
                                  const std::filesystem::path& shader,
                                  const std::filesystem::path& output, Logger& logger);
} // namespace samples
} // namespace renderlab
