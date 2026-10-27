#pragma once

#include "renderer/scene_renderer.hpp"

#include <filesystem>
#include <memory>

namespace renderlab
{
class Logger;
namespace samples
{
/// Fixed motion/stop/reveal sequences and per-frame references; records stability and image error.
void VerifyReflectionTemporalMotion(rhi::Runtime&, renderer::SceneRenderer&,
                                    std::shared_ptr<rhi::Texture>, rhi::State&,
                                    const std::filesystem::path&, Logger&);
} // namespace samples
} // namespace renderlab
