#include "renderer/project_module.hpp"
#include "renderer/project_settings.hpp"

#include <Windows.h>

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void Require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void Fails(F operation, const std::string& message)
{
    try
    {
        operation();
    }
    catch (const std::exception& error)
    {
        Require(std::string(error.what()).find(message) != std::string::npos, error.what());
        return;
    }
    throw std::runtime_error("Expected failure: " + message);
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    try
    {
        Require(argc == 13 || argc == 14,
                "Expected twelve project module fixtures and optional incompatible fixture");
        using renderlab::renderer::ProjectModules;
        std::filesystem::path paths[12];
        for (int i = 0; i < 12; ++i)
            paths[i] = std::filesystem::absolute(argv[i + 1]);
        {
            ProjectModules registry({paths[0], paths[8]});
            Require(registry.Pipelines().size() == 2, "Both pipelines must be registered");
            Require(registry.Pipeline("test.other.pipeline").nodes.at(0) == "test.demo.node",
                    "Cross-module node references must resolve");
            int result = 0;
            const auto& node = registry.Node("test.demo.node");
            Require(node.build(node.user, &result, nullptr, nullptr, nullptr, 0) == 0 &&
                        result == 42,
                    "Registered callback must remain callable");
            Fails([&] { registry.Node("missing"); }, "Unknown project node");
            Fails([&] { registry.Pipeline("missing"); }, "Unknown project pipeline");
            Require(GetModuleHandleW(paths[0].filename().c_str()) != nullptr,
                    "Loaded callback library must remain resident");
        }
        Require(GetModuleHandleW(paths[0].filename().c_str()) == nullptr,
                "Module must unload after registry destruction");
        Fails([&] { ProjectModules invalid({paths[1]}); }, "ABI");
        Fails([&] { ProjectModules invalid({paths[2]}); }, "Duplicate project node");
        Fails([&] { ProjectModules invalid({paths[3]}); }, "Missing node");
        Fails([&] { ProjectModules invalid({paths[4]}); }, "node descriptor");
        Fails([&] { ProjectModules invalid({paths[0], paths[5]}); }, "Duplicate project node");
        Fails([&] { ProjectModules invalid({paths[6]}); }, "entry is missing");
        Fails([&] { ProjectModules invalid({paths[0], paths[7]}); }, "Duplicate project pipeline");
        Fails([&] { ProjectModules invalid({paths[0], paths[0]}); }, "Duplicate project module");
        Fails([&] { ProjectModules invalid({paths[0].parent_path() / L"missing.dll"}); },
              "Cannot load project module");
        Fails([&] { ProjectModules invalid({paths[9]}); }, "ABI");
        Fails([&] { ProjectModules invalid({paths[10]}); }, "ABI");
        Fails([&] { ProjectModules invalid({paths[11]}); }, "reserved renderlab namespace");
        if (argc == 14)
        {
            const auto incompatible = std::filesystem::absolute(argv[13]);
            Fails([&] { ProjectModules invalid({incompatible}); }, "ABI");
        }
        const auto descriptor =
            std::filesystem::temp_directory_path() /
            (L"renderlab-pipelines-" + std::to_wstring(GetCurrentProcessId()) + L".json");
        const auto write = [&](const char* value)
        {
            std::ofstream file(descriptor, std::ios::binary | std::ios::trunc);
            file << value;
            if (!file)
                throw std::runtime_error("Cannot write pipeline test descriptor");
        };
        try
        {
            write(
                R"({"version":1,"pipelines":[{"id":"test.managed","base":"forward","nodes":["test.demo.node"]}]})");
            {
                ProjectModules registry({paths[0]}, descriptor);
                Require(registry.Pipelines().size() == 2 &&
                            registry.Pipeline("test.managed").nodes.at(0) == "test.demo.node",
                        "Described and module pipelines must share one registry");
            }
            write(
                R"({"version":1,"pipelines":[{"id":"test.demo.pipeline","base":"forward","nodes":[]}]})");
            Fails([&] { ProjectModules invalid({paths[0]}, descriptor); },
                  "Duplicate project pipeline");
            write(
                R"({"version":1,"pipelines":[{"id":"test.missing","base":"forward","nodes":["missing"]}]})");
            Fails([&] { ProjectModules invalid({paths[0]}, descriptor); }, "Missing node");
            write(
                R"({"version":1,"pipelines":[{"id":"test.repeat","base":"forward","nodes":["test.demo.node","test.demo.node"]}]})");
            Fails([&] { ProjectModules invalid({paths[0]}, descriptor); },
                  "Duplicate project node");
            write(R"({"version":2,"pipelines":[]})");
            Fails([&] { ProjectModules invalid({paths[0]}, descriptor); },
                  "Invalid project pipelines");
            write(
                R"({"version":1,"pipelines":[{"id":"bad\u0000id","base":"forward","nodes":[]}]})");
            Fails([&] { ProjectModules invalid({paths[0]}, descriptor); },
                  "Invalid project module ID");
        }
        catch (...)
        {
            std::filesystem::remove(descriptor);
            throw;
        }
        std::filesystem::remove(descriptor);
        using renderlab::renderer::ParseProjectSettings;
        const auto settings = ParseProjectSettings(
            R"({"version":1,"values":{"enabled":true,"count":3,"gain":1.25,"name":"test"}})");
        Require(settings->version == 1 && std::get<bool>(settings->values.at("enabled")) &&
                    std::get<int32_t>(settings->values.at("count")) == 3 &&
                    std::get<double>(settings->values.at("gain")) == 1.25 &&
                    std::get<std::string>(settings->values.at("name")) == "test",
                "Project settings values must retain their supported types");
        Fails([] { ParseProjectSettings(R"({"version":0,"values":{}})"); }, "version");
        Fails([] { ParseProjectSettings(R"({"version":1,"values":{"x":1,"x":2}})"); }, "Duplicate");
        const auto wide = ParseProjectSettings(R"({"version":1,"values":{"x":4294967295}})");
        Require(std::get<double>(wide->values.at("x")) == 4294967295.0,
                "Integral representations of double values must be supported");
        Fails([] { ParseProjectSettings(R"({"version":1,"values":{"Bad":true}})"); }, "ID");
        Fails([] { ParseProjectSettings(R"({"version":1,"values":{"x":null}})"); }, "Unsupported");
        Fails([] { ParseProjectSettings(std::string(65537, 'x')); }, "budget");
        std::cout << "Project module contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
