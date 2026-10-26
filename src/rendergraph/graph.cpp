#include "rendergraph/detail/graph_impl.hpp"

#include <algorithm>
#include <atomic>
#include <format>

namespace renderlab::rendergraph
{
using detail::Require;
void Graph::Impl::Building() const
{
    Require(phase == detail::Phase::Building, "Graph is frozen or invalid");
}
void Graph::Impl::Builder(uint32_t pass) const
{
    Building();
    Require(active == pass, "PassBuilder is sealed or another pass is active");
}
detail::Resource& Graph::Impl::Validate(uint64_t graph, uint32_t index, uint32_t version,
                                        bool texture)
{
    Require(graph == id && index < resources.size(), "Foreign, expired or invalid graph handle");
    auto& resource = resources[index];
    Require(resource.isTexture == texture && version < resource.versions.size(),
            "Handle kind/version mismatch");
    return resource;
}
Graph::Graph() : impl_(std::make_unique<Impl>())
{
    static std::atomic<uint64_t> next{0};
    impl_->id = ++next;
}
Graph::~Graph() = default;
TextureHandle Graph::CreateTexture(std::string name, rhi::TextureDesc desc)
{
    auto& g = *impl_;
    g.Building();
    Require(!name.empty() && !g.names.contains(name), "Empty or duplicate resource name: " + name);
    Require(desc.width && desc.height, "Zero texture extent");
    detail::Resource r;
    r.isTexture = true;
    desc.name = name;
    r.textureDesc = std::move(desc);
    const auto index = static_cast<uint32_t>(g.resources.size());
    g.resources.push_back(std::move(r));
    g.names.emplace(std::move(name), Impl::NamedResource{index, true});
    return {g.id, index, 0};
}
BufferHandle Graph::CreateBuffer(std::string name, rhi::BufferDesc desc)
{
    auto& g = *impl_;
    g.Building();
    Require(!name.empty() && !g.names.contains(name), "Empty or duplicate resource name: " + name);
    Require(desc.size, "Zero buffer extent");
    Require(desc.memory == rhi::Memory::Device,
            "Graph transient buffers require Device memory; import host buffers explicitly");
    detail::Resource r;
    desc.name = name;
    r.bufferDesc = std::move(desc);
    const auto index = static_cast<uint32_t>(g.resources.size());
    g.resources.push_back(std::move(r));
    g.names.emplace(std::move(name), Impl::NamedResource{index, false});
    return {g.id, index, 0};
}
TextureHandle Graph::ImportTexture(std::string name, std::shared_ptr<rhi::Texture> texture,
                                   ImportInfo info)
{
    impl_->Building();
    Require(bool(texture), "Null imported texture");
    Require(std::ranges::none_of(impl_->resources, [&](auto& r) { return r.texture == texture; }),
            "Duplicate physical texture import");
    Require(!info.initialized || info.state != rhi::State::Undefined,
            "Initialized import needs a defined initial state");
    detail::Resource imported;
    imported.isTexture = true;
    imported.imported = true;
    imported.textureDesc = texture->desc;
    impl_->ValidateState(imported, info.state);
    auto handle = CreateTexture(std::move(name), texture->desc);
    auto& r = impl_->resources.back();
    r.imported = true;
    r.texture = std::move(texture);
    r.initial = info;
    r.start = info.state;
    return handle;
}
BufferHandle Graph::ImportBuffer(std::string name, std::shared_ptr<rhi::Buffer> buffer,
                                 ImportInfo info)
{
    auto& g = *impl_;
    g.Building();
    Require(bool(buffer), "Null imported buffer");
    Require(!name.empty() && !g.names.contains(name), "Empty or duplicate resource name: " + name);
    Require(std::ranges::none_of(g.resources, [&](auto& r) { return r.buffer == buffer; }),
            "Duplicate physical buffer import");
    Require(!info.initialized || info.state != rhi::State::Undefined,
            "Initialized import needs a defined initial state");
    detail::Resource r;
    r.bufferDesc = buffer->desc;
    r.bufferDesc.name = name;
    r.buffer = std::move(buffer);
    r.imported = true;
    r.initial = info;
    r.start = info.state;
    g.ValidateState(r, info.state);
    const auto index = static_cast<uint32_t>(g.resources.size());
    g.resources.push_back(std::move(r));
    g.names.emplace(std::move(name), Impl::NamedResource{index, false});
    return {g.id, index, 0};
}
TextureHandle Graph::GetTexture(std::string_view name) const
{
    impl_->Building();
    const auto found = impl_->names.find(std::string(name));
    Require(found != impl_->names.end(), "Unknown resource name: " + std::string(name));
    Require(found->second.texture, "Resource type mismatch: " + std::string(name));
    const auto id = found->second.resource;
    return {impl_->id, id, static_cast<uint32_t>(impl_->resources[id].versions.size() - 1)};
}
BufferHandle Graph::GetBuffer(std::string_view name) const
{
    impl_->Building();
    const auto found = impl_->names.find(std::string(name));
    Require(found != impl_->names.end(), "Unknown resource name: " + std::string(name));
    Require(!found->second.texture, "Resource type mismatch: " + std::string(name));
    const auto id = found->second.resource;
    return {impl_->id, id, static_cast<uint32_t>(impl_->resources[id].versions.size() - 1)};
}
void Graph::SetTexture(std::string_view name, TextureHandle handle)
{
    impl_->Building();
    auto& r = impl_->Validate(handle.graph, handle.resource, handle.version, true);
    Require(handle.version + 1 == r.versions.size(), "SetTexture requires latest version");
    Require(!name.empty(), "Empty resource name");
    const auto old = impl_->names.find(std::string(name));
    Require(old == impl_->names.end() || old->second.texture, "Resource type mismatch");
    impl_->names[std::string(name)] = {handle.resource, true};
}
void Graph::SetBuffer(std::string_view name, BufferHandle handle)
{
    impl_->Building();
    auto& r = impl_->Validate(handle.graph, handle.resource, handle.version, false);
    Require(handle.version + 1 == r.versions.size(), "SetBuffer requires latest version");
    Require(!name.empty(), "Empty resource name");
    const auto old = impl_->names.find(std::string(name));
    Require(old == impl_->names.end() || !old->second.texture, "Resource type mismatch");
    impl_->names[std::string(name)] = {handle.resource, false};
}
rhi::TextureDesc Graph::GetTextureDesc(TextureHandle handle) const
{
    impl_->Building();
    return impl_->Validate(handle.graph, handle.resource, handle.version, true).textureDesc;
}
rhi::BufferDesc Graph::GetBufferDesc(BufferHandle handle) const
{
    impl_->Building();
    return impl_->Validate(handle.graph, handle.resource, handle.version, false).bufferDesc;
}
void Graph::Export(TextureHandle handle, rhi::State state)
{
    impl_->Building();
    auto& r = impl_->Validate(handle.graph, handle.resource, handle.version, true);
    Require(handle.version + 1 == r.versions.size(), "Export requires the latest resource version");
    Require(state != rhi::State::Undefined, "Export requires a defined final state");
    impl_->ValidateState(r, state);
    r.exported = state;
}
void Graph::Export(BufferHandle handle, rhi::State state)
{
    impl_->Building();
    auto& r = impl_->Validate(handle.graph, handle.resource, handle.version, false);
    Require(handle.version + 1 == r.versions.size(), "Export requires the latest resource version");
    Require(state != rhi::State::Undefined, "Export requires a defined final state");
    impl_->ValidateState(r, state);
    r.exported = state;
}
void Graph::Export(std::string_view name, rhi::State state)
{
    impl_->Building();
    auto found = impl_->names.find(std::string(name));
    Require(found != impl_->names.end(), "Unknown resource name: " + std::string(name));
    if (found->second.texture)
        Export(GetTexture(name), state);
    else
        Export(GetBuffer(name), state);
}
uint32_t Graph::BeginPass(std::string name, PassKind kind, std::source_location source)
{
    impl_->Building();
    Require(impl_->active == UINT32_MAX,
            "Seal the previous pass with Execute before adding a pass");
    detail::Pass pass;
    pass.name = std::move(name);
    pass.kind = kind;
    pass.source = std::format("{}:{}", source.file_name(), source.line());
    impl_->active = static_cast<uint32_t>(impl_->passes.size());
    impl_->passes.push_back(std::move(pass));
    return impl_->active;
}
RasterPassBuilder Graph::AddRasterPass(std::string name, std::source_location source)
{
    return {*this, BeginPass(std::move(name), PassKind::Raster, source)};
}
ComputePassBuilder Graph::AddComputePass(std::string name, std::source_location source)
{
    return {*this, BeginPass(std::move(name), PassKind::Compute, source)};
}
CopyPassBuilder Graph::AddCopyPass(std::string name, std::source_location source)
{
    return {*this, BeginPass(std::move(name), PassKind::Copy, source)};
}
uint32_t Graph::PassCount() const
{
    impl_->Building();
    return static_cast<uint32_t>(impl_->passes.size());
}
PassHandle Graph::GetPass(uint32_t index) const
{
    impl_->Building();
    Require(index < impl_->passes.size(), "Invalid pass index");
    return {impl_->id, index};
}
void Graph::Poison()
{
    impl_->commands.reset();
    impl_->phase = detail::Phase::Failed;
    impl_->active = UINT32_MAX;
}
void Graph::After(PassHandle pass, PassHandle predecessor)
{
    impl_->Building();
    Require(pass.graph == impl_->id && predecessor.graph == impl_->id &&
                pass.index < impl_->passes.size() && predecessor.index < impl_->passes.size(),
            "Foreign pass ordering handle");
    Require(pass.index != predecessor.index, "Pass cannot depend on itself");
    impl_->order.push_back({predecessor.index, pass.index, "explicit order"});
}
const Plan& Graph::GetPlan() const
{
    Require(impl_->phase != detail::Phase::Building && impl_->phase != detail::Phase::Failed,
            "Compile the graph before inspecting its plan");
    return impl_->plan;
}
RayTracingPassBuilder Graph::AddRayTracingPass(std::string name, std::source_location source)
{
    return {*this, BeginPass(std::move(name), PassKind::RayTracing, source)};
}
} // namespace renderlab::rendergraph
