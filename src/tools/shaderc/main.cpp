#include "platform/text.hpp"
#include "shader/compiler.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int wmain(int argc, wchar_t** argv)
{
    renderlab::platform::ConfigureConsoleUtf8();
    try
    {
        if (argc < 2)
        {
            std::cout << "renderlab_shaderc FILE.shader [--cache DIRECTORY] [--schema FILE] "
                         "[--target dxil|spirv|both] [--include DIRECTORY]\n";
            return 2;
        }
        renderlab::shader::CompileOptions options;
        options.cacheDirectory = "cache/shaders";
        std::filesystem::path output;
        for (int i = 2; i < argc; ++i)
        {
            const std::wstring option = argv[i];
            if (++i >= argc)
                throw std::invalid_argument("Missing option value");
            if (option == L"--cache")
                options.cacheDirectory = argv[i];
            else if (option == L"--include")
                options.includeDirectories.emplace_back(argv[i]);
            else if (option == L"--schema")
                output = argv[i];
            else if (option == L"--target")
            {
                const std::wstring target = argv[i];
                if (target == L"dxil")
                    options.targets = {renderlab::shader::Target::Dxil};
                else if (target == L"spirv")
                    options.targets = {renderlab::shader::Target::Spirv};
                else if (target != L"both")
                    throw std::invalid_argument("Unknown target");
            }
            else
                throw std::invalid_argument("Unknown option");
        }
        const auto result = renderlab::shader::CompileShader(argv[1], options);
        if (!result)
        {
            std::cerr << result.diagnostics << '\n';
            return 1;
        }
        const auto json = renderlab::shader::SchemaToJson(*result.package);
        if (output.empty())
            std::cout << json;
        else
        {
            if (output.has_parent_path())
                std::filesystem::create_directories(output.parent_path());
            std::ofstream file(output, std::ios::binary | std::ios::trunc);
            file << json;
            if (!file)
                throw std::runtime_error("Cannot write schema");
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
