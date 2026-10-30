#include "assets/io.hpp"
#include "core/hash.hpp"
#include "math/math.hpp"
#include "renderer/environment.hpp"
#include "scene/scene.hpp"

#include <nlohmann/json.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace renderlab;
using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
std::string Id(uint32_t value)
{
    return std::format("{:032x}", value);
}
struct Fixture
{
    scene::Scene document;
    std::map<std::string, assets::Model> models;
    explicit Fixture(uint32_t count)
    {
        scene::Object camera;
        camera.id = Id(1);
        camera.camera = scene::Camera{};
        document.activeCamera = camera.id;
        document.objects.push_back(camera);
        assets::Model model;
        model.id = Id(2);
        model.key = "cpu-benchmark-v1";
        assets::Mesh mesh;
        mesh.id = Id(3);
        mesh.bounds = {{-.25f, -.25f, -.25f}, {.25f, .25f, .25f}};
        model.meshes.push_back(mesh);
        model.materials.push_back({Id(4), "Benchmark"});
        model.instances.push_back({Id(5), 0, math::Mat4::Identity()});
        models.emplace(model.id, model);
        for (uint32_t i = 0; i < count; ++i)
        {
            scene::Object object;
            object.id = Id(i + 100);
            object.transform.position = {i % 2 ? 100.f : 0.f, 0, -5};
            object.mesh = scene::MeshRenderer{model.id};
            document.objects.push_back(std::move(object));
        }
    }
};
double Consume(const math::Mat4& value)
{
    double sum = 0;
    for (size_t i = 0; i < value.values.size(); ++i)
    {
        Require(std::isfinite(value.values[i]), "Nonfinite matrix output");
        sum += double(i + 1) * value.values[i];
    }
    return sum;
}
double Consume(const renderer::EnvironmentImage& image)
{
    Require(image.pixels.size() == size_t(image.width) * image.height * 4,
            "Invalid environment output extent");
    double sum = 0;
    for (float value : image.pixels)
    {
        Require(std::isfinite(value) && value >= 0, "Invalid environment output");
        sum += value;
    }
    return sum;
}
template <class F>
void Measure(Json& cases, const std::string& name, uint32_t count, uint32_t samples,
             uint32_t iterations, F&& work)
{
    const auto warmupStart = Clock::now();
    const double expected = work();
    const double warmupMs =
        std::chrono::duration<double, std::milli>(Clock::now() - warmupStart).count();
    Require(std::isfinite(expected), "Nonfinite warmup checksum");
    Json raw = Json::array();
    for (uint32_t sample = 0; sample < samples; ++sample)
    {
        const auto start = Clock::now();
        double checksum = 0;
        for (uint32_t iteration = 0; iteration < iterations; ++iteration)
            checksum += work();
        const auto ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        Require(std::isfinite(checksum) && std::abs(checksum - expected * iterations) <=
                                               1e-8 * std::max(1.0, std::abs(checksum)),
                "Unstable CPU benchmark checksum");
        raw.push_back({{"ms", ms / iterations}, {"checksum", checksum / iterations}});
    }
    cases.push_back({{"name", name},
                     {"elements", count},
                     {"iterationsPerSample", iterations},
                     {"warmupMs", warmupMs},
                     {"samples", raw}});
    std::cout << name << " (" << count << "): " << raw.front().at("ms") << " ms\n";
}
void Run(Json& report, uint32_t samples, bool verify)
{
    auto& cases = report["cases"] = Json::array();
    const uint32_t elements = verify ? 65 : 4096;
    std::vector<math::Mat4> matrices, inverses;
    for (uint32_t i = 0; i < elements; ++i)
    {
        const auto value = math::Compose(
            {float(i % 17), float(i % 5), -float(i % 31)},
            math::Quaternion::EulerDegrees({float(i % 47), float(i % 63), float(i % 19)}),
            {1.f + float(i % 3), 2, i % 2 ? -1.f : 1.f});
        matrices.push_back(value);
        inverses.push_back(math::Inverse(value));
        const auto identity = value * inverses.back();
        for (size_t r = 0; r < 4; ++r)
            for (size_t c = 0; c < 4; ++c)
                Require(std::abs(identity.At(r, c) - (r == c ? 1.f : 0.f)) < 1e-4f,
                        "Matrix benchmark fixture inverse mismatch");
    }
    const auto iterations = verify ? 1u : 16u;
    Measure(cases, "matrix.multiply", elements, samples, iterations,
            [&]
            {
                double sum = 0;
                for (size_t i = 0; i < matrices.size(); ++i)
                    sum += Consume(matrices[i] * inverses[i]);
                return sum;
            });
    Measure(cases, "matrix.inverse", elements, samples, iterations,
            [&]
            {
                double sum = 0;
                for (const auto& matrix : matrices)
                    sum += Consume(math::Inverse(matrix));
                return sum;
            });
    const math::Bounds bounds{{-.5f, -.5f, -.5f}, {.5f, .5f, .5f}};
    const auto projection = math::Perspective(60, 1, .1f, 100);
    Measure(cases, "bounds.transform", elements, samples, iterations,
            [&]
            {
                double sum = 0;
                for (const auto& matrix : matrices)
                {
                    const auto value = math::TransformBounds(matrix, bounds);
                    Require(value.minimum.x <= value.maximum.x, "Invalid transformed bounds");
                    sum += value.minimum.x + value.maximum.y + value.maximum.z;
                }
                return sum;
            });
    std::vector<math::Bounds> worldBounds;
    for (const auto& matrix : matrices)
        worldBounds.push_back(math::TransformBounds(matrix, bounds));
    Measure(cases, "bounds.visible", elements, samples, iterations,
            [&]
            {
                double sum = 0;
                for (const auto& value : worldBounds)
                    sum += math::Visible(projection, value) ? 1 : 0;
                return sum;
            });
    for (uint32_t count : {64u, 1024u, 4096u})
    {
        if (verify && count != 64)
            continue;
        Fixture fixture(count);
        scene::Extractor extractor;
        const auto extract = [&](bool culling)
        {
            const auto snapshot = extractor.Extract(fixture.document, 1, fixture.models, culling);
            Require(snapshot.totalInstances == count && snapshot.allInstances.size() == count &&
                        snapshot.instances.size() == (culling ? count / 2 : 0),
                    "Scene benchmark fixture visibility mismatch");
            return double(snapshot.totalInstances + snapshot.instances.size());
        };
        Measure(cases, "scene.extract.cpu", count, samples, verify ? 1 : 4,
                [&] { return extract(true); });
        Measure(cases, "scene.extract.gpu", count, samples, verify ? 1 : 4,
                [&] { return extract(false); });
    }
    assets::Image image{32, 16};
    image.rgba.resize(size_t(image.width) * image.height * 4);
    for (size_t i = 0; i < image.rgba.size(); ++i)
        image.rgba[i] = uint8_t((i * 37 + i / 4) % 256);
    Measure(cases, "environment.prefilter",
            renderer::EnvironmentWidth * renderer::EnvironmentHeight, samples, 1,
            [&] { return Consume(renderer::PrefilterEnvironment(&image, {})); });
    Measure(cases, "environment.brdf", 64 * 64, samples, 1,
            [&] { return Consume(renderer::IntegrateEnvironmentBrdf()); });
}
} // namespace
int wmain(int argc, wchar_t** argv)
{
    try
    {
        const bool verify = argc == 2 && std::wstring_view(argv[1]) == L"--verify";
        Require(verify || argc == 2 || argc == 3,
                "Usage: renderlab_cpu_benchmark OUTPUT.json [SAMPLES] | --verify");
        const auto samples = verify ? 1u : argc == 3 ? std::stoul(argv[2]) : 5u;
        Require(samples >= 1 && samples <= 100, "Samples must be in [1,100]");
        Json report{{"schemaVersion", 1},
                    {"scope", "single-thread CPU kernels including checksum validation; not FPS"},
                    {"sourceIdentity", "runtime source hashes; executable hash identifies binary"},
                    {"compiler", _MSC_FULL_VER},
                    {"samplesPerCase", samples}};
#ifdef NDEBUG
        report["configuration"] = "Release";
#else
        report["configuration"] = "Debug";
#endif
        SYSTEM_INFO system{};
        GetNativeSystemInfo(&system);
        report["logicalProcessors"] = system.dwNumberOfProcessors;
        wchar_t executable[32768]{};
        const auto length = GetModuleFileNameW(nullptr, executable, DWORD(std::size(executable)));
        Require(length > 0 && length < std::size(executable), "Cannot identify executable");
        report["executableSha256"] = Sha256(assets::detail::Read(executable));
        for (const auto* source :
             {"src/math/math.cpp", "src/math/math.hpp", "src/scene/scene.cpp",
              "src/scene/scene.hpp", "src/renderer/environment.cpp", "src/renderer/environment.hpp",
              "src/tools/cpu_benchmark/main.cpp", "CMakeLists.txt", "cmake/Dependencies.cmake"})
            report["sourceSha256"][source] =
                Sha256(assets::detail::Read(std::filesystem::path(RENDERLAB_SOURCE_ROOT) / source));
        Run(report, static_cast<uint32_t>(samples), verify);
        if (!verify)
        {
            Require(!std::filesystem::exists(argv[1]), "Refusing to overwrite benchmark report");
            const auto text = report.dump(2) + "\n";
            WriteFileAtomically(argv[1], std::as_bytes(std::span(text.data(), text.size())));
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
