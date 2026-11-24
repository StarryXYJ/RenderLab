#include "renderer/project_module.hpp"

#include <Windows.h>

#include <exception>
#include <filesystem>
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
        Require(argc == 12 || argc == 13,
                "Expected eleven project module fixtures and optional incompatible fixture");
        using renderlab::renderer::ProjectModules;
        std::filesystem::path paths[11];
        for (int i = 0; i < 11; ++i)
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
        if (argc == 13)
        {
            const auto incompatible = std::filesystem::absolute(argv[12]);
            Fails([&] { ProjectModules invalid({incompatible}); }, "ABI");
        }
        std::cout << "Project module contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
