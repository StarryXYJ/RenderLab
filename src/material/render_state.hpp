#pragma once

#include "material/asset.hpp"

namespace renderlab::material
{
struct RenderPolicy
{
    shader::SurfaceType surface = shader::SurfaceType::Opaque;
    shader::BlendMode blend = shader::BlendMode::Alpha;
    bool alphaClip = false;
    int32_t queue = 2000;
};
/** @brief Resolve a validated material's finite policy and fixed-function state on the CPU. */
RenderPolicy ResolvePolicy(const shader::Definition& definition, const Asset& asset);
shader::RenderState ResolveState(const shader::Definition& definition, const shader::Pass& pass,
                                 const Asset& asset);
bool PassEnabled(const shader::Definition& definition, const shader::Pass& pass,
                 const Asset& asset);
} // namespace renderlab::material
