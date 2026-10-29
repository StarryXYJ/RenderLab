#pragma once

#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace renderlab::rhi
{
/// Runtime-local persistent native recording workers. Run/destruction require the owner thread.
/// Job zero runs on the caller; exceptions propagate only after all jobs have joined.
class RecordingWorkers final
{
public:
    ~RecordingWorkers();
    void Run(size_t count, const std::function<void(size_t)>& job);

private:
    void Worker(size_t index, size_t generation);
    void Invoke(size_t index) noexcept;
    std::mutex mutex_;
    std::condition_variable ready_, done_;
    std::vector<std::thread> workers_;
    std::vector<std::exception_ptr> errors_;
    std::function<void(size_t)> job_;
    size_t generation_ = 0, count_ = 0, remaining_ = 0;
    bool stopping_ = false;
};
} // namespace renderlab::rhi
