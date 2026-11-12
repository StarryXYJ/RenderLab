#include "core/log.hpp"
#include "renderer/raytracing.hpp"
#include "renderer/scene_renderer.hpp"
#include "rhi/device.hpp"
#include "rhi/runtime.hpp"

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
    renderer::SceneRenderOptions options;
    options.pipeline = renderer::PipelineType::PathTracing;
    options.reflectionDenoise = true;
    options.reflectionTemporal = true;
    auto targetImage = runtime.CreateTexture(
        {64, 64, rhi::Format::Rgba8Srgb, false, true, "AreaLight.Validation"});
    auto state = rhi::State::Undefined;
    const auto frame = [&]()
    {
        auto result = renderer.Render(targetImage, state, nullptr, false, {}, options);
        state = rhi::State::CopySource;
        runtime.Wait(result.completion);
        return result;
    };
    const auto first = frame();
    const auto second = frame();
    Require(first.reflectionHistoryReset && !second.reflectionHistoryReset &&
                second.reflectionSamples == 2 * renderer::PathSamplesPerFrame,
            "Initial no-light history did not accumulate");
    scene::Object light;
    light.id = "3e6f5f4af2cf4c7fa81ea84cd230b6fa";
    light.areaLight = scene::RectAreaLight{};
    light.transform.position = {0, 3, 0};
    renderer.Document().objects.push_back(light);
    auto result = frame();
    Require(result.reflectionHistoryReset && result.reflectionTemporalFrames == 1,
            "Adding light did not invalidate path and temporal history");
    result = frame();
    Require(!result.reflectionHistoryReset && result.reflectionTemporalFrames > 1,
            "Stable light did not reuse history");
    renderer.Document().Get(light.id).areaLight->radiance.x = 2;
    result = frame();
    Require(result.reflectionHistoryReset && result.reflectionTemporalFrames == 1,
            "Changing light radiance did not invalidate histories");
    renderer.Document().Get(light.id).transform.position.x = 1;
    result = frame();
    Require(result.reflectionHistoryReset && result.reflectionTemporalFrames == 1,
            "Moving light did not invalidate histories");
    renderer.Document().Get(light.id).areaLight->width = 2;
    result = frame();
    Require(result.reflectionHistoryReset && result.reflectionTemporalFrames == 1,
            "Resizing light did not invalidate histories");
    renderer.Document().Get(light.id).areaLight->enabled = false;
    result = frame();
    Require(result.reflectionHistoryReset && result.reflectionTemporalFrames == 1,
            "Disabling light did not invalidate histories");
    renderer.Document().Get(light.id).areaLight->enabled = true;
    scene::Object point;
    point.id = "6e6f5f4af2cf4c7fa81ea84cd230b6fa";
    point.pointLight = scene::PointLight{};
    point.transform.position = {0, 2, 0};
    renderer.Document().objects.push_back(point);
    result = frame();
    Require(result.reflectionHistoryReset && result.reflectionTemporalFrames == 1,
            "Adding point light did not invalidate histories");
    result = frame();
    Require(!result.reflectionHistoryReset && result.reflectionTemporalFrames > 1,
            "Stable point light did not reuse history");
    renderer.Document().Get(point.id).pointLight->intensity = 2;
    result = frame();
    Require(result.reflectionHistoryReset && result.reflectionTemporalFrames == 1,
            "Changing point light did not invalidate histories");
    renderer.Document().Get(point.id).transform.position.x = 1;
    result = frame();
    Require(result.reflectionHistoryReset && result.reflectionTemporalFrames == 1,
            "Moving point light did not invalidate histories");
    renderer.Document().Get(point.id).pointLight->enabled = false;
    result = frame();
    Require(result.reflectionHistoryReset && result.reflectionTemporalFrames == 1,
            "Disabling point light did not invalidate histories");
    for (int i = 0; i < 2; ++i)
    {
        auto extra = light;
        extra.id = i == 0 ? "4e6f5f4af2cf4c7fa81ea84cd230b6fa" : "5e6f5f4af2cf4c7fa81ea84cd230b6fa";
        renderer.Document().objects.push_back(extra);
    }
    bool rejected = false;
    try
    {
        renderer.ValidatePipeline(renderer::PipelineType::PathTracing);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Require(rejected, "Excess area-light count was not rejected by preflight");
    rejected = false;
    try
    {
        (void)frame();
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Require(rejected, "Excess area-light count was not rejected by render");
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
        Logger logger(std::filesystem::path(argv[4]) / "area-light-history.log");
        rhi::DeviceOptions options;
        options.backend = backend == "dx12" ? rhi::Backend::Dx12 : rhi::Backend::Vulkan;
        options.validation = rhi::ValidationMode::On;
        auto device = rhi::CreateDevice(options, logger);
        {
            auto runtime = device->CreateRuntime();
            Check(*runtime, backend == "dx12" ? shader::Target::Dxil : shader::Target::Spirv,
                  argv[2], argv[3]);
            runtime->WaitIdle();
            Require(runtime->GetMemoryStats().used == 0, "Area-light history leaked resources");
        }
        device->PollDiagnostics();
        device->Shutdown();
        Require(device->GetValidationErrorCount() == 0, "Area-light history validation errors");
        std::cout << "Area-light history contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
