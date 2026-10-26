#pragma once

#include "rhi/render_state.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace renderlab::shader
{
enum class PassRole
{
    Custom,
    Forward,
    ShadowCaster
};
enum class SurfaceType
{
    Opaque,
    Transparent
};
enum class BlendMode
{
    Alpha,
    Premultiplied,
    Additive,
    Multiply
};
enum class StatePreset
{
    Explicit,
    SurfaceForward,
    ShadowCaster
};
enum class PassEnable
{
    Always,
    Never,
    OpaqueShadow
};
/** @brief Literal or a stable material PropertyId, resolved and type checked at declaration load.
 */
struct StateValue
{
    int64_t literal = 0;
    std::string property;
};
struct RenderPolicy
{
    StateValue surface, blend, alphaClip, queueOffset;
};
/** @brief Resolved CPU state, independent of GPU object identities and attachment selection. */
struct RenderState
{
    rhi::RasterState raster;
    rhi::DepthState depth;
    rhi::BlendState blend;
    rhi::StencilState stencil;
    uint32_t stencilReference = 0;
};
struct StateFieldInfo
{
    std::string_view path, type;
    int64_t maximum;
    void (*apply)(RenderState&, int64_t);
};
/** @brief Fixed grammar field index; never resolved from an HLSL symbol at draw time. */
struct StateOverride
{
    uint32_t field;
    StateValue value;
    bool faceSpecific = false;
};
struct StateDefinition
{
    StatePreset preset = StatePreset::Explicit;
    std::vector<StateOverride> overrides;
};
std::span<const StateFieldInfo> StateFields();
} // namespace renderlab::shader
