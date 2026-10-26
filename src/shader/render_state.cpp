#include "shader/definition.hpp"

#include <array>

namespace renderlab::shader
{
std::vector<EnumDefinition> BuiltinEnums()
{
    return {
        {"SurfaceType",
         {{"Opaque", static_cast<uint32_t>(shader::SurfaceType::Opaque)},
          {"Transparent", static_cast<uint32_t>(shader::SurfaceType::Transparent)}}},
        {"BlendMode",
         {{"Alpha", static_cast<uint32_t>(shader::BlendMode::Alpha)},
          {"Premultiplied", static_cast<uint32_t>(shader::BlendMode::Premultiplied)},
          {"Additive", static_cast<uint32_t>(shader::BlendMode::Additive)},
          {"Multiply", static_cast<uint32_t>(shader::BlendMode::Multiply)}}},
        {"CullMode",
         {{"None", static_cast<uint32_t>(rhi::CullMode::None)},
          {"Front", static_cast<uint32_t>(rhi::CullMode::Front)},
          {"Back", static_cast<uint32_t>(rhi::CullMode::Back)}}},
        {"FrontFace",
         {{"CounterClockwise", static_cast<uint32_t>(rhi::FrontFace::CounterClockwise)},
          {"Clockwise", static_cast<uint32_t>(rhi::FrontFace::Clockwise)}}},
        {"CompareOp",
         {{"Never", static_cast<uint32_t>(rhi::CompareOp::Never)},
          {"Less", static_cast<uint32_t>(rhi::CompareOp::Less)},
          {"Equal", static_cast<uint32_t>(rhi::CompareOp::Equal)},
          {"LessEqual", static_cast<uint32_t>(rhi::CompareOp::LessEqual)},
          {"Greater", static_cast<uint32_t>(rhi::CompareOp::Greater)},
          {"NotEqual", static_cast<uint32_t>(rhi::CompareOp::NotEqual)},
          {"GreaterEqual", static_cast<uint32_t>(rhi::CompareOp::GreaterEqual)},
          {"Always", static_cast<uint32_t>(rhi::CompareOp::Always)}}},
        {"StencilOp",
         {{"Keep", static_cast<uint32_t>(rhi::StencilOp::Keep)},
          {"Zero", static_cast<uint32_t>(rhi::StencilOp::Zero)},
          {"Replace", static_cast<uint32_t>(rhi::StencilOp::Replace)},
          {"IncrementClamp", static_cast<uint32_t>(rhi::StencilOp::IncrementClamp)},
          {"DecrementClamp", static_cast<uint32_t>(rhi::StencilOp::DecrementClamp)},
          {"Invert", static_cast<uint32_t>(rhi::StencilOp::Invert)},
          {"IncrementWrap", static_cast<uint32_t>(rhi::StencilOp::IncrementWrap)},
          {"DecrementWrap", static_cast<uint32_t>(rhi::StencilOp::DecrementWrap)}}},
        {"BlendFactor",
         {{"Zero", static_cast<uint32_t>(rhi::BlendFactor::Zero)},
          {"One", static_cast<uint32_t>(rhi::BlendFactor::One)},
          {"SrcColor", static_cast<uint32_t>(rhi::BlendFactor::SrcColor)},
          {"OneMinusSrcColor", static_cast<uint32_t>(rhi::BlendFactor::OneMinusSrcColor)},
          {"DstColor", static_cast<uint32_t>(rhi::BlendFactor::DstColor)},
          {"OneMinusDstColor", static_cast<uint32_t>(rhi::BlendFactor::OneMinusDstColor)},
          {"SrcAlpha", static_cast<uint32_t>(rhi::BlendFactor::SrcAlpha)},
          {"OneMinusSrcAlpha", static_cast<uint32_t>(rhi::BlendFactor::OneMinusSrcAlpha)},
          {"DstAlpha", static_cast<uint32_t>(rhi::BlendFactor::DstAlpha)},
          {"OneMinusDstAlpha", static_cast<uint32_t>(rhi::BlendFactor::OneMinusDstAlpha)},
          {"SrcAlphaSaturate", static_cast<uint32_t>(rhi::BlendFactor::SrcAlphaSaturate)}}},
        {"BlendOp",
         {{"Add", static_cast<uint32_t>(rhi::BlendOp::Add)},
          {"Subtract", static_cast<uint32_t>(rhi::BlendOp::Subtract)},
          {"ReverseSubtract", static_cast<uint32_t>(rhi::BlendOp::ReverseSubtract)},
          {"Min", static_cast<uint32_t>(rhi::BlendOp::Min)},
          {"Max", static_cast<uint32_t>(rhi::BlendOp::Max)}}},
    };
}
std::span<const StateFieldInfo> StateFields()
{
    static const std::array fields = {
        StateFieldInfo{"cull", "CullMode", 2, [](RenderState& state, int64_t value)
                       { state.raster.cull = static_cast<rhi::CullMode>(value); }},
        StateFieldInfo{"frontFace", "FrontFace", 1, [](RenderState& state, int64_t value)
                       { state.raster.frontFace = static_cast<rhi::FrontFace>(value); }},
        StateFieldInfo{"colorWriteMask", "uint", 255, [](RenderState& state, int64_t value)
                       { state.raster.colorWriteMask = static_cast<uint8_t>(value); }},
        StateFieldInfo{"depth.test", "bool", 1, [](RenderState& state, int64_t value)
                       { state.depth.testEnabled = static_cast<bool>(value); }},
        StateFieldInfo{"depth.compare", "CompareOp", 7, [](RenderState& state, int64_t value)
                       { state.depth.compare = static_cast<rhi::CompareOp>(value); }},
        StateFieldInfo{"depth.write", "bool", 1, [](RenderState& state, int64_t value)
                       { state.depth.writeEnabled = static_cast<bool>(value); }},
        StateFieldInfo{"blend.enabled", "bool", 1, [](RenderState& state, int64_t value)
                       { state.blend.enabled = static_cast<bool>(value); }},
        StateFieldInfo{"blend.srcColor", "BlendFactor", 10, [](RenderState& state, int64_t value)
                       { state.blend.srcColor = static_cast<rhi::BlendFactor>(value); }},
        StateFieldInfo{"blend.dstColor", "BlendFactor", 10, [](RenderState& state, int64_t value)
                       { state.blend.dstColor = static_cast<rhi::BlendFactor>(value); }},
        StateFieldInfo{"blend.colorOp", "BlendOp", 4, [](RenderState& state, int64_t value)
                       { state.blend.colorOp = static_cast<rhi::BlendOp>(value); }},
        StateFieldInfo{"blend.srcAlpha", "BlendFactor", 10, [](RenderState& state, int64_t value)
                       { state.blend.srcAlpha = static_cast<rhi::BlendFactor>(value); }},
        StateFieldInfo{"blend.dstAlpha", "BlendFactor", 10, [](RenderState& state, int64_t value)
                       { state.blend.dstAlpha = static_cast<rhi::BlendFactor>(value); }},
        StateFieldInfo{"blend.alphaOp", "BlendOp", 4, [](RenderState& state, int64_t value)
                       { state.blend.alphaOp = static_cast<rhi::BlendOp>(value); }},
        StateFieldInfo{"stencil.enabled", "bool", 1, [](RenderState& state, int64_t value)
                       { state.stencil.enabled = static_cast<bool>(value); }},
        StateFieldInfo{"stencil.readMask", "uint", 255, [](RenderState& state, int64_t value)
                       { state.stencil.readMask = static_cast<uint8_t>(value); }},
        StateFieldInfo{"stencil.writeMask", "uint", 255, [](RenderState& state, int64_t value)
                       { state.stencil.writeMask = static_cast<uint8_t>(value); }},
        StateFieldInfo{"stencil.reference", "uint", 255, [](RenderState& state, int64_t value)
                       { state.stencilReference = static_cast<uint32_t>(value); }},
        StateFieldInfo{"stencil.front.compare", "CompareOp", 7,
                       [](RenderState& state, int64_t value)
                       { state.stencil.front.compare = static_cast<rhi::CompareOp>(value); }},
        StateFieldInfo{"stencil.front.fail", "StencilOp", 7, [](RenderState& state, int64_t value)
                       { state.stencil.front.fail = static_cast<rhi::StencilOp>(value); }},
        StateFieldInfo{"stencil.front.depthFail", "StencilOp", 7,
                       [](RenderState& state, int64_t value)
                       { state.stencil.front.depthFail = static_cast<rhi::StencilOp>(value); }},
        StateFieldInfo{"stencil.front.pass", "StencilOp", 7, [](RenderState& state, int64_t value)
                       { state.stencil.front.pass = static_cast<rhi::StencilOp>(value); }},
        StateFieldInfo{"stencil.back.compare", "CompareOp", 7, [](RenderState& state, int64_t value)
                       { state.stencil.back.compare = static_cast<rhi::CompareOp>(value); }},
        StateFieldInfo{"stencil.back.fail", "StencilOp", 7, [](RenderState& state, int64_t value)
                       { state.stencil.back.fail = static_cast<rhi::StencilOp>(value); }},
        StateFieldInfo{"stencil.back.depthFail", "StencilOp", 7,
                       [](RenderState& state, int64_t value)
                       { state.stencil.back.depthFail = static_cast<rhi::StencilOp>(value); }},
        StateFieldInfo{"stencil.back.pass", "StencilOp", 7, [](RenderState& state, int64_t value)
                       { state.stencil.back.pass = static_cast<rhi::StencilOp>(value); }},
    };
    return fields;
}
} // namespace renderlab::shader
