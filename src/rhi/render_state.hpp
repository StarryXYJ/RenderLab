#pragma once

#include <cstdint>

namespace renderlab::rhi
{
enum class CompareOp
{
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always
};
enum class StencilOp
{
    Keep,
    Zero,
    Replace,
    IncrementClamp,
    DecrementClamp,
    Invert,
    IncrementWrap,
    DecrementWrap
};
enum class BlendFactor
{
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
    SrcAlphaSaturate
};
enum class BlendOp
{
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max
};
enum class CullMode
{
    None,
    Front,
    Back
};
enum class FrontFace
{
    CounterClockwise,
    Clockwise
};
/** @brief Immutable fixed-function state; all fields participate in pipeline identity. */
struct RasterState
{
    CullMode cull = CullMode::Back;
    FrontFace frontFace = FrontFace::CounterClockwise;
    uint8_t colorWriteMask = 15;
    bool operator==(const RasterState&) const = default;
};
struct DepthState
{
    bool testEnabled = false, writeEnabled = false;
    CompareOp compare = CompareOp::GreaterEqual;
    bool operator==(const DepthState&) const = default;
};
struct BlendState
{
    bool enabled = false;
    BlendFactor srcColor = BlendFactor::SrcAlpha, dstColor = BlendFactor::OneMinusSrcAlpha;
    BlendOp colorOp = BlendOp::Add;
    BlendFactor srcAlpha = BlendFactor::One, dstAlpha = BlendFactor::Zero;
    BlendOp alphaOp = BlendOp::Add;
    bool operator==(const BlendState&) const = default;
};
struct StencilFaceState
{
    CompareOp compare = CompareOp::Always;
    StencilOp fail = StencilOp::Keep, depthFail = StencilOp::Keep, pass = StencilOp::Keep;
    bool operator==(const StencilFaceState&) const = default;
};
struct StencilState
{
    bool enabled = false;
    uint8_t readMask = 255, writeMask = 255;
    StencilFaceState front, back;
    bool operator==(const StencilState&) const = default;
    /// @brief Conservative write capability, independent of the current reference or geometry.
    bool Writes() const
    {
        const auto writes = [](const StencilFaceState& face)
        {
            return face.fail != StencilOp::Keep || face.depthFail != StencilOp::Keep ||
                   face.pass != StencilOp::Keep;
        };
        return enabled && writeMask && (writes(front) || writes(back));
    }
};
} // namespace renderlab::rhi
