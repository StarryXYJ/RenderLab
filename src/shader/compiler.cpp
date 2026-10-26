#include "shader/compiler.hpp"

#include "core/file_io.hpp"
#include "core/hash.hpp"
#include "shader/dxc_compiler.hpp"

#include <nlohmann/json.hpp>

#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

namespace renderlab::shader
{
namespace
{
using Json = nlohmann::json;
constexpr const char* GeneratorVersion = "renderlab-declarations-3-static-axes;sm6.0;-HV2021;-Ges;-"
                                         "WX;-O3;-Zpc;vulkan1.3;-fvk-use-dx-layout";
std::vector<std::byte> ReadBinary(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() <= 0 || file.tellg() > 16 * 1024 * 1024)
        throw std::runtime_error("Missing/invalid shader cache binary");
    std::vector<std::byte> bytes(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file)
        throw std::runtime_error("Truncated shader cache binary");
    return bytes;
}
struct Job
{
    ProgramRequest request;
    Program program;
    size_t interfaceIndex;
};
} // namespace
BuildResult CompileShader(const std::filesystem::path& source, const CompileOptions& options)
{
    try
    {
        if (std::set<Target>(options.targets.begin(), options.targets.end()).size() !=
            options.targets.size())
            throw std::invalid_argument("Duplicate shader targets");
        for (const auto target : options.targets)
            if (target != Target::Dxil && target != Target::Spirv)
                throw std::invalid_argument("Unsupported shader target");
        if (options.targets.empty())
            throw std::runtime_error("Shader build needs a target");
        auto package = std::make_shared<Package>();
        package->generation = options.generation;
        package->definition = ParseDefinition(source, options.includeDirectories);
        DxcCompiler dxc;
        std::string key = std::string(GeneratorVersion) + dxc.Version();
        std::set<std::filesystem::path> dependencies;
        for (const auto& dependency : package->definition.declarations)
        {
            key += ToUtf8(dependency.path) + Sha256(dependency.content);
            dependencies.insert(dependency.path);
        }
        std::vector<Job> jobs;
        for (const auto& pass : package->definition.passes)
        {
            auto interface = BuildInterface(package->definition, pass);
            const auto index = package->interfaces.size();
            package->interfaces.push_back(interface);
            for (auto target : options.targets)
                for (const auto& variant : EnumerateVariants(package->definition, pass))
                    for (const auto& entry : pass.entries)
                    {
                        Job job;
                        job.interfaceIndex = index;
                        job.program = {pass.name, variant.name, entry.function, entry.stage, target,
                                       {},        {},           variant.axes};
                        job.request = {entry,
                                       target,
                                       variant.defines,
                                       options.includeDirectories,
                                       GenerateHeader(interface, target),
                                       ReadText(entry.source),
                                       GenerateTypesHeader(interface)};
                        auto preprocessed = dxc.Preprocess(job.request);
                        // The exact preprocessed snapshot is compiled; concurrent source edits
                        // cannot poison its key.
                        job.request.sourceSnapshot = std::move(preprocessed.text);
                        key += pass.name + variant.name + entry.function +
                               std::to_string(int(target)) + std::to_string(int(entry.stage)) +
                               Sha256(job.request.generated) + Sha256(job.request.generatedTypes) +
                               Sha256(job.request.sourceSnapshot);
                        for (const auto& [name, value] : variant.defines)
                            key += name + "=" + value + ";";
                        for (const auto& dependency : preprocessed.dependencies)
                        {
                            key += ToUtf8(dependency.path) + Sha256(dependency.content);
                            dependencies.insert(std::filesystem::weakly_canonical(dependency.path));
                        }
                        jobs.push_back(std::move(job));
                    }
        }
        package->key = Sha256(key);
        package->dependencies.assign(dependencies.begin(), dependencies.end());
        const auto directory = options.cacheDirectory / package->key;
        Json manifest;
        bool hit = options.useCache && !options.cacheDirectory.empty();
        if (hit)
        {
            try
            {
                manifest = Json::parse(ReadText(directory / "manifest.json"));
                hit = manifest.at("key") == package->key &&
                      manifest.at("programs").size() == jobs.size();
            }
            catch (const std::exception&)
            {
                hit = false;
            }
        }
        if (hit)
        {
            try
            {
                for (size_t i = 0; i < jobs.size(); ++i)
                {
                    jobs[i].program.code = ReadBinary(directory / (std::to_string(i) + ".bin"));
                    if (Sha256(jobs[i].program.code) !=
                        manifest.at("programs")[i].get<std::string>())
                        throw std::runtime_error("Cache checksum mismatch");
                }
            }
            catch (const std::exception&)
            {
                hit = false;
            }
        }
        Json hashes = Json::array();
        for (auto& job : jobs)
        {
            if (!hit)
                job.program.code = dxc.Compile(job.request);
            job.program.reflection = job.program.target == Target::Dxil
                                         ? ReflectDxil(job.program.code)
                                         : ReflectSpirv(job.program.code);
            try
            {
                ValidateReflection(package->interfaces[job.interfaceIndex], job.program.reflection);
            }
            catch (const std::exception& error)
            {
                throw std::runtime_error(job.program.pass + "/" + job.program.variant + "/" +
                                         std::to_string(int(job.program.target)) + ": " +
                                         error.what());
            }
            hashes.push_back(Sha256(job.program.code));
            package->programs.push_back(std::move(job.program));
        }
        if (!hit && options.useCache && !options.cacheDirectory.empty())
        {
            std::filesystem::create_directories(directory);
            for (size_t i = 0; i < package->programs.size(); ++i)
                WriteFileAtomically(directory / (std::to_string(i) + ".bin"),
                                    package->programs[i].code);
            const auto text =
                Json{{"key", package->key}, {"generator", GeneratorVersion}, {"programs", hashes}}
                    .dump(2) +
                "\n";
            WriteFileAtomically(directory / "manifest.json",
                                std::as_bytes(std::span{text.data(), text.size()}));
        }
        package->cacheHit = hit;
        return {std::move(package), {}};
    }
    catch (const std::exception& error)
    {
        return {{}, error.what()};
    }
}
} // namespace renderlab::shader
