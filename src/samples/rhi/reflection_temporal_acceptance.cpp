#include "samples/rhi/reflection_temporal_acceptance.hpp"

#include "core/log.hpp"
#include "renderer/raytracing.hpp"
#include "rhi/commands.hpp"
#include "rhi/runtime.hpp"
#include "shader/dxc_compiler.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace renderlab::samples
{
void VerifyReflectionTemporal(rhi::Runtime& runtime, shader::Target target,
                              const std::filesystem::path& source, Logger& logger)
{
    using namespace rhi;
    constexpr uint32_t width = 9, height = 7, pitch = 256;
    using Pixel = std::array<float, 4>;
    using Image = std::array<Pixel, width * height>;
    shader::DxcCompiler compiler;
    shader::ProgramRequest request;
    request.entry = {ShaderStage::Compute, source, "Temporal"};
    request.target = target;
    if (target == shader::Target::Spirv)
        request.defines["SPIRV"] = "1";
    PipelineDesc desc;
    std::vector<BindingDesc> layout;
    for (uint32_t slot = 0; slot <= 10; ++slot)
        layout.push_back({slot, slot == 3 ? BindingKind::ConstantBuffer
                                : slot == 2 || slot == 9 || slot == 10
                                    ? BindingKind::StorageTexture
                                    : BindingKind::SampledTexture});
    desc.layout = runtime.CreateLayout(layout);
    desc.compute =
        runtime.CreateShader(ShaderStage::Compute, compiler.Compile(request), "Temporal");
    auto pipeline = runtime.CreatePipeline(desc);
    renderer::ReflectionTemporalParameters parameters;
    parameters.inverseViewProjection =
        parameters.previousViewProjection = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    parameters.cameraPosition = parameters.previousCameraPosition = {0, 0, 2, 1};
    parameters.options = {1, 1, 0, 16};
    Image current, previous, world, normal, depth, hit;
    current.fill({2, 2, 2, 1});
    previous.fill({2, 2, 2, 8});
    normal.fill({0, 0, 1, .5f});
    depth.fill({.25f, 0, 0, 0});
    hit.fill({2, 0, 0, 0});
    const auto resetWorld = [&]
    {
        for (uint32_t y = 0; y < height; ++y)
            for (uint32_t x = 0; x < width; ++x)
                world[y * width + x] = {(x + .5f) * 2 / width - 1, 1 - (y + .5f) * 2 / height, .25f,
                                        2};
    };
    resetWorld();
    auto oldNormal = normal;
    const auto run = [&]()
    {
        auto commands = runtime.CreateCommands();
        const auto upload = [&](const Image& values)
        {
            auto image = runtime.CreateTexture(
                {width, height, Format::Rgba32Float, false, false, "Temporal.Input"});
            auto buffer = runtime.CreateBuffer({pitch * height, Memory::Upload, "Temporal.Upload"});
            for (uint32_t y = 0; y < height; ++y)
                std::memcpy(buffer->Map().data() + y * pitch, values.data() + y * width,
                            width * sizeof(Pixel));
            commands->Barrier(buffer, State::HostWrite, State::CopySource);
            commands->Barrier(image, State::Undefined, State::CopyDest);
            commands->CopyTexture(buffer, image, pitch, true);
            commands->Barrier(image, State::CopyDest, State::Sampled);
            return runtime.CreateView(image);
        };
        std::vector<Binding> bindings{
            {0, upload(current)}, {1, upload(previous)}, {4, upload(world)}, {5, upload(oldNormal)},
            {6, upload(hit)},     {7, upload(depth)},    {8, upload(normal)}};
        auto uniform =
            runtime.CreateBuffer({sizeof(parameters), Memory::Upload, "Temporal.Constants"});
        std::memcpy(uniform->Map().data(), &parameters, sizeof(parameters));
        Binding constant{3};
        constant.buffer = uniform;
        constant.size = sizeof(parameters);
        bindings.push_back(constant);
        std::array<std::shared_ptr<Texture>, 3> outputs;
        std::array<std::shared_ptr<Buffer>, 3> readbacks;
        constexpr std::array<uint32_t, 3> slots{2, 9, 10};
        for (size_t i = 0; i < 3; ++i)
        {
            outputs[i] = runtime.CreateTexture(
                {width, height, Format::Rgba32Float, true, false, "Temporal.Output"});
            readbacks[i] =
                runtime.CreateBuffer({pitch * height, Memory::Readback, "Temporal.Readback"});
            commands->Barrier(outputs[i], State::Undefined, State::Storage);
            bindings.push_back({slots[i], runtime.CreateView(outputs[i])});
        }
        commands->Barrier(uniform, State::HostWrite, State::UniformRead);
        commands->Bind(pipeline);
        commands->Bind(runtime.CreateGroup(desc.layout, bindings));
        commands->Compute((width + 7) / 8, (height + 7) / 8);
        for (size_t i = 0; i < 3; ++i)
        {
            commands->Barrier(outputs[i], State::Storage, State::CopySource);
            commands->CopyTexture(readbacks[i], outputs[i], pitch, false);
            commands->Barrier(readbacks[i], State::CopyDest, State::HostRead);
        }
        runtime.Wait(runtime.Submit(std::move(commands)));
        std::array<Image, 3> result;
        for (size_t i = 0; i < 3; ++i)
            for (uint32_t y = 0; y < height; ++y)
                std::memcpy(result[i].data() + y * width, readbacks[i]->Map().data() + y * pitch,
                            width * sizeof(Pixel));
        for (const auto& image : result)
            for (const auto& pixel : image)
                for (float value : pixel)
                    if (!std::isfinite(value))
                        throw std::runtime_error("Non-finite temporal output");
        return result;
    };
    const auto require = [](bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    };
    constexpr uint32_t center = 3 * width + 4;
    auto result = run();
    require(std::abs(result[0][center][3] - 9) < 1e-4f && result[0][center][0] == 2,
            "Temporal valid stationary history/constant preservation failed");
    require(std::abs(result[1][center][2] - .25f) < 1e-5f && result[1][center][3] == 2 &&
                result[2][center][3] == .5f,
            "Temporal geometry guides were not stored");
    for (size_t i = 0; i < current.size(); ++i)
        current[i][0] = current[i][1] = current[i][2] = i % 2 ? 1.f : 3.f;
    result = run();
    require(std::abs(result[0][center][0] - 2) < std::abs(current[center][0] - 2),
            "Temporal history did not reduce synthetic noise");
    parameters.options[0] = 0;
    result = run();
    require(result[0][center][3] == 1 && result[0][center][0] == current[center][0],
            "Temporal reset sampled stale history");
    parameters.options[0] = 1;
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
        {
            world[y * width + x][0] -= 2.f / width;
            previous[y * width + x][3] = float(x + 1);
        }
    parameters.previousViewProjection[3] = 2.f / width;
    require(std::abs(run()[0][center][3] - 7) < 1e-4f,
            "Camera reprojection selected the wrong pixel");
    parameters.previousViewProjection[3] = 1.f / width;
    for (auto& pixel : world)
        pixel[0] += 1.f / width;
    require(std::abs(run()[0][center][3] - 6.5f) < 1e-4f, "Subpixel history interpolation failed");
    parameters.previousViewProjection[3] = 8;
    require(run()[0][center][3] == 1, "Offscreen reprojection retained history");
    parameters.previousViewProjection[3] = 0;
    resetWorld();
    for (auto& pixel : world)
        pixel[2] = .75f;
    require(run()[0][center][3] == 1, "Disoccluded primary depth retained history");
    resetWorld();
    oldNormal.fill({0, 0, -1, .5f});
    require(run()[0][center][3] == 1, "Changed normal retained temporal history");
    oldNormal = normal;
    for (auto& pixel : oldNormal)
        pixel[3] = .8f;
    require(run()[0][center][3] == 1, "Changed roughness retained temporal history");
    oldNormal = normal;
    previous.fill({1000, 1000, 1000, 16});
    result = run();
    require(result[0][center][0] <= 3 && result[0][center][3] <= 16,
            "Temporal highlight clamp or history cap failed");
    parameters.options[1] = 2048;
    result = run();
    require(result[0][center][0] == current[center][0] && result[0][center][3] == 0,
            "High-spp temporal bypass changed the unfiltered reference");
    parameters.options[1] = 1;
    for (auto& pixel : normal)
        pixel[3] = .045f;
    require(run()[0][center][3] == 0, "Sharp mirror temporal bypass failed");
    normal = oldNormal;
    depth.fill({0, 0, 0, 0});
    result = run();
    require(result[0][center][3] == 0 && result[2][center][3] == -1,
            "Background temporal history was not invalidated");
    depth.fill({.25f, 0, 0, 0});
    for (auto& pixel : normal)
        pixel[3] = -1;
    require(run()[0][center][3] == 0, "Unlit temporal history was not invalidated");
    logger.Write(
        LogLevel::Info,
        "Temporal reflection contracts passed: 9x7 extent, constants, "
        "noise reduction, reset, integer/subpixel reprojection, offscreen/depth/normal/roughness "
        "rejection, highlight clipping, bounded count, sharp/high-spp/background/unlit bypass");
}
} // namespace renderlab::samples
