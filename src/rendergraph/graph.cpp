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
    Require(active == pass, "PassBuilder escaped Setup");
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
TextureHandle Graph::Create(rhi::TextureDesc desc)
{
    impl_->Building();
    Require(desc.width && desc.height, "Zero texture extent");
    detail::Resource resource;
    resource.isTexture = true;
    resource.textureDesc = std::move(desc);
    const auto index = static_cast<uint32_t>(impl_->resources.size());
    if (resource.textureDesc.name.empty())
        resource.textureDesc.name = "Texture." + std::to_string(index);
    impl_->resources.push_back(std::move(resource));
    return {impl_->id, index, 0};
}
BufferHandle Graph::Create(rhi::BufferDesc desc)
{
    impl_->Building();
    Require(desc.size > 0, "Zero buffer extent");
    Require(desc.memory == rhi::Memory::Device,
            "Graph transient buffers require Device memory; import host buffers explicitly");
    detail::Resource resource;
    resource.bufferDesc = std::move(desc);
    const auto index = static_cast<uint32_t>(impl_->resources.size());
    if (resource.bufferDesc.name.empty())
        resource.bufferDesc.name = "Buffer." + std::to_string(index);
    impl_->resources.push_back(std::move(resource));
    return {impl_->id, index, 0};
}
TextureHandle Graph::Import(std::shared_ptr<rhi::Texture> texture, ImportInfo info)
{
    impl_->Building();
    Require(bool(texture), "Null imported texture");
    Require(std::ranges::none_of(impl_->resources, [&](auto& r) { return r.texture == texture; }),
            "Duplicate physical texture import");
    Require(!info.initialized || info.state != rhi::State::Undefined,
            "Initialized import needs a defined initial state");
    auto handle = Create(texture->desc);
    auto& r = impl_->resources.back();
    r.imported = true;
    r.texture = std::move(texture);
    r.initial = info;
    r.start = info.state;
    impl_->ValidateState(r, info.state);
    return handle;
}
BufferHandle Graph::Import(std::shared_ptr<rhi::Buffer> buffer, ImportInfo info)
{
    impl_->Building();
    Require(bool(buffer), "Null imported buffer");
    Require(std::ranges::none_of(impl_->resources, [&](auto& r) { return r.buffer == buffer; }),
            "Duplicate physical buffer import");
    Require(!info.initialized || info.state != rhi::State::Undefined,
            "Initialized import needs a defined initial state");
    detail::Resource r;
    r.bufferDesc = buffer->desc;
    r.buffer = std::move(buffer);
    r.imported = true;
    r.initial = info;
    r.start = info.state;
    impl_->ValidateState(r, info.state);
    const auto index = static_cast<uint32_t>(impl_->resources.size());
    impl_->resources.push_back(std::move(r));
    return {impl_->id, index, 0};
}
void Graph::Export(TextureHandle handle, rhi::State state)
{
    impl_->Building();
    auto& r = impl_->Validate(handle.graph, handle.resource, handle.version, true);
    Require(handle.version + 1 == r.versions.size(), "Export requires the latest resource version");
    Require(state != rhi::State::Undefined, "Export requires a defined final state");
    r.exported = state;
    impl_->ValidateState(r, state);
}
void Graph::Export(BufferHandle handle, rhi::State state)
{
    impl_->Building();
    auto& r = impl_->Validate(handle.graph, handle.resource, handle.version, false);
    Require(handle.version + 1 == r.versions.size(), "Export requires the latest resource version");
    Require(state != rhi::State::Undefined, "Export requires a defined final state");
    r.exported = state;
    impl_->ValidateState(r, state);
}
PassBuilder Graph::BeginPass(std::string name, PassKind kind,
                             std::unique_ptr<detail::Callbacks> callbacks,
                             std::source_location source)
{
    impl_->Building();
    Require(impl_->active == UINT32_MAX, "Nested AddPass is not supported");
    detail::Pass pass;
    pass.name = std::move(name);
    pass.kind = kind;
    pass.callbacks = std::move(callbacks);
    pass.source = std::format("{}:{}", source.file_name(), source.line());
    impl_->active = static_cast<uint32_t>(impl_->passes.size());
    impl_->passes.push_back(std::move(pass));
    return {*this, impl_->active};
}
PassHandle Graph::EndPass(PassBuilder& builder)
{
    impl_->Builder(builder.pass_);
    const auto index = impl_->active;
    impl_->active = UINT32_MAX;
    return {impl_->id, index};
}
void Graph::Poison()
{
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
} // namespace renderlab::rendergraph
