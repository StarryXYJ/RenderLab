#include "material/asset.hpp"
#include "shader/compiler.hpp"
#include "shader/reload.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
using namespace renderlab;
void Require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}
void Write(const std::filesystem::path& path, const std::string& value)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << value;
    Require(bool(file), "Cannot write test fixture");
}
void Replace(std::string& text, const std::string& before, const std::string& after)
{
    const auto position = text.find(before);
    Require(position != std::string::npos, "Missing fixture token " + before);
    text.replace(position, before.size(), after);
}
template <class F> void Fails(F function, const std::string& fragment)
{
    try
    {
        function();
    }
    catch (const std::exception& e)
    {
        Require(std::string(e.what()).find(fragment) != std::string::npos, e.what());
        return;
    }
    throw std::runtime_error("Expected diagnostic " + fragment);
}
shader::BuildResult Await(shader::ReloadCompiler& worker)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (auto result = worker.Poll())
            return std::move(*result);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("Background compilation timed out");
}
void FeatureContracts(const std::filesystem::path& root)
{
    const auto source = root / "features.shader";
    const std::string text = R"(shader "Test/Features"
{
    enum NormalMode { Off = 0; Tangent = 3; Detail = 7; }
    material
    {
        [id("clip"), keyword("ALPHA_CLIP")] bool Clip = false;
        [id("normal"), keyword("NORMAL_MODE")] NormalMode Normal = Tangent;
        NormalMode DynamicMode = Detail;
        bool Enabled = true;
        float4 Tint = (1, 1, 1, 1);
    }
    pass Beauty
    {
        features = { Normal, Clip };
        vertex = "features.hlsl:VSMain";
        pixel = "features.hlsl:PSMain";
        depth = false;
    }
    pass Sun
    {
        features = { Clip };
        vertex = "features.hlsl:VSMain";
        pixel = "features.hlsl:PSMain";
        depth = false;
    }
})";
    Write(source, text);
    Write(root / "features.hlsl", R"(#include <renderlab/generated/parameters.hlsli>
struct P { float4 position : SV_Position; };
P VSMain(float3 p : POSITION) { P v; v.position = float4(p, 1); return v; }
float4 PSMain(P p) : SV_Target
{
#ifdef NORMAL_MODE
    return Material.Tint + float4(NORMAL_MODE, Material.DynamicMode, ALPHA_CLIP, Material.Enabled);
#else
    return Material.Tint * (1 + ALPHA_CLIP);
#endif
})");
    shader::CompileOptions options;
    options.cacheDirectory = root / "feature-cache";
    auto built = shader::CompileShader(source, options);
    Require(bool(built), built.diagnostics);
    const auto& definition = built.package->definition;
    Require(built.package->programs.size() == 32,
            "Forward 6 + Shadow 2 combinations, each with two stages and two targets");
    const shader::MaterialTemplate materialTemplate(built.package);
    Require(materialTemplate.Properties().size() == 5 && materialTemplate.Fields().size() == 3,
            "Static properties remain in logical schema without occupying GPU fields");
    Require(built.package->interfaces[0].Material()->size ==
                built.package->interfaces[1].Material()->size,
            "Different pass feature sets must preserve material layout");
    auto asset = material::Create(materialTemplate);
    material::Set(asset, materialTemplate, "clip", std::array<double, 1>{1});
    material::Set(asset, materialTemplate, "normal", std::array<double, 1>{7});
    const auto selected =
        shader::SelectVariantKey(definition, definition.passes[0], {{"clip", 1}, {"normal", 7}});
    Require(selected == shader::VariantKey{{"clip", 1}, {"normal", 7}},
            "Variant keys must sort stable property IDs");
    Require(built.package->Find("Beauty", selected, rhi::ShaderStage::Pixel, shader::Target::Dxil)
                    .axes == selected,
            "Program lookup must select the exact static key");
    Fails(
        [&]
        {
            built.package->Find("Beauty", shader::VariantKey{{"normal", 7}},
                                rhi::ShaderStage::Pixel, shader::Target::Dxil);
        },
        "Missing prepared");
    Fails([&] { material::Set(asset, materialTemplate, "normal", std::array<double, 1>{2}); },
          "enum value");
    Fails([&] { shader::SelectVariantKey(definition, definition.passes[0], {{"clip", 1}}); },
          "feature value");
    material::Save(asset, root / "features.material");
    Require(
        material::Load(root / "features.material", materialTemplate).values.at("normal").numbers ==
            std::vector<double>{7},
        "Static properties must round trip as ordinary logical values");
    auto renamed = text;
    Replace(renamed, "bool Clip", "bool RenamedClip");
    Replace(renamed, "{ Normal, Clip }", "{ Normal, RenamedClip }");
    Replace(renamed, "{ Clip }", "{ RenamedClip }");
    Write(source, renamed);
    auto next = shader::CompileShader(source, options);
    Require(bool(next), next.diagnostics);
    auto migrated = material::Migrate(asset, shader::MaterialTemplate(next.package));
    Require(migrated.values.at("clip").numbers == std::vector<double>{1} &&
                shader::SelectVariantKey(next.package->definition,
                                         next.package->definition.passes[0],
                                         {{"clip", 1}, {"normal", 7}}) == selected,
            "Renaming a static symbol must preserve values and program selection IDs");
    auto removedValue = text;
    Replace(removedValue, "Detail = 7", "Detail = 9");
    Write(source, removedValue);
    auto incompatible = shader::CompileShader(source, options);
    Require(bool(incompatible), incompatible.diagnostics);
    Fails([&] { material::Migrate(asset, shader::MaterialTemplate(incompatible.package)); },
          "enum value");
    const auto rejected = [&](std::string invalid, const char* diagnostic)
    {
        Write(source, invalid);
        const auto result = shader::CompileShader(source, options);
        Require(!result && result.diagnostics.find(diagnostic) != std::string::npos,
                "Expected static feature diagnostic: " + std::string(diagnostic) + ": " +
                    result.diagnostics);
    };
    auto invalid = text;
    Replace(invalid, "bool Clip", "float Clip");
    rejected(invalid, "material bool/enum");
    invalid = text;
    Replace(invalid, "features = { Normal, Clip }", "features = { Enabled }");
    rejected(invalid, "keyword properties");
    invalid = text;
    Replace(invalid, "Tangent = 3", "Tangent = 0");
    rejected(invalid, "unique names");
    std::string budget = "shader \"Budget\" { material { ";
    for (int i = 0; i < 7; ++i)
        budget +=
            "[keyword(\"K" + std::to_string(i) + "\")] bool B" + std::to_string(i) + " = false;";
    budget += "} pass Test { features = { B0, B1, B2, B3, B4, B5, B6 }; vertex = "
              "\"features.hlsl:VSMain\"; pixel = \"features.hlsl:PSMain\"; } }";
    rejected(budget, "limit (64)");
    Write(source, text);
}
} // namespace
int wmain(int argc, wchar_t** argv)
{
    using namespace renderlab;
    try
    {
        Require(argc == 3, "Expected source and output directories");
        const auto root =
            std::filesystem::path(argv[2]) /
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root,
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing);
        const auto source = root / "surface.shader";
        const auto original = shader::ReadText(source);
        shader::CompileOptions options;
        options.cacheDirectory = root / "cache";
        auto result = shader::CompileShader(source, options);
        Require(bool(result), result.diagnostics);
        const auto package = result.package;
        Require(!package->cacheHit && package->programs.size() == 20,
                "Expected complete finite pass/variant/target combinations");
        auto cached = shader::CompileShader(source, options);
        Require(bool(cached) && cached.package->cacheHit && cached.package->key == package->key,
                "Cache hit");
        Write(options.cacheDirectory / package->key / "0.bin", "corrupt");
        auto repaired = shader::CompileShader(source, options);
        Require(bool(repaired) && !repaired.package->cacheHit, "Corrupt cache must rebuild");
        const shader::MaterialTemplate materialTemplate(package);
        auto asset = material::Create(materialTemplate);
        Require(asset.values.contains("unused") && materialTemplate.Fields().size() == 9,
                "Unused declared fields must remain in schema");
        const std::array<double, 4> color{.5, .25, .125, 1};
        material::Set(asset, materialTemplate, "base_color", color);
        material::Save(asset, root / "sample.material");
        auto loaded = material::Load(root / "sample.material", materialTemplate);
        Require(loaded.values.at("base_color").numbers == asset.values.at("base_color").numbers,
                "Material round trip");
        Fails([&]
              { material::Set(asset, materialTemplate, "roughness", std::array<double, 1>{2}); },
              "range");
        Fails([&] { material::SetResource(asset, materialTemplate, "base_map", "C:/invalid.png"); },
              "relative");
        Fails([&] { material::SelectVariant(asset, materialTemplate, "Unknown"); }, "variant");
        const auto& block = *package->Layout("Forward").Material();
        const auto bytes = material::Pack(block, asset.values);
        for (const auto& field : block.fields)
            if (field.property.symbol == "Basis")
            {
                std::array<float, 8> actual{};
                std::memcpy(actual.data(), bytes.data() + field.offset, 32);
                Require(actual == std::array<float, 8>{1, 3, 5, 0, 2, 4, 6, 0},
                        "Logical matrix -> column-major packing");
            }
        const auto tool = root / "tools/color.hlsli";
        const auto toolOriginal = shader::ReadText(tool);
        Write(tool, toolOriginal + "\n// Dependency edit\n");
        auto includeChanged = shader::CompileShader(source, options);
        Require(bool(includeChanged) && includeChanged.package->key != package->key &&
                    !includeChanged.package->cacheHit,
                "Transitive include invalidation");
        Write(tool, toolOriginal);
        const auto module = root / "view.params";
        const auto moduleOriginal = shader::ReadText(module);
        Write(module, moduleOriginal + "\n// Shared module edit\n");
        auto sharedChanged = shader::CompileShader(source, options);
        Require(bool(sharedChanged) && sharedChanged.package->key != package->key,
                "Shared module invalidation");
        Write(module, moduleOriginal);
        const auto check = [&](const std::string& text, const std::string& error)
        {
            Write(source, text);
            const auto failed = shader::CompileShader(source, options);
            Require(!failed && failed.diagnostics.find(error) != std::string::npos,
                    "Expected " + error + ": " + failed.diagnostics);
        };
        auto text = original;
        Replace(text, "(1, 2, 3)", "(1, 2, 3]");
        check(text, "Expected ')'");
        text = original;
        Replace(text, "= 0.5;", "= 1e100;");
        check(text, "range");
        text = original;
        Replace(text, "float Unused", "[enum(\"a\")] float Unused");
        check(text, "enum requires");
        text = original;
        Replace(text, "use \"view.params\";", "use \"absent.params\";");
        check(text, "absent.params");
        Write(module, "use \"view.params\";");
        check(original, "cycle");
        Write(module, moduleOriginal);
        Write(tool, "#include \"absent.hlsli\"\n");
        check(original, "absent.hlsli");
        Write(tool, toolOriginal);
        const auto hlsl = root / "surface.hlsl";
        const auto hlslOriginal = shader::ReadText(hlsl);
        Write(hlsl, hlslOriginal + "\nfloat invalid = absentSymbol;\n");
        check(original, "absentSymbol");
        Write(hlsl, hlslOriginal + "\nTexture2D<float4> Rogue : register(t0,space0);\n");
        // Force an undeclared resource to survive optimization.
        auto rogue = shader::ReadText(hlsl);
        Replace(rogue, "ApplyDetail(Material.BaseColor.rgb)", "Rogue.Load(int3(0,0,0)).rgb");
        Replace(rogue, "Texture2D<float4> Rogue : register(t0,space0);", "");
        Write(hlsl, "Texture2D<float4> Rogue : register(t0,space0);\n" + rogue);
        check(original, "binding");
        Write(hlsl, hlslOriginal);
        text = original;
        Replace(text, "float3x2 Basis = (1, 2, 3, 4, 5, 6);",
                "float3x2 Basis = (1, 2, 3, 4, 5, 6);\n        float Added = 9;");
        Write(source, text);
        auto added = shader::CompileShader(source, options);
        Require(bool(added), added.diagnostics);
        auto migrated = material::Migrate(asset, shader::MaterialTemplate(added.package));
        Require(migrated.values.at("Added").numbers[0] == 9 &&
                    migrated.values.at("base_color").numbers ==
                        asset.values.at("base_color").numbers,
                "Reload adds defaults and keeps stable IDs");
        // Read every boundary field so both targets must preserve array/matrix tail packing.
        text = original;
        Replace(text, "float3x2 Basis = (1, 2, 3, 4, 5, 6);",
                "float3x2 Basis = (1, 2, 3, 4, 5, 6);\n        float Single[1] = 3;\n        float "
                "After = 4;\n        float2x3 Matrices[2] = 2;\n        float Last = 7;");
        Write(source, text);
        const auto computeSource = root / "probe.hlsl";
        const auto computeOriginal = shader::ReadText(computeSource);
        auto computeBoundary = computeOriginal;
        Replace(computeBoundary, "Results[0] = Material.BaseColor;",
                "Results[0] = float4(Material.Single[0], Material.After, "
                "Material.Matrices[1][1][2], Material.Last);");
        Write(computeSource, computeBoundary);
        auto boundary = shader::CompileShader(source, options);
        Require(bool(boundary), boundary.diagnostics);
        Write(computeSource, computeOriginal);
        text = original;
        Replace(text, "float Unused", "int Unused");
        Replace(text, "= 0.125;", "= 0;");
        Write(source, text);
        auto changed = shader::CompileShader(source, options);
        Require(bool(changed), changed.diagnostics);
        Fails([&] { material::Migrate(asset, shader::MaterialTemplate(changed.package)); },
              "type conflict");
        Write(source, original);
        shader::ReloadCompiler worker;
        uint64_t latest = 0;
        for (int i = 0; i < 12; ++i)
            latest = worker.Request(source, options);
        auto background = Await(worker);
        Require(bool(background) && background.package->generation == latest,
                "Stale background builds must be discarded");
        Write(hlsl, "invalid hlsl");
        worker.Request(source, options);
        Require(!Await(worker), "Background failure diagnostic");
        Write(hlsl, hlslOriginal);
        worker.Request(source, options);
        Require(bool(Await(worker)), "Background failure recovery");
        FeatureContracts(root);
        std::cout << "M2 CPU acceptance passed: declarations, metadata, packing, dual reflection, "
                     "cache invalidation/corruption, material round trip/migration, background "
                     "coalescing/failure/recovery\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
