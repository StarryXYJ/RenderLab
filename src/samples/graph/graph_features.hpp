#pragma once

#include "renderer/feature.hpp"
#include "rhi/resources.hpp"
#include "samples/common/demo_geometry.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace renderlab::samples::detail
{
struct NoisePrograms
{
    std::shared_ptr<rhi::Pipeline> initialize, update;
};
struct RasterPrograms
{
    std::shared_ptr<rhi::Pipeline> pipeline;
    std::shared_ptr<rhi::Sampler> sampler;
};
class NoiseFeature final : public renderer::RenderFeature
{
public:
    NoiseFeature(NoisePrograms programs, std::string output, uint32_t width, uint32_t height,
                 bool update = true)
        : RenderFeature(output), programs_(std::move(programs)), output_(std::move(output)),
          width_(width), height_(height), update_(update)
    {
    }
    void Build(rendergraph::Graph&, const renderer::RenderFrame&) override;

private:
    NoisePrograms programs_;
    std::string output_;
    uint32_t width_, height_;
    bool update_;
};
class DemoSceneFeature final : public renderer::RenderFeature
{
public:
    DemoSceneFeature(RasterPrograms programs, DemoGeometry geometry, std::string noise = "Noise",
                     std::string color = "CameraColor", std::string depth = "CameraDepth")
        : RenderFeature(color, {noise}), programs_(std::move(programs)),
          geometry_(std::move(geometry)), noise_(std::move(noise)), color_(std::move(color)),
          depth_(std::move(depth))
    {
    }
    void Build(rendergraph::Graph&, const renderer::RenderFrame&) override;

private:
    RasterPrograms programs_;
    DemoGeometry geometry_;
    std::string noise_, color_, depth_;
};
class ReadbackFeature final : public renderer::RenderFeature
{
public:
    ReadbackFeature(std::string source, std::shared_ptr<rhi::Buffer> destination, uint32_t rowPitch,
                    std::string producer = {})
        : RenderFeature("Readback." + source, {producer.empty() ? source : producer}),
          source_(std::move(source)), destination_(std::move(destination)), rowPitch_(rowPitch)
    {
    }
    void Build(rendergraph::Graph&, const renderer::RenderFrame&) override;

private:
    std::string source_;
    std::shared_ptr<rhi::Buffer> destination_;
    uint32_t rowPitch_;
};
} // namespace renderlab::samples::detail
