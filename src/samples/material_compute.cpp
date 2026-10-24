#include "samples/detail/check.hpp"
#include "samples/detail/material_checks.hpp"

#include <cmath>
#include <cstring>
#include <fstream>

namespace renderlab::samples::detail
{
using namespace rhi;
void VerifyMaterialCompute(rhi::Runtime& runtime, material::Runtime& materials,
                           std::shared_ptr<const shader::Package> package,
                           const material::Asset& asset, const material::ResourceResolver& resolve)
{
    // The same fixed material layout is read by raster passes and a compute pass.
    auto input = runtime.CreateBuffer({272, Memory::Upload, "Probe.Input"});
    const std::array<float, 4> inputValue{10, 20, 30, 40};
    std::memcpy(input->Map().data() + 256, inputValue.data(), 16);
    auto results = runtime.CreateBuffer({336, Memory::Device, "Probe.Results", true});
    auto outputTexture = runtime.CreateTexture({1, 1, Format::Rgba8, true, false, "Probe.Output"});
    auto resultRead = runtime.CreateBuffer({80, Memory::Readback, "Probe.Results.Readback"});
    auto textureRead = runtime.CreateBuffer({256, Memory::Readback, "Probe.Texture.Readback"});
    material::Preparation probe;
    probe.pass = "Probe";
    probe.constants["Draw"]["Offset"] = {"float4", 1, {1, 2, 3, 4}};
    probe.bindings["Input"] = {0, {}, {}, input, 256, 16, 16};
    probe.bindings["Results"] = {0, {}, {}, results, 256, 80, 16};
    probe.bindings["Output"] = {0, runtime.CreateView(outputTexture)};
    auto viewConstants = runtime.CreateBuffer({512, Memory::Upload, "Probe.ExternalView"});
    const auto viewBytes = material::Pack(package->Layout("Probe").blocks.front(), {});
    std::memcpy(viewConstants->Map().data() + 256, viewBytes.data(), viewBytes.size());
    probe.bindings["View"] = {0, {}, {}, viewConstants, 256, 256};
    auto compute = materials.Prepare(package, asset, probe, resolve);
    auto commands = runtime.CreateCommands();
    MaterialConstants(*commands, compute);
    commands->Barrier(viewConstants, State::HostWrite, State::UniformRead);
    commands->Barrier(input, State::HostWrite, State::ShaderRead);
    commands->Barrier(results, State::Undefined, State::Storage);
    commands->Barrier(outputTexture, State::Undefined, State::Storage);
    commands->Bind(compute.pipeline);
    commands->Bind(runtime.CreateGroup(compute.pipeline->desc.layout, compute.resources));
    commands->Compute(1, 1);
    commands->Barrier(results, State::Storage, State::Storage);
    commands->Barrier(outputTexture, State::Storage, State::Storage);
    commands->Compute(1, 1);
    commands->Barrier(results, State::Storage, State::CopySource);
    commands->Copy(results, resultRead, 80, 256);
    commands->Barrier(resultRead, State::CopyDest, State::HostRead);
    commands->Barrier(outputTexture, State::Storage, State::CopySource);
    commands->CopyTexture(textureRead, outputTexture, 256, false);
    commands->Barrier(textureRead, State::CopyDest, State::HostRead);
    runtime.Wait(runtime.Submit(std::move(commands)));
    std::array<float, 20> actual{};
    std::memcpy(actual.data(), resultRead->Map().data(), 80);
    const std::array<float, 20> expected{.25f, .5f, .75f, 1, .5f, 2, 1,  7,  1,  2,
                                         3,    .8f, 1,    2, 5,   6, 11, 22, 33, 45};
    for (size_t i = 0; i < actual.size(); ++i)
        Require(std::abs(actual[i] - expected[i]) < 1e-6f, "Compute cbuffer/SRV/UAV result " +
                                                               std::to_string(i) + " = " +
                                                               std::to_string(actual[i]));
    const std::array<uint8_t, 4> expectedPixel{64, 128, 191, 255};
    for (size_t channel = 0; channel < 4; ++channel)
        Require(std::abs(int(std::to_integer<uint8_t>(textureRead->Map()[channel])) -
                         int(expectedPixel[channel])) <= 1,
                "Compute UAV texture channel " + std::to_string(channel) + " = " +
                    std::to_string(std::to_integer<uint8_t>(textureRead->Map()[channel])));
}
} // namespace renderlab::samples::detail
