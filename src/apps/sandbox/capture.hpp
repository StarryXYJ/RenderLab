#pragma once

#include "apps/sandbox/options.hpp"
#include "core/log.hpp"
#include "rhi/device.hpp"
#include "rhi/runtime.hpp"

#include <filesystem>

namespace renderlab::sandbox
{
/// @brief Fixed-camera capture and optional warmup/measurement run; independent of presentation.
void CaptureScene(rhi::Runtime& runtime, const rhi::Device& device, const Options& options,
                  const std::filesystem::path& shaders, Logger& logger);
} // namespace renderlab::sandbox
