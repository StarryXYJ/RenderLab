#pragma once

#include "rhi/resources.hpp"

#include <filesystem>
#include <memory>

namespace renderlab::rhi
{
class Runtime;
class CommandList;
} // namespace renderlab::rhi
namespace renderlab
{
class Logger;
}

namespace renderlab::samples
{
/** @brief Shared RHI demonstration scene; owns no window and contains no native API calls. */
class RhiDemo
{
public:
    RhiDemo(rhi::Runtime& runtime, const std::filesystem::path& shaderDirectory, Logger& logger);
    void Prepare(rhi::Format format);
    void Render(rhi::CommandList& commands, std::shared_ptr<rhi::TextureView> color,
                std::shared_ptr<rhi::TextureView> depth);
    /// Borrow the scene output for diagnostics; the returned reference does not transfer ownership.
    const std::shared_ptr<rhi::Texture>& GetNoiseTexture() const
    {
        return noise_;
    }

private:
    std::shared_ptr<rhi::Pipeline> Graphics(rhi::Format format);
    rhi::Runtime& runtime_;
    std::shared_ptr<rhi::Buffer> vertices_, indices_, indirect_;
    std::shared_ptr<rhi::Texture> noise_;
    std::shared_ptr<rhi::BindingLayout> graphicsLayout_;
    std::shared_ptr<rhi::BindingGroup> graphicsGroup_;
    std::shared_ptr<rhi::ShaderModule> vertex_, pixel_;
    std::shared_ptr<rhi::Pipeline> graphicsRgba_, graphicsBgra_;
};
} // namespace renderlab::samples
