#include "core/log.hpp"
#include "rendergraph/graph.hpp"
#include "rendergraph/pool.hpp"
#include "rhi/commands.hpp"
#include "rhi/device.hpp"
#include "rhi/runtime.hpp"
#include "rhi/upload.hpp"
#include "shader/compiler.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>

namespace
{
using namespace renderlab;
using namespace rhi;
using namespace rendergraph;
void Require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}
struct Step
{
    PipelineDesc pipeline;
    std::array<float, 4> color = {1, 0, 0, 1};
    float z = .5f;
    std::optional<uint32_t> reference = 0;
    bool reverse = false;
    LoadOp depthLoad = LoadOp::Load, stencilLoad = LoadOp::Load;
};
struct Vertex
{
    float x, y, z;
    std::array<float, 4> color;
};
class Fixture
{
public:
    Fixture(Runtime& runtime, shader::Target target, const std::filesystem::path& output)
        : runtime_(runtime), pool_(runtime)
    {
        std::ofstream(output / "state.shader") << R"(shader "Test/State"
{
    pass Test
    {
        vertex = "state.hlsl:VSMain";
        pixel = "state.hlsl:PSMain";
        depth = false;
    }
})";
        std::ofstream(output / "state.hlsl") << R"(
struct V { float3 position : POSITION; float4 color : COLOR; };
struct P { float4 position : SV_Position; float4 color : COLOR; };
P VSMain(V v) { P p; p.position = float4(v.position, 1); p.color = v.color; return p; }
float4 PSMain(P p) : SV_Target { return p.color; }
)";
        shader::CompileOptions options;
        options.targets = {target};
        options.cacheDirectory = output / "cache";
        const auto result = shader::CompileShader(output / "state.shader", options);
        if (!result)
            throw std::runtime_error(result.diagnostics);
        base.layout = runtime.CreateLayout({});
        for (const auto stage : {ShaderStage::Vertex, ShaderStage::Pixel})
        {
            const auto& program = result.package->Find("Test", "Default", stage, target);
            auto module = runtime.CreateShader(stage, program.code, program.entry);
            (stage == ShaderStage::Vertex ? base.vertex : base.pixel) = std::move(module);
        }
        base.color = Format::Rgba32Float;
        base.depthStencilFormat = Format::D32S8;
        base.vertexStride = sizeof(Vertex);
        base.attributes = {{"POSITION", 0, VertexFormat::Float3, 0},
                           {"COLOR", 1, VertexFormat::Float4, 12}};
        base.raster.cull = CullMode::None;
    }
    std::array<float, 4> Render(const std::vector<Step>& steps)
    {
        std::vector<Vertex> data;
        for (const auto& step : steps)
        {
            std::array<Vertex, 3> triangle = {{{-1, -1, step.z, step.color},
                                               {3, -1, step.z, step.color},
                                               {-1, 3, step.z, step.color}}};
            if (step.reverse)
                std::swap(triangle[1], triangle[2]);
            data.insert(data.end(), triangle.begin(), triangle.end());
        }
        const auto size = data.size() * sizeof(Vertex);
        auto upload = runtime_.CreateBuffer({size, Memory::Upload, "State.Upload"});
        auto vertices = runtime_.CreateBuffer({size, Memory::Device, "State.Vertices"});
        std::memcpy(upload->Map().data(), data.data(), size);
        auto commands = runtime_.CreateCommands();
        commands->Barrier(upload, State::HostWrite, State::CopySource);
        commands->Barrier(vertices, State::Undefined, State::CopyDest);
        commands->Copy(upload, vertices, size);
        commands->Barrier(vertices, State::CopyDest, State::Vertex);
        runtime_.Wait(runtime_.Submit(std::move(commands)));
        Graph graph;
        auto color = graph.CreateTexture("Color", {16, 16, Format::Rgba32Float, false, true});
        auto depth = graph.CreateTexture("Depth", {16, 16, Format::D32S8, false, true});
        auto vertex = graph.ImportBuffer("Vertices", vertices, {State::Vertex, true});
        auto init = graph.AddRasterPass("Initialize");
        color = init.ColorTarget(color, LoadOp::Clear, {.2f, .4f, .6f, .5f});
        depth = init.DepthTarget(depth);
        init.Execute([](RasterContext&) {});
        for (size_t i = 0; i < steps.size(); ++i)
        {
            const auto& step = steps[i];
            auto pipeline = runtime_.CreatePipeline(step.pipeline);
            auto pass = graph.AddRasterPass("Draw" + std::to_string(i));
            color = pass.ColorTarget(color, LoadOp::Load);
            const auto depthAccess =
                step.pipeline.depth.writeEnabled || step.depthLoad == LoadOp::Clear
                    ? AttachmentAccess::ReadWrite
                    : AttachmentAccess::ReadOnly;
            const auto stencilAccess =
                step.pipeline.stencil.Writes() || step.stencilLoad == LoadOp::Clear
                    ? AttachmentAccess::ReadWrite
                    : AttachmentAccess::ReadOnly;
            depth = pass.DepthStencilTarget(depth, {step.depthLoad, depthAccess},
                                            {step.stencilLoad, stencilAccess});
            pass.ReadBuffer(vertex, Use::Vertex);
            pass.Execute(
                [pipeline, vertex, first = static_cast<uint32_t>(i * 3),
                 reference = step.reference](RasterContext& context)
                {
                    context.Bind(pipeline);
                    if (reference)
                        context.StencilReference(*reference);
                    context.Geometry(vertex);
                    context.Draw(3, first);
                });
        }
        auto readback = runtime_.CreateBuffer({4096, Memory::Readback, "State.Readback"});
        auto buffer = graph.ImportBuffer("Readback", readback, {State::CopyDest, false});
        auto copy = graph.AddCopyPass("Readback");
        copy.ReadTexture(color);
        buffer = copy.WriteBuffer(buffer);
        copy.Execute([buffer, color](CopyContext& context)
                     { context.CopyTexture(buffer, color, 256, false); });
        graph.Export(buffer, State::HostRead);
        graph.Compile();
        runtime_.Wait(graph.Execute(pool_).completion);
        std::array<float, 4> result;
        std::memcpy(result.data(), readback->Map().data() + 8 * 256 + 8 * 16, 16);
        return result;
    }
    PipelineDesc base;

