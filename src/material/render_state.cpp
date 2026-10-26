#include "material/render_state.hpp"

#include <cmath>
#include <stdexcept>

namespace renderlab::material
{
namespace
{
int64_t Read(const Asset& asset, const shader::StateValue& source, int64_t maximum,
             int64_t minimum = 0)
{
    const double value = source.property.empty() ? static_cast<double>(source.literal)
                                                 : asset.values.at(source.property).numbers.at(0);
    if (!std::isfinite(value) || value < minimum || value > maximum || value != std::floor(value))
        throw std::invalid_argument("Material state value outside range: " + source.property);
    return static_cast<int64_t>(value);
}
} // namespace
RenderPolicy ResolvePolicy(const shader::Definition& definition, const Asset& asset)
{
    RenderPolicy result;
    result.surface = static_cast<shader::SurfaceType>(Read(asset, definition.render.surface, 1));
    result.blend = static_cast<shader::BlendMode>(Read(asset, definition.render.blend, 3));
    result.alphaClip = Read(asset, definition.render.alphaClip, 1) != 0;
    const auto base = result.surface == shader::SurfaceType::Transparent ? 3000
                      : result.alphaClip                                 ? 2450
                                                                         : 2000;
    result.queue = base + static_cast<int32_t>(Read(asset, definition.render.queueOffset, 99, -99));
    return result;
}
bool PassEnabled(const shader::Definition& definition, const shader::Pass& pass, const Asset& asset)
{
    if (pass.enabled == shader::PassEnable::Always)
        return true;
    if (pass.enabled == shader::PassEnable::Never)
        return false;
    return ResolvePolicy(definition, asset).surface == shader::SurfaceType::Opaque &&
           Read(asset, pass.castShadows, 1) != 0;
}
shader::RenderState ResolveState(const shader::Definition& definition, const shader::Pass& pass,
                                 const Asset& asset)
{
    using namespace rhi;
    shader::RenderState result;
    if (pass.state.preset == shader::StatePreset::ShadowCaster)
        result.depth = {true, true};
    else if (pass.state.preset == shader::StatePreset::SurfaceForward)
    {
        const auto policy = ResolvePolicy(definition, asset);
        const bool transparent = policy.surface == shader::SurfaceType::Transparent;
        result.depth = {true, !transparent};
        result.blend.enabled = transparent;
        result.blend.dstAlpha = BlendFactor::OneMinusSrcAlpha;
        if (transparent && policy.blend == shader::BlendMode::Premultiplied)
            result.blend.srcColor = BlendFactor::One;
        else if (transparent && policy.blend == shader::BlendMode::Additive)
        {
            result.blend.dstColor = BlendFactor::One;
            result.blend.srcAlpha = BlendFactor::Zero;
            result.blend.dstAlpha = BlendFactor::One;
        }
        else if (transparent && policy.blend == shader::BlendMode::Multiply)
        {
            result.blend.srcColor = BlendFactor::DstColor;
            result.blend.dstColor = BlendFactor::Zero;
            result.blend.srcAlpha = BlendFactor::Zero;
            result.blend.dstAlpha = BlendFactor::One;
        }
    }
    const auto fields = shader::StateFields();
    for (const auto& field : pass.state.overrides)
    {
        if (field.field >= fields.size())
            throw std::invalid_argument("Unknown fixed state field index");
        const auto& info = fields[field.field];
        info.apply(result, Read(asset, field.value, info.maximum));
    }
    return result;
}
} // namespace renderlab::material
