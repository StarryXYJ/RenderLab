#pragma once

#include "core/log.hpp"
#include <filesystem>

namespace renderlab::rhi
{
class Runtime;
}

namespace renderlab::samples
{
class RhiDemo;

/// Verify a prepared scene through the same public RHI used by presentation.
void VerifyRhi(rhi::Runtime& runtime, RhiDemo& demo, Logger& logger,
               const std::filesystem::path& outputDirectory);
} // namespace renderlab::samples