private:
    Runtime& runtime_;
    Pool pool_;
};
void Pixel(const std::array<float, 4>& actual, const std::array<float, 4>& expected,
           const char* message)
{
    for (size_t i = 0; i < 4; ++i)
        if (!std::isfinite(actual[i]) || std::abs(actual[i] - expected[i]) > .0001f)
            throw std::runtime_error(std::string(message) + ": channel " + std::to_string(i) +
                                     " expected " + std::to_string(expected[i]) + " got " +
                                     std::to_string(actual[i]));
}
void Gpu(Runtime& runtime, shader::Target target, const std::filesystem::path& output)
{
    Fixture f(runtime, target, output);
    Step write{f.base}, read{f.base};
    write.pipeline.depth = {false, true};
    write.z = .8f;
    read.pipeline.depth = {true, false, CompareOp::GreaterEqual};
    read.z = .2f;
    read.color = {0, 1, 0, 1};
    Pixel(f.Render({write, read}), {1, 0, 0, 1}, "No-test depth write must use Always");
    write.pipeline.depth.writeEnabled = false;
    Pixel(f.Render({write, read}), read.color, "Disabled depth writes must preserve clear depth");

    Step mask{f.base}, test{f.base};
    mask.pipeline.raster.colorWriteMask = 0;
    mask.pipeline.stencil.enabled = true;
    mask.pipeline.stencil.front.pass = mask.pipeline.stencil.back.pass = StencilOp::Replace;
    mask.reference = 7;
    test.pipeline.stencil.enabled = true;
    test.pipeline.stencil.writeMask = 0;
    test.pipeline.stencil.front.compare = test.pipeline.stencil.back.compare = CompareOp::Equal;
    test.reference = 7;
    test.color = {0, 1, 0, 1};
    Pixel(f.Render({mask, test}), test.color, "Stencil must survive across graph passes");
    test.depthLoad = LoadOp::Clear;
    Pixel(f.Render({mask, test}), test.color, "Depth clear must preserve loaded stencil");
    test.depthLoad = LoadOp::Load;
    test.reference.reset();
    Pixel(f.Render({mask, test}), {.2f, .4f, .6f, .5f},
          "Pipeline bind must reset stencil reference");
    test.reference = 7;
    test.stencilLoad = LoadOp::Clear;
    Pixel(f.Render({mask, test}), {.2f, .4f, .6f, .5f}, "Stencil clear must reset the mask");
    test.stencilLoad = LoadOp::Load;
    mask.pipeline.stencil.front.pass = StencilOp::Keep;
    mask.reverse = false;
    const auto front = f.Render({mask, test});
    mask.reverse = true;
    const auto back = f.Render({mask, test});
    Require(front != back, "Front/back stencil operations must follow winding");

    mask.pipeline.depth = {true, false, CompareOp::Never};
    mask.pipeline.stencil.front.depthFail = mask.pipeline.stencil.back.depthFail =
        StencilOp::Invert;
    test.reference = 255;
    Pixel(f.Render({mask, test}), test.color, "Stencil DepthFail must run when depth rejects");
    mask.pipeline.depth = {};
    mask.pipeline.stencil.front.compare = mask.pipeline.stencil.back.compare = CompareOp::Never;
    mask.pipeline.stencil.front.fail = mask.pipeline.stencil.back.fail = StencilOp::IncrementWrap;
    test.reference = 1;
    Pixel(f.Render({mask, test}), test.color, "Stencil Fail must run when stencil rejects");

    Step blend{f.base};
    blend.color = {.8f, .2f, .4f, .25f};
    blend.pipeline.blend.enabled = true;
    blend.pipeline.blend.dstAlpha = BlendFactor::OneMinusSrcAlpha;
    Pixel(f.Render({blend}), {.35f, .35f, .55f, .625f}, "Independent alpha blending");
    blend.pipeline.blend.srcColor = BlendFactor::One;
    blend.pipeline.blend.dstColor = BlendFactor::One;
    blend.pipeline.blend.colorOp = BlendOp::Subtract;
    blend.pipeline.raster.colorWriteMask = 3;
    Pixel(f.Render({blend}), {.6f, -.2f, .6f, .5f}, "Blend operations and channel write masks");
    auto same = runtime.CreatePipeline(blend.pipeline);
    Require(runtime.CreatePipeline(blend.pipeline) == same, "Identical states must share PSO");
    auto changed = blend.pipeline;
    changed.depth.compare = CompareOp::Less;
    Require(runtime.CreatePipeline(changed) != same,
            "Depth comparison must participate in PSO key");
    changed = blend.pipeline;
    changed.stencil.back.fail = StencilOp::Invert;
    Require(runtime.CreatePipeline(changed) != same,
            "Back-face stencil must participate in PSO key");
    const auto invalid = [&](PipelineDesc desc)
    {
        bool rejected = false;
        try
        {
            runtime.CreatePipeline(std::move(desc));
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        Require(rejected, "Invalid fixed-function state must fail before native PSO creation");
    };
    changed = f.base;
    changed.depthStencilFormat.reset();
    changed.stencil.enabled = true;
    invalid(changed);
    changed.depthStencilFormat = Format::D32;
    invalid(changed);
    changed = f.base;
    changed.depth.compare = static_cast<CompareOp>(99);
    invalid(changed);
    changed = f.base;
    changed.blend.srcAlpha = BlendFactor::SrcColor;
    invalid(changed);
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "Expected backend and output directory");
        const std::filesystem::path output(argv[2]);
        std::filesystem::create_directories(output);
        Logger logger(output / "states.log");
        DeviceOptions options;
        options.backend = std::string(argv[1]) == "dx12" ? Backend::Dx12 : Backend::Vulkan;
        options.validation = ValidationMode::On;
        auto device = CreateDevice(options, logger);
        {
            auto runtime = device->CreateRuntime();
            Gpu(*runtime,
                options.backend == Backend::Dx12 ? shader::Target::Dxil : shader::Target::Spirv,
                output);
            runtime->WaitIdle();
            Require(runtime->GetMemoryStats().used == 0, "Render state resources leaked");
        }
        device->PollDiagnostics();
        device->Shutdown();
        Require(device->GetValidationErrorCount() == 0, "Render state validation errors");
        std::cout << "Render state contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
