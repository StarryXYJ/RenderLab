#pragma once

#include "core/log.hpp"
#include "rendergraph/graph.hpp"
#include "rendergraph/pool.hpp"
#include "samples/common/demo_geometry.hpp"

#include <filesystem>
#include <map>

namespace renderlab::samples
{
struct GraphFrameSettings
{
    bool updateNoise = true, unusedFeature = true, cull = true;
};
struct GraphReadback
{
    uint64_t completion = 0;
    std::shared_ptr<rhi::Buffer> noise, color;
};
class GraphDemo final
{
public:
    GraphDemo(rhi::Runtime& runtime, const std::filesystem::path& shaders);
    void Prepare(rhi::Format format);
    GraphReadback Render(std::shared_ptr<rhi::Texture> color, rhi::State initial,
                         rhi::Swapchain* presentation = nullptr, bool readback = false,
                         GraphFrameSettings settings = {}, const std::filesystem::path& dump = {});
    rendergraph::PoolStats Stats() const
    {
        return pool_.Stats();
    }

private:
    rhi::Runtime& runtime_;
    rendergraph::Pool pool_;
    detail::DemoGeometry geometry_;
    std::shared_ptr<rhi::BindingLayout> computeLayout_, rasterLayout_;
    std::shared_ptr<rhi::Pipeline> initialize_, update_;
    std::shared_ptr<rhi::ShaderModule> vertex_, pixel_;
    std::shared_ptr<rhi::Sampler> sampler_;
    std::map<rhi::Format, std::shared_ptr<rhi::Pipeline>> pipelines_;
};
void VerifyGraph(rhi::Runtime& runtime, const std::filesystem::path& shaders,
                 const std::filesystem::path& output, Logger& logger);
void VerifyGraphBuffers(rhi::Runtime& runtime, const std::filesystem::path& shaders,
                        const std::filesystem::path& output);
} // namespace renderlab::samples
