#pragma once

#include "apps/sandbox/options.hpp"
#include "renderer/scene_renderer.hpp"
#include "rhi/device.hpp"
#include "rhi/runtime.hpp"

#include <map>
#include <string>

namespace renderlab::sandbox
{
renderer::SceneRenderOptions RenderOptions(const Options& options);
/** @brief Accumulates completed frames only; startup/retirement waits are excluded from CPU work.
 */
class Statistics final
{
public:
    void Add(const renderer::SceneReadback& frame, const rhi::Runtime& runtime, double cpuMs);
    std::string Text(const scene::Snapshot& snapshot, rhi::MemoryStats memory) const;
    void Save(const std::filesystem::path& path, const Options& options,
              const rhi::DeviceReport& device, rhi::MemoryStats memory) const;

private:
    struct Samples
    {
        uint64_t count = 0;
        double total = 0, minimum = 0, maximum = 0;
        void Add(double value);
        double Mean() const;
    } cpu_, compile_, prepare_, record_;
    std::map<std::string, Samples> passes_;
};
} // namespace renderlab::sandbox
