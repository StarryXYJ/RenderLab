#include "core/log.hpp"
#include "project/project.hpp"
#include "renderer/project_module.hpp"
#include "renderer/scene_renderer.hpp"
#include "rhi/device.hpp"
#include "rhi/runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
using namespace renderlab;
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void Fails(F work, const std::string& text)
{
    try
    {
        work();
    }
    catch (const std::exception& failure)
    {
        Require(std::string(failure.what()).find(text) != std::string::npos, failure.what());
        return;
    }
    throw std::runtime_error("Expected failure: " + text);
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 6, "Expected backend, module, project, shaders, output");
        const auto modulePath = std::filesystem::absolute(argv[2]);
        const auto source = std::filesystem::path(argv[3]);
        const auto output = std::filesystem::path(argv[5]);
        const auto root = output / project::NewId();
        std::filesystem::create_directories(root);
        std::filesystem::copy(source.parent_path() / "Assets", root / "Assets",
                              std::filesystem::copy_options::recursive);
        std::filesystem::copy_file(source, root / "project.rlproject");
        Logger logger(output / "project-render.log");
        rhi::DeviceOptions deviceOptions;
        deviceOptions.backend =
            std::string(argv[1]) == "dx12" ? rhi::Backend::Dx12 : rhi::Backend::Vulkan;
        deviceOptions.validation = rhi::ValidationMode::On;
        auto device = rhi::CreateDevice(deviceOptions, logger);
        {
            auto runtime = device->CreateRuntime();
            runtime->SetAllocationBudget(1024ull * 1024 * 1024);
            {
                auto modules = std::make_shared<renderer::ProjectModules>(
                    std::vector<std::filesystem::path>{modulePath});
                renderer::SceneRenderer renderer(*runtime,
                                                 deviceOptions.backend == rhi::Backend::Dx12
                                                     ? shader::Target::Dxil
                                                     : shader::Target::Spirv,
                                                 root / "project.rlproject", argv[4], {}, modules);
                modules.reset();
                Require(GetModuleHandleW(modulePath.filename().c_str()) != nullptr,
                        "Renderer did not retain its project module");
                auto color = runtime->CreateTexture({40, 40, rhi::Format::Rgba8Srgb, false, true});
                renderer::SceneRenderOptions settings;
                settings.pipeline = renderer::PipelineType::Forward;
                settings.projectPipeline = "test.blue";
                settings.sceneHdrDiagnostics = true;
                settings.captureGraph = true;
                renderer.ValidateOptions(settings);
                const auto frame =
                    renderer.Render(color, rhi::State::Undefined, nullptr, false, {}, settings);
                runtime->Wait(frame.completion);
                Require(frame.sceneHdr && frame.graphSnapshot,
                        "Missing project graph or HDR readback");
                Require(
                    std::ranges::any_of(frame.graphSnapshot->passes, [](const auto& pass)
                                        { return pass.name == "Project.BlueClear" && pass.live; }),
                    "Project node is absent from the submitted graph");
                const auto& plan = *frame.graphSnapshot;
                Require(std::ranges::any_of(plan.edges,
                                            [&](const auto& edge)
                                            {
                                                return plan.passes[edge.before].name ==
                                                           "Project.BlueClear" &&
                                                       plan.passes[edge.after].name ==
                                                           "Project.Marker";
                                            }),
                        "Project nodes have no explicit ordering edge");
                std::array<float, 4> pixel{};
                std::memcpy(pixel.data(), frame.sceneHdr->Map().data(), sizeof(pixel));
                Require(std::abs(pixel[0]) < 1e-5f && std::abs(pixel[1] - .25f) < 1e-5f &&
                            std::abs(pixel[2] - .75f) < 1e-5f,
                        "Project node did not change rendered HDR output");
                settings.projectPipeline = "test.failure";
                Fails(
                    [&]
                    {
                        renderer.Render(color, rhi::State::CopySource, nullptr, false, {},
                                        settings);
                    },
                    "intentional node failure");
                settings.projectPipeline = "test.missing";
                Fails([&] { renderer.ValidateOptions(settings); }, "Unknown project pipeline");
                settings.projectPipeline.clear();
                auto recovered =
                    renderer.Render(color, rhi::State::CopySource, nullptr, false, {}, settings);
                runtime->Wait(recovered.completion);
                runtime->WaitIdle();
            }
            Require(GetModuleHandleW(modulePath.filename().c_str()) == nullptr,
                    "Project module was not unloaded after renderer retirement");
            {
                auto modules = std::make_shared<renderer::ProjectModules>(
                    std::vector<std::filesystem::path>{modulePath});
                std::weak_ptr<const renderer::ProjectModules> retained = modules;
                auto renderer = std::make_unique<renderer::SceneRenderer>(
                    *runtime,
                    deviceOptions.backend == rhi::Backend::Dx12 ? shader::Target::Dxil
                                                                : shader::Target::Spirv,
                    root / "project.rlproject", argv[4], "", std::move(modules));
                auto color = runtime->CreateTexture({40, 40, rhi::Format::Rgba8Srgb, false, true});
                renderer::SceneRenderOptions settings;
                settings.projectPipeline = "test.blue";
                auto frame =
                    renderer->Render(color, rhi::State::Undefined, nullptr, false, {}, settings);
                settings.projectPipeline = "test.failure";
                Fails(
                    [&]
                    {
                        renderer->Render(color, rhi::State::CopySource, nullptr, false, {},
                                         settings);
                    },
                    "intentional node failure");
                renderer.reset();
                if (runtime->Completed() < frame.completion)
                    Require(!retained.expired(), "In-flight submission lost its module");
                runtime->Wait(frame.completion);
                Require(retained.expired(), "Retired submission retained its module");
            }
            runtime->WaitIdle();
            runtime->TrimMemory();
            Require(runtime->GetMemoryStats().used == 0, "Project render leaked GPU resources");
        }
        device->PollDiagnostics();
        device->Shutdown();
        Require(device->GetValidationErrorCount() == 0,
                "Project render produced validation errors");
        std::cout << "Project render contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
