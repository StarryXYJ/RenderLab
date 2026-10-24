#pragma once

#include "core/log.hpp"
#include "rhi/runtime.hpp"
#include "shader/definition.hpp"

#include <filesystem>

namespace renderlab::samples
{
/// Code-driven M2 example and GPU acceptance. Writes inspectable material images and assets.
void VerifyMaterial(rhi::Runtime& runtime, shader::Target target, Logger& logger,
                    const std::filesystem::path& source, const std::filesystem::path& output);
} // namespace renderlab::samples
