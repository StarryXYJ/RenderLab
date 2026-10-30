#include "rendergraph/types.hpp"

#include <nlohmann/json.hpp>

namespace renderlab::rendergraph
{
namespace
{
const char* Name(rhi::State state)
{
    switch (state)
    {
    case rhi::State::Undefined:
        return "Undefined";
    case rhi::State::CopySource:
        return "CopySource";
    case rhi::State::CopyDest:
        return "CopyDest";
    case rhi::State::Vertex:
        return "Vertex";
    case rhi::State::Index:
        return "Index";
    case rhi::State::Indirect:
        return "Indirect";
    case rhi::State::Storage:
        return "Storage";
    case rhi::State::Sampled:
        return "Sampled";
    case rhi::State::ColorAttachment:
        return "ColorAttachment";
    case rhi::State::DepthWrite:
        return "DepthWrite";
    case rhi::State::Present:
        return "Present";
    case rhi::State::HostRead:
        return "HostRead";
    case rhi::State::HostWrite:
        return "HostWrite";
    case rhi::State::UniformRead:
        return "UniformRead";
    case rhi::State::ShaderTableRead:
        return "ShaderTableRead";
    case rhi::State::ShaderRead:
        return "ShaderRead";
    case rhi::State::AccelerationStructureBuildInput:
        return "ASBuildInput";
    case rhi::State::AccelerationStructureBuildWrite:
        return "ASBuildWrite";
    case rhi::State::AccelerationStructureRead:
        return "ASRead";
    }
    return "Unknown";
}
const char* Name(PassKind kind)
{
    return kind == PassKind::Raster    ? "Raster"
           : kind == PassKind::Compute ? "Compute"
           : kind == PassKind::Copy    ? "Copy"
                                       : "RayTracing";
}
} // namespace

std::string Plan::ToJson() const
{
    using Json = nlohmann::json;
    Json output{{"queue", asyncCompute ? "graphics+compute" : "graphics"},
                {"recording", recordingThreads == 1 ? "serial" : "parallel-native"},
                {"recordingThreads", recordingThreads},
                {"recordingChunks", recordingChunks},
                {"aliasing", memoryAlias}};
    output["aliasGroups"] = aliasGroups;
    output["aliasBackingBytes"] = aliasBackingBytes;
    output["asyncCompute"] = asyncCompute;
    output["asyncResources"] = asyncResources;
    output["gpuTimingReason"] = profilingReason;
    output["passes"] = Json::array();
    output["resources"] = Json::array();
    output["edges"] = Json::array();
    output["schedule"] = Json::array();
    for (uint32_t id = 0; id < passes.size(); ++id)
    {
        const auto& p = passes[id];
        Json info{{"name", p.name}, {"kind", Name(p.kind)}, {"source", p.source},
                  {"live", p.live}, {"reason", p.reason},   {"accesses", Json::array()}};
        info["id"] = id;
        info["recordingChunk"] =
            p.recordingChunk == UINT32_MAX ? Json(nullptr) : Json(p.recordingChunk);
        info["commandRange"] = {p.firstCommand, p.lastCommand};
        static constexpr const char* categories[] = {"Other", "Opaque", "Shadow", "Transparent",
                                                     "Fullscreen"};
        info["category"] = categories[static_cast<size_t>(p.category)];
        const auto& c = p.commands;
        info["commands"] = {{"direct", c.direct},
                            {"indexed", c.indexed},
                            {"indirect", c.indirect},
                            {"indirectCapacity", c.indirectCapacity},
                            {"batches", c.batches},
                            {"directInstances", c.directInstances},
                            {"indirectInstancesUnknown", c.indirectInstancesUnknown},
                            {"dispatch", c.dispatch},
                            {"threadGroups", c.threadGroups},
                            {"traceRays", c.traceRays},
                            {"rays", c.rays},
                            {"asBuilds", c.asBuilds},
                            {"asUpdates", c.asUpdates},
                            {"lastDispatch", c.lastDispatch},
                            {"lastTrace", c.lastTrace},
                            {"copyBytes", c.copyBytes}};
        for (auto& a : p.accesses)
            info["accesses"].push_back({{"resource", a.resource},
                                        {"inputVersion", a.inputVersion},
                                        {"outputVersion", a.outputVersion},
                                        {"use", int(a.use)},
                                        {"read", a.read},
                                        {"write", a.write},
                                        {"bufferOffset", a.bufferRange.offset},
                                        {"bufferSize", a.bufferRange.size},
                                        {"textureRange",
                                         {{"firstMip", a.textureRange.firstMip},
                                          {"mipCount", a.textureRange.mipCount},
                                          {"firstLayer", a.textureRange.firstLayer},
                                          {"layerCount", a.textureRange.layerCount},
                                          {"aspect", int(a.textureRange.aspect)}}}});
        output["passes"].push_back(std::move(info));
    }
    output["cpuNs"] = {{"compile", compileNs}, {"prepare", prepareNs}, {"record", recordNs}};
    for (uint32_t id = 0; id < resources.size(); ++id)
    {
        const auto& r = resources[id];
        static constexpr const char* formats[] = {"Rgba8",       "Bgra8", "Rgba8Srgb", "Bgra8Srgb",
                                                  "Rgba32Float", "D32",   "D32S8",     "R32Float"};
        output["resources"].push_back(
            {{"id", id},
             {"name", r.name},
             {"kind", r.texture ? "Texture" : "Buffer"},
             {"exported", r.exported},
             {"width", r.width},
             {"height", r.height},
             {"format", r.texture ? Json(formats[static_cast<size_t>(r.format)]) : Json(nullptr)},
             {"bufferBytes", r.bufferBytes},
             {"imported", r.imported},
             {"aliasGroup", r.aliasGroup == UINT32_MAX ? Json(nullptr) : Json(r.aliasGroup)},
             {"first", r.first == UINT32_MAX ? Json(nullptr) : Json(r.first)},
             {"last", r.last},
             {"estimatedBytes", r.estimatedBytes},
             {"versions", r.versions},
             {"initial", Name(r.initial)},
             {"final", Name(r.final)}});
    }
    for (const auto& e : edges)
        output["edges"].push_back({{"before", e.before}, {"after", e.after}, {"reason", e.reason}});
    const auto barriers = [](const auto& list)
    {
        Json result = Json::array();
        for (auto& b : list)
            result.push_back(
                {{"resource", b.resource}, {"before", Name(b.before)}, {"after", Name(b.after)}});
        return result;
    };
    for (uint32_t position = 0; position < schedule.size(); ++position)
    {
        const auto& p = schedule[position];
        Json activations = Json::array();
        for (const auto& barrier : p.barriers)
            if (resources[barrier.resource].aliasGroup != UINT32_MAX &&
                resources[barrier.resource].first == position)
                activations.push_back(barrier.resource);
        output["schedule"].push_back(
            {{"pass", p.pass},
             {"barriers", barriers(p.barriers)},
             {"aliasActivations", std::move(activations)},
             {"queue", p.queue == rhi::QueueType::Compute ? "compute" : "graphics"},
             {"waitCompute", p.waitCompute}});
    }
    output["prologueBarriers"] = barriers(prologueBarriers);
    output["finalBarriers"] = barriers(finalBarriers);
    return output.dump(2) + "\n";
}
} // namespace renderlab::rendergraph
