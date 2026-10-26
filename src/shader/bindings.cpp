#include "shader/package.hpp"

#include <stdexcept>

namespace renderlab::shader
{
BindingHandle Package::BlockBinding(PassHandle pass, std::string_view localName) const
{
    GetPass(pass);
    const auto& blocks = interfaces.at(pass.index).blocks;
    for (uint32_t i = 0; i < blocks.size(); ++i)
        if (blocks[i].name == localName)
            return {pass, i, true};
    throw std::invalid_argument("Shader lacks local parameter block " + std::string(localName));
}
BindingHandle Package::ResourceBinding(PassHandle pass, std::string_view localName) const
{
    GetPass(pass);
    const auto& resources = interfaces.at(pass.index).resources;
    for (uint32_t i = 0; i < resources.size(); ++i)
        if (resources[i].property.symbol == localName)
            return {pass, i, false};
    throw std::invalid_argument("Shader lacks local resource " + std::string(localName));
}
std::optional<BindingHandle> Package::SchemaBinding(PassHandle pass,
                                                    const BlockContract& contract) const
{
    GetPass(pass);
    const auto& blocks = interfaces.at(pass.index).blocks;
    for (uint32_t i = 0; i < blocks.size(); ++i)
    {
        const auto& block = blocks[i];
        if (block.schemaId != contract.id)
            continue;
        if (block.schemaVersion != contract.version || block.layoutHash != contract.layoutHash ||
            block.scope != contract.scope)
            throw std::invalid_argument("Incompatible parameter schema: " + block.schemaId);
        return BindingHandle{pass, i, true};
    }
    return std::nullopt;
}
std::optional<BindingHandle> Package::SemanticBinding(PassHandle pass,
                                                      Property::BindingSemantic semantic) const
{
    GetPass(pass);
    if (semantic == Property::BindingSemantic::None)
        throw std::invalid_argument("None is not a resource binding contract");
    const auto& resources = interfaces.at(pass.index).resources;
    std::optional<BindingHandle> result;
    for (uint32_t i = 0; i < resources.size(); ++i)
        if (resources[i].property.binding == semantic)
        {
            if (result)
                throw std::invalid_argument("Ambiguous resource binding semantic");
            result = BindingHandle{pass, i, false};
        }
    return result;
}
const BlockLayout& Package::GetBlock(BindingHandle handle) const
{
    GetPass(handle.pass);
    const auto& blocks = interfaces.at(handle.pass.index).blocks;
    if (!handle.constant || handle.index >= blocks.size())
        throw std::invalid_argument("Invalid constant block handle");
    return blocks[handle.index];
}
const ResourceLayout& Package::GetResource(BindingHandle handle) const
{
    GetPass(handle.pass);
    const auto& resources = interfaces.at(handle.pass.index).resources;
    if (handle.constant || handle.index >= resources.size())
        throw std::invalid_argument("Invalid resource binding handle");
    return resources[handle.index];
}
} // namespace renderlab::shader
