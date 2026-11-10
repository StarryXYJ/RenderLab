#include "core/log.hpp"
#include "renderer/raytracing.hpp"
#include "renderer/scene_renderer.hpp"
#include "rhi/device.hpp"
#include "rhi/runtime.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace renderlab;
void Require(bool ok, const char* message)
{
    if (!ok)
        throw std::runtime_error(message);
}
void Check(rhi::Runtime& runtime, shader::Target target, const std::filesystem::path& project,
           const std::filesystem::path& shaders)
{
    renderer::SceneRenderer renderer(runtime, target, project, shaders);
    auto& doc = renderer.Document();
    const auto it =
        std::ranges::find_if(doc.objects, [](const auto& o) { return o.areaLight.has_value(); });
    Require(it != doc.objects.end(), "Missing area light");
    const auto id = it->id;
    renderer::SceneRenderOptions options;
    options.pipeline = renderer::PipelineType::RayTracing;
    options.reflectionDenoise = false;
    options.reflectionTemporal = false;
    options.view = renderer::SceneView::ReflectionRaw;
    auto targetImage =
        runtime.CreateTexture({80, 80, rhi::Format::Rgba8Srgb, false, true, "AreaReflection.Test"});
    auto state = rhi::State::Undefined;
    const auto capture = [&]()
    {
        auto result = renderer.Render(targetImage, state, nullptr, true, {}, options);
        state = rhi::State::CopySource;
        runtime.Wait(result.completion);
        auto pixels = result.pixels->Map();
        uint64_t sum = 0;
        for (uint32_t y = 0; y < 80; ++y)
            for (uint32_t x = 0; x < 80; ++x)
                for (uint32_t c = 0; c < 3; ++c)
                    sum +=
                        std::to_integer<uint8_t>(pixels[y * rhi::TextureRowPitch(80) + x * 4 + c]);
        return sum;
    };
    doc.Get(id).areaLight->radiance = {50, 50, 50};
    const auto lit = capture();
    doc.Get(id).areaLight->radiance = {};
    const auto dark = capture();
    std::cout << "Reflection display sum: lit=" << lit << " dark=" << dark << '\n';
    Require(lit > dark + 100, "Reflections did not see area emitter");
    doc.Get(id).areaLight->radiance = {50, 50, 50};
    doc.Get(id).transform.rotation = {.70710678f, 0, 0, .70710678f};
    const auto back = capture();
    std::cout << "Reflection back=" << back << '\n';
    Require(back < lit * 4 / 10, "Reversing area-light orientation failed to change reflection energy");
    doc.Get(id).transform.rotation = {-.70710678f, 0, 0, .70710678f};
    scene::Object blocker;
    blocker.id = "e1f0a5640d4d41f39dd49fa3b4e96abb";
    blocker.transform.position = {0, 2.84f, -.1f};
    blocker.transform.scale = {1.5f, .04f, 1.5f};
    blocker.mesh = scene::MeshRenderer{"b4748e75e72a5dba8d4439fa86854c92"};
    doc.objects.push_back(blocker);
    const auto blocked = capture();
    std::cout << "Reflection back=" << back << " blocked=" << blocked << '\n';
    Require(blocked < lit / 5, "Geometry failed to occlude reflected area emitter");
    doc.objects.pop_back();
    options.view = renderer::SceneView::Final;
    options.reflectionTemporal = true;
    options.reflectionDenoise = true;
    const auto frame = [&]()
    {
        auto result = renderer.Render(targetImage, state, nullptr, false, {}, options);
        state = rhi::State::CopySource;
        runtime.Wait(result.completion);
        return result;
    };
    (void)frame();
    auto stable = frame();
    std::cout << "Reflection stable: reset=" << stable.reflectionHistoryReset
              << " samples=" << stable.reflectionSamples
              << " temporal=" << stable.reflectionTemporalFrames << '\n';
    Require(!stable.reflectionHistoryReset && stable.reflectionSamples >= 2,
            "Reflection history did not accumulate");
    doc.Get(id).transform.position.x += .2f;
    auto moved = frame();
    Require(moved.reflectionHistoryReset && moved.reflectionTemporalFrames == 1,
            "Moving area light failed to invalidate reflection histories");
    doc.Get(id).areaLight->radiance = {2, 1, .5f};
    auto changed = frame();
    Require(changed.reflectionHistoryReset && changed.reflectionTemporalFrames == 1,
            "Area-light radiance failed to invalidate reflection histories");
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 5, "Expected backend, project, shader directory and output");
        const std::string backend = argv[1];
        Require(backend == "dx12" || backend == "vulkan", "Invalid backend");
        std::filesystem::create_directories(argv[4]);
        Logger logger(std::filesystem::path(argv[4]) / "area-light-reflection.log");
        rhi::DeviceOptions options;
        options.backend = backend == "dx12" ? rhi::Backend::Dx12 : rhi::Backend::Vulkan;
        options.validation = rhi::ValidationMode::On;
        auto device = rhi::CreateDevice(options, logger);
        {
            auto runtime = device->CreateRuntime();
            Check(*runtime, backend == "dx12" ? shader::Target::Dxil : shader::Target::Spirv,
                  argv[2], argv[3]);
            runtime->WaitIdle();
            Require(runtime->GetMemoryStats().used == 0, "Area-light reflection leaked resources");
        }
        device->PollDiagnostics();
        device->Shutdown();
        Require(device->GetValidationErrorCount() == 0, "Area-light reflection validation errors");
        std::cout << "Area-light reflection contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
