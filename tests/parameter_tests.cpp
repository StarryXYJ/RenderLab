#include "parameter_fixture.hpp"

#include "material/asset.hpp"
#include "shader/compiler.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
void Require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}
template <class Function> void Invalid(Function&& function)
{
    try
    {
        function();
    }
    catch (const std::invalid_argument&)
    {
        return;
    }
    throw std::runtime_error("Expected parameter contract diagnostic");
}
} // namespace
int main(int argc, char** argv)
{
    using namespace renderlab;
    try
    {
        Require(argc == 3, "Expected fixture and output directories");
        shader::CompileOptions options;
        options.cacheDirectory = std::filesystem::path(argv[2]) / "cache";
        const auto built =
            shader::CompileShader(std::filesystem::path(argv[1]) / "parameters.shader", options);
        Require(bool(built), built.diagnostics.c_str());
        const auto pass = built.package->ResolvePass(std::string("Draw"));
        const auto binding = built.package->SchemaBinding(pass, shader::generated::AbiDataContract);
        Require(binding.has_value(), "Generated CPU contract must match the compiled HLSL schema");
        const auto& block = built.package->GetBlock(*binding);
        const auto texture =
            built.package->SemanticBinding(pass, shader::Property::BindingSemantic::BrdfLut);
        Require(texture && built.package->GetResource(*texture).property.symbol == "RenamedLut",
                "Standard resources must resolve by semantic instead of variable name");
        const auto expected = material::Pack(block, {});
        shader::generated::AbiData data;
        const auto packed = shader::generated::Pack(data);
        Require(
            expected.size() == packed.size() &&
                std::equal(expected.begin(), expected.end(), packed.begin()),
            "Generated packing must match reflected arrays, bools, enums and non-square matrices");
        std::array<float, 8> matrix{};
        std::memcpy(matrix.data(), packed.data(), sizeof(matrix));
        Require(matrix == std::array<float, 8>{1, 3, 5, 0, 2, 4, 6, 0},
                "Generated matrix packing must be column-major");
        auto wrong = shader::generated::AbiDataContract;
        ++wrong.version;
        Invalid([&] { built.package->SchemaBinding(pass, wrong); });
        data.Enabled = 2;
        Invalid([&] { shader::generated::Pack(data); });
        data.Enabled = 1;
        data.Choice = 2;
        Invalid([&] { shader::generated::Pack(data); });
        data.Choice = 7;
        data.Count = 0;
        Invalid([&] { shader::generated::Pack(data); });
        data.Count = 11;
        Invalid([&] { shader::generated::Pack(data); });
        data.Count = 10;
        data.Weights[0] = std::numeric_limits<float>::quiet_NaN();
        Invalid([&] { shader::generated::Pack(data); });
        Require(
            shader::GenerateTypesHeader(built.package->interfaces.front()).find("ConstantBuffer") ==
                std::string::npos,
            "Library type headers must contain no GPU bindings");
        const auto root = std::filesystem::path(argv[2]) / "alias";
        std::filesystem::create_directories(root);
        std::filesystem::copy(argv[1], root,
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing);
        auto declaration = shader::ReadText(root / "parameters.shader");
        declaration.replace(declaration.find("parameters.params\";"),
                            std::string("parameters.params\";").size(),
                            "parameters.params\" as Local;");
        declaration.replace(declaration.find("RenamedLut"), std::string("RenamedLut").size(),
                            "AnotherLut");
        std::ofstream(root / "parameters.shader", std::ios::binary) << declaration;
        auto entry = shader::ReadText(root / "parameters.hlsl");
        entry.replace(entry.find("EvaluateParameters(Inputs)"),
                      std::string("EvaluateParameters(Inputs)").size(),
                      "EvaluateParameters(Local_Inputs)");
        std::ofstream(root / "parameters.hlsl", std::ios::binary) << entry;
        auto renamed = shader::CompileShader(root / "parameters.shader", options);
        Require(bool(renamed), renamed.diagnostics.c_str());
        const auto next = renamed.package->ResolvePass(std::string("Draw"));
        const auto alias = renamed.package->SchemaBinding(next, shader::generated::AbiDataContract);
        Require(alias && renamed.package->GetBlock(*alias).name == "Local_Inputs",
                "Aliasing a module must preserve schema identity and layout");
        Invalid([&] { renamed.package->GetBlock(*binding); });
        const auto nextTexture =
            renamed.package->SemanticBinding(next, shader::Property::BindingSemantic::BrdfLut);
        Require(nextTexture &&
                    renamed.package->GetResource(*nextTexture).property.symbol == "AnotherLut",
                "Resource aliases must preserve their semantic binding contract");
        Invalid([&] { renamed.package->GetResource(*texture); });
        std::cout << "Generated CPU/HLSL parameter contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
