#pragma once

#include "rhi/runtime.hpp"

namespace renderlab::samples::detail
{
struct DemoVertex
{
    float position[3], color[4], uv[2];
};
static_assert(sizeof(DemoVertex) == 36);
struct DemoGeometry
{
    std::shared_ptr<rhi::Buffer> vertices, indices, indirect;
};
DemoGeometry CreateDemoGeometry(rhi::Runtime& runtime);
} // namespace renderlab::samples::detail
