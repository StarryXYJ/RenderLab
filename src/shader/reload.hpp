#pragma once

#include "shader/compiler.hpp"

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>

namespace renderlab::shader
{
/** @brief Coalesced CPU-only background builds; Poll never returns superseded generations. */
class ReloadCompiler final
{
public:
    ReloadCompiler();
    ~ReloadCompiler();
    uint64_t Request(std::filesystem::path source, CompileOptions options);
    std::optional<BuildResult> Poll();

private:
    void Run(std::stop_token stop);
    struct RequestData
    {
        std::filesystem::path source;
        CompileOptions options;
    };
    std::mutex mutex_;
    std::condition_variable_any changed_;
    std::optional<RequestData> request_;
    std::optional<BuildResult> result_;
    uint64_t latest_ = 0;
    std::jthread worker_;
};
} // namespace renderlab::shader
