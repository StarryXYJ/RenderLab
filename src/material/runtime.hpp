#pragma once

#include "material/asset.hpp"
#include "rhi/runtime.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace renderlab::material
{
/// @brief Per-draw pass selection, vertex layout and external values; no recording occurs here.
struct Preparation
{
    shader::PassSelection pass;
    rhi::Format color = rhi::Format::Rgba8Srgb;
    uint32_t vertexStride = 0;
    std::vector<rhi::VertexAttribute> attributes;
    std::optional<rhi::Format> depthStencilFormat;
    bool mirrored = false;
    /// Explicit generation/pass-local interface handles; constant members use stable PropertyId.
    std::map<shader::BindingHandle, rhi::Binding> bindings;
    std::map<shader::BindingHandle, std::map<std::string, Value>> constants;
    /// Graph-owned pass resources, resolved during recording rather than preparation.
    /// Only external resource handles are accepted; constants remain prepared.
    std::set<shader::BindingHandle> deferredResources;
};
using ResourceResolver = std::function<rhi::Binding(const shader::Property&, const std::string&)>;
struct GpuGeneration;
/** @brief Immutable recording snapshot. Commands retain its pipeline and resources when bound. */
struct Prepared
{
    uint32_t stencilReference = 0;
    std::shared_ptr<const GpuGeneration> generation;
    std::shared_ptr<rhi::Pipeline> pipeline;
    std::vector<rhi::Binding> resources;
    std::vector<shader::BindingHandle> deferredResources;
    /// Newly uploaded constants require HostWrite -> UniformRead before their first shader use.
    std::vector<std::shared_ptr<rhi::Buffer>> constants;
};
/** @brief Owner-thread preparation; never compiles source and never records implicit barriers. */
class Runtime final
{
public:
    Runtime(rhi::Runtime& runtime, shader::Target target);
    /// @brief Creates immutable binding values; caller imports them into Graph or builds a group.
    Prepared Prepare(std::shared_ptr<const shader::Package> package, const Asset& asset,
                     const Preparation& preparation, const ResourceResolver& resolve = {});

private:
    rhi::Runtime& runtime_;
    shader::Target target_;
    std::thread::id owner_ = std::this_thread::get_id();
    std::vector<std::weak_ptr<GpuGeneration>> generations_;
};
} // namespace renderlab::material
