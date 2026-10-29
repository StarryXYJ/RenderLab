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
    Json output{{"queue", "graphics"},
                {"recording", recordingThreads == 1 ? "serial" : "parallel-native"},
                {"recordingThreads", recordingThreads},
                {"recordingChunks", recordingChunks},
                {"aliasing", false}};
    output["passes"] = Json::array();
    output["resources"] = Json::array();
    output["edges"] = Json::array();
    output["schedule"] = Json::array();
    for (const auto& p : passes)
    {
        Json info{{"name", p.name}, {"kind", Name(p.kind)}, {"source", p.source},
                  {"live", p.live}, {"reason", p.reason},   {"accesses", Json::array()}};
        for (auto& a : p.accesses)
            info["accesses"].push_back({{"resource", a.resource},
                                        {"inputVersion", a.inputVersion},
                                        {"outputVersion", a.outputVersion},
                                        {"use", int(a.use)},
                                        {"read", a.read},
                                        {"write", a.write},
                                        {"bufferOffset", a.bufferRange.offset},
                                        {"bufferSize", a.bufferRange.size}});
        output["passes"].push_back(std::move(info));
    }
    output["cpuNs"] = {{"compile", compileNs}, {"prepare", prepareNs}, {"record", recordNs}};
    for (const auto& r : resources)
        output["resources"].push_back(
            {{"name", r.name},
             {"imported", r.imported},
             {"first", r.first == UINT32_MAX ? Json(nullptr) : Json(r.first)},
             {"last", r.last},
             {"estimatedBytes", r.estimatedBytes},
             {"versions", r.versions},
             {"initial", Name(r.initial)},
             {"final", Name(r.final)}});
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
    for (const auto& p : schedule)
        output["schedule"].push_back({{"pass", p.pass}, {"barriers", barriers(p.barriers)}});
    output["finalBarriers"] = barriers(finalBarriers);
    return output.dump(2) + "\n";
}
} // namespace renderlab::rendergraph
