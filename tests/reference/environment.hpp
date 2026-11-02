#pragma once

#include "renderer/environment.hpp"

namespace renderlab::tests::reference
{
renderer::EnvironmentImage PrefilterEnvironment(const assets::Image*, math::Vec3 fallback);
renderer::EnvironmentImage IntegrateEnvironmentBrdf();
} // namespace renderlab::tests::reference
