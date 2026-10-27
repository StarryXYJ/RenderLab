#pragma once

#include "renderer/scene_renderer.hpp"

#include <filesystem>
#include <memory>

namespace renderlab
{
class Logger;
namespace samples
{
/// Fixed camera trajectories and endpoint references; records error without a quality threshold.
void VerifyReflectionTemporalMotion(rhi::Runtime&, renderer::SceneRenderer&,
                                    std::shared_ptr<rhi::Texture>, rhi::State&,
                                    const std::filesystem::path&, Logger&);
} // namespace samples
} // namespace renderlab
