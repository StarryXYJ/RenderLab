#include "shader/reload.hpp"

#include <algorithm>

namespace renderlab::shader
{
ReloadCompiler::ReloadCompiler() : worker_([this](std::stop_token stop) { Run(stop); }) {}
ReloadCompiler::~ReloadCompiler()
{
    worker_.request_stop();
    changed_.notify_all();
    // jthread joins before the synchronization members are destroyed.
}
uint64_t ReloadCompiler::Request(std::filesystem::path source, CompileOptions options)
{
    std::lock_guard lock(mutex_);
    latest_ = std::max(latest_ + 1, options.generation);
    options.generation = latest_;
    request_ = RequestData{std::move(source), std::move(options)};
    result_.reset();
    changed_.notify_all();
    return latest_;
}
std::optional<BuildResult> ReloadCompiler::Poll()
{
    std::lock_guard lock(mutex_);
    auto result = std::move(result_);
    result_.reset();
    return result;
}
void ReloadCompiler::Run(std::stop_token stop)
{
    while (!stop.stop_requested())
    {
        RequestData request;
        {
            std::unique_lock lock(mutex_);
            if (!changed_.wait(lock, stop, [&] { return request_.has_value(); }))
                return;
            request = std::move(*request_);
            request_.reset();
        }
        auto result = CompileShader(request.source, request.options);
        std::lock_guard lock(mutex_);
        if (!stop.stop_requested() && request.options.generation == latest_)
            result_ = std::move(result);
    }
}
} // namespace renderlab::shader
