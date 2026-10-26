#include "samples/common/check.hpp"
#include "samples/material/material_checks.hpp"

#include "shader/reload.hpp"
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <thread>

namespace renderlab::samples::detail
{
using namespace rhi;
namespace
{
void Write(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    Require(bool(file), "Cannot write reload fixture");
}
shader::BuildResult Await(shader::ReloadCompiler& compiler)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (auto result = compiler.Poll())
            return std::move(*result);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("Reload compilation timed out");
}
} // namespace
void VerifyMaterialReload(rhi::Runtime& runtime, material::Runtime& materials,
                          const std::filesystem::path& source, const std::filesystem::path& output,
                          shader::CompileOptions options, const material::Asset& asset,
                          const material::ResourceResolver& resolve,
                          const std::shared_ptr<rhi::Buffer>& vertices,
                          std::span<const material::Preparation> required)
{
    // Live source files are copied; acceptance never edits the authored project shader.
    const auto reloadRoot = output / "reload";
    std::filesystem::create_directories(reloadRoot);
    std::filesystem::copy(source.parent_path(), reloadRoot,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing);
    const auto reloadSource = reloadRoot / source.filename();
    shader::ReloadCompiler compiler;
    options.generation = 40;
    compiler.Request(reloadSource, options);
    auto initial = Await(compiler);
    Require(bool(initial), initial.diagnostics);
    material::LiveMaterial live(initial.package, asset);
    live.Stage(materials, initial.package, required, resolve);
    live.PublishFrameBoundary();
    auto oldRead = RecordMaterial(runtime, live.Snapshots().front(), vertices);
    std::weak_ptr<Pipeline> oldPipeline = live.Snapshots().front().pipeline;
    const auto declaration = shader::ReadText(reloadSource);
    auto changed = declaration;
    const auto point = changed.find("int Mode");
    Require(point != std::string::npos, "Reload fixture");
    changed.insert(point, "float Added = 9;\n        ");
    Write(reloadSource, changed);
    const auto toolPath = reloadRoot / "tools/color.hlsli";
    const auto originalTool = shader::ReadText(toolPath);
    auto changedTool = originalTool;
    const auto returnPosition = changedTool.find("return value;");
    Require(returnPosition != std::string::npos, "Reload tool fixture");
    changedTool.replace(returnPosition, std::string("return value;").size(), "return value * 0.5;");
    Write(toolPath, changedTool);
    uint64_t latest = 0;
    for (int i = 0; i < 8; ++i)
        latest = compiler.Request(reloadSource, options);
    auto replacement = Await(compiler);
    Require(bool(replacement), replacement.diagnostics);
    Require(replacement.package->generation == latest, "Background generation ordering");
    live.Stage(materials, replacement.package, required, resolve);
    Require(live.GetPackage() == initial.package, "Staging must not replace active generation");
    Require(live.PublishFrameBoundary() && live.GetAsset().values.at("Added").numbers[0] == 9,
            "Frame boundary publish/default migration");
    Require(live.Snapshots().front().pipeline != oldPipeline.lock(),
            "Reload generation invalidates PSO");
    Require(!oldPipeline.expired(), "Recorded old generation must stay alive");
    // Submit after replacement to cover recorded-but-not-submitted and then in-flight ownership.
    const auto oldPoint = runtime.Submit(std::move(oldRead.commands));
    auto newRead = RecordMaterial(runtime, live.Snapshots().front(), vertices);
    runtime.Wait(runtime.Submit(std::move(newRead.commands)));
    runtime.Wait(oldPoint);
    CheckMaterialColor(oldRead.pixels, {.25f, .5f, .75f, 1});
    CheckMaterialColor(newRead.pixels, {.125f, .25f, .375f, 1});
    SaveMaterialImage(output / "material-reloaded.bmp", newRead.pixels);
    Require(oldPipeline.expired(), "Completed old generation must retire");
    const auto active = live.GetPackage();
    // A valid compilation can still fail migration or preparation; neither may partially publish.
    auto conflicting = changed;
    const auto conflictPosition = conflicting.find("float Unused");
    conflicting.replace(conflictPosition, std::string("float Unused").size(), "int Unused");
    const auto defaultPosition = conflicting.find("0.125");
    conflicting.replace(defaultPosition, 5, "0");
    Write(reloadSource, conflicting);
    compiler.Request(reloadSource, options);
    auto typeConflict = Await(compiler);
    Require(bool(typeConflict), typeConflict.diagnostics);
    bool rejected = false;
    try
    {
        live.Stage(materials, typeConflict.package, required, resolve);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Require(rejected && live.GetPackage() == active && !live.PublishFrameBoundary(),
            "Type conflict must preserve active generation");
    rejected = false;
    try
    {
        live.Stage(materials, initial.package, required, resolve);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Require(rejected, "Stale prepared generation must be rejected");
    auto incomplete = std::vector<material::Preparation>(required.begin(), required.end());
    incomplete.back().pass = "MissingPass";
    rejected = false;
    try
    {
        live.Stage(materials, active, incomplete, resolve);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Require(rejected && live.GetPackage() == active && !live.PublishFrameBoundary(),
            "Partial preparation must not publish");
    Write(reloadSource, changed);
    Write(reloadRoot / "surface.hlsl", "invalid source");
    compiler.Request(reloadSource, options);
    auto failure = Await(compiler);
    Require(!failure && live.GetPackage() == active, "Compile failure keeps valid generation");
    auto retainedRead = RecordMaterial(runtime, live.Snapshots().front(), vertices);
    runtime.Wait(runtime.Submit(std::move(retainedRead.commands)));
    CheckMaterialColor(retainedRead.pixels, {.125f, .25f, .375f, 1});
    Write(reloadRoot / "surface.hlsl", shader::ReadText(source.parent_path() / "surface.hlsl"));
    Write(toolPath, originalTool);
    compiler.Request(reloadSource, options);
    auto recovery = Await(compiler);
    Require(bool(recovery), recovery.diagnostics);
    live.Stage(materials, recovery.package, required, resolve);
    live.PublishFrameBoundary();
    auto recoveredRead = RecordMaterial(runtime, live.Snapshots().front(), vertices);
    runtime.Wait(runtime.Submit(std::move(recoveredRead.commands)));
    CheckMaterialColor(recoveredRead.pixels, {.25f, .5f, .75f, 1});
    auto editedAsset = live.GetAsset();
    material::Set(editedAsset, shader::MaterialTemplate(live.GetPackage()), "base_color",
                  std::array<double, 4>{.5, .25, .125, 1});
    const auto unchangedPipeline = live.Snapshots().front().pipeline;
    live.Stage(materials, live.GetPackage(), editedAsset, required, resolve);
    live.PublishFrameBoundary();
    Require(live.Snapshots().front().pipeline == unchangedPipeline,
            "Live numeric edit must reuse pipeline");
    auto editedRead = RecordMaterial(runtime, live.Snapshots().front(), vertices);
    runtime.Wait(runtime.Submit(std::move(editedRead.commands)));
    CheckMaterialColor(editedRead.pixels, {.5f, .25f, .125f, 1});
}
} // namespace renderlab::samples::detail
