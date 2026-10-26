#include "samples/material/material_demo.hpp"

#include "rhi/commands.hpp"
#include "rhi/upload.hpp"
#include "samples/common/check.hpp"
#include "samples/material/material_checks.hpp"
#include "shader/compiler.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <thread>

namespace renderlab::samples
{
using detail::Require;
void VerifyMaterial(rhi::Runtime& runtime, shader::Target target, Logger& logger,
                    const std::filesystem::path& source, const std::filesystem::path& output)
{
    using namespace rhi;
    std::filesystem::create_directories(output);
    shader::CompileOptions options;
    options.cacheDirectory = output / "cache";
    auto build = shader::CompileShader(source, options);
    Require(bool(build), build.diagnostics);
    auto package = build.package;
    const shader::MaterialTemplate materialTemplate(package);
    auto asset = material::Create(materialTemplate);
    material::Runtime materials(runtime, target);
    material::Preparation raster;
    raster.pass = "Forward";
    raster.vertexStride = 20;
    raster.attributes = {{"POSITION", 0, VertexFormat::Float3, 0},
                         {"TEXCOORD", 1, VertexFormat::Float2, 12}};
    // A full-screen triangle makes readback comparisons independent of interpolation near edges.
    const std::array<float, 15> data{-1, -1, .5f, 0, 1, -1, 3, .5f, 0, -1, 3, -1, .5f, 2, 1};
    auto upload = runtime.CreateBuffer({sizeof(data), Memory::Upload, "Material.Vertices.Upload"});
    std::memcpy(upload->Map().data(), data.data(), sizeof(data));
    auto vertices = runtime.CreateBuffer({sizeof(data), Memory::Device, "Material.Vertices"});
    std::map<std::string, std::shared_ptr<TextureView>> textures;
    auto sampler = runtime.CreateSampler();
    auto setup = runtime.CreateCommands();
    setup->Barrier(upload, State::HostWrite, State::CopySource);
    setup->Copy(upload, vertices, sizeof(data));
    setup->Barrier(vertices, State::CopyDest, State::Vertex);
    for (const auto& [name, value] :
         {std::pair{"white", uint8_t(255)}, std::pair{"gray", uint8_t(128)}})
    {
        auto texture = runtime.CreateTexture({1, 1, Format::Rgba8Srgb, false, false, name});
        auto staging = runtime.CreateBuffer({256, Memory::Upload, "Texture.Upload"});
        const std::array<uint8_t, 4> rgba{value, value, value, 255};
        std::memcpy(staging->Map().data(), rgba.data(), 4);
        setup->Barrier(staging, State::HostWrite, State::CopySource);
        setup->Barrier(texture, State::Undefined, State::CopyDest);
        setup->CopyTexture(staging, texture, 256, true);
        setup->Barrier(texture, State::CopyDest, State::Sampled);
        textures.emplace(name, runtime.CreateView(texture));
    }
    runtime.Wait(runtime.Submit(std::move(setup)));
    const material::ResourceResolver resolve =
        [&](const shader::Property& property, const std::string& reference)
    {
        Binding binding{};
        if (property.type.resource == BindingKind::Sampler)
        {
            Require(reference == "point_clamp", "Unsupported sampler preset " + reference);
            binding.sampler = sampler;
        }
        else
        {
            Require(textures.contains(reference), "Unknown texture reference " + reference);
            binding.view = textures.at(reference);
        }
        return binding;
    };
    auto first = materials.Prepare(package, asset, raster, resolve);
    auto firstRead = detail::RecordMaterial(runtime, first, vertices);
    runtime.Wait(runtime.Submit(std::move(firstRead.commands)));
    detail::CheckMaterialColor(firstRead.pixels, {.25f, .5f, .75f, 1});
    detail::SaveMaterialImage(output / "material-default.bmp", firstRead.pixels);
    material::Set(asset, materialTemplate, "base_color", std::array<double, 4>{.5, .25, .125, 1});
    material::Save(asset, output / "sample.material");
    asset = material::Load(output / "sample.material", materialTemplate);
    auto edited = materials.Prepare(package, asset, raster, resolve);
    Require(first.pipeline == edited.pipeline, "Numeric edits must reuse PSO");
    auto editedRead = detail::RecordMaterial(runtime, edited, vertices);
    runtime.Wait(runtime.Submit(std::move(editedRead.commands)));
    detail::CheckMaterialColor(editedRead.pixels, {.5f, .25f, .125f, 1});
    detail::SaveMaterialImage(output / "material-edited.bmp", editedRead.pixels);
    auto alternate = raster;
    alternate.pass = "Alternate";
    auto alternatePrepared = materials.Prepare(package, asset, alternate, resolve);
    auto alternateRead = detail::RecordMaterial(runtime, alternatePrepared, vertices);
    runtime.Wait(runtime.Submit(std::move(alternateRead.commands)));
    detail::CheckMaterialColor(alternateRead.pixels, {.5f, 0, 0, 1});
    material::SetResource(asset, materialTemplate, "base_map", "gray");
    auto textured = materials.Prepare(package, asset, raster, resolve);
    Require(textured.pipeline == edited.pipeline, "Texture edit must reuse PSO");
    auto texturedRead = detail::RecordMaterial(runtime, textured, vertices);
    runtime.Wait(runtime.Submit(std::move(texturedRead.commands)));
    const float gray = std::pow((128 / 255.f + .055f) / 1.055f, 2.4f);
    detail::CheckMaterialColor(texturedRead.pixels, {.5f * gray, .25f * gray, .125f * gray, 1});
    material::Set(asset, materialTemplate, "detail", std::array<double, 1>{1});
    auto detail = materials.Prepare(package, asset, raster, resolve);
    Require(detail.pipeline != edited.pipeline, "Variant must choose distinct PSO");
    auto detailRead = detail::RecordMaterial(runtime, detail, vertices);
    runtime.Wait(runtime.Submit(std::move(detailRead.commands)));
    detail::CheckMaterialColor(detailRead.pixels, {.25f * gray, .125f * gray, .0625f * gray, 1});
    auto blended = raster;
    material::Set(asset, materialTemplate, "blend_test", std::array<double, 1>{1});
    const auto blendedPrepared = materials.Prepare(package, asset, blended, resolve);
    Require(blendedPrepared.pipeline != detail.pipeline &&
                blendedPrepared.pipeline->desc.pixel == detail.pipeline->desc.pixel &&
                blendedPrepared.pipeline->desc.vertex == detail.pipeline->desc.vertex,
            "State edits must change PSO while reusing shader programs");

    asset = material::Create(materialTemplate);
    detail::VerifyMaterialCompute(runtime, materials, package, asset, resolve);
    const std::array required{raster, alternate};
    detail::VerifyMaterialReload(runtime, materials, source, output, options, asset, resolve,
                                 vertices, required);
    logger.Write(LogLevel::Info,
                 "M2 acceptance passed: scalar/vector/array/non-square matrix cbuffer, SRV/UAV "
                 "buffer+texture, sRGB/material edits/save/load, multi-pass/variant/state PSO "
                 "cache, background stale/failure/recovery, recorded/in-flight reload retirement");
}
} // namespace renderlab::samples
