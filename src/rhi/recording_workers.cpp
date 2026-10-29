#include "rhi/recording_workers.hpp"

#include <stdexcept>

namespace renderlab::rhi
{
RecordingWorkers::~RecordingWorkers()
{
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    ready_.notify_all();
    for (auto& worker : workers_)
        worker.join();
}
void RecordingWorkers::Invoke(size_t index) noexcept
{
    try
    {
        job_(index);
    }
    catch (...)
    {
        errors_[index] = std::current_exception();
    }
}
void RecordingWorkers::Worker(size_t index, size_t generation)
{
    std::unique_lock lock(mutex_);
    for (;;)
    {
        ready_.wait(lock, [&] { return stopping_ || generation_ != generation; });
        if (stopping_)
            return;
        generation = generation_;
        if (index >= count_)
            continue;
        lock.unlock();
        Invoke(index);
        lock.lock();
        if (--remaining_ == 0)
            done_.notify_one();
    }
}
void RecordingWorkers::Run(size_t count, const std::function<void(size_t)>& job)
{
    if (!count || count > 16 || !job)
        throw std::invalid_argument("Native recording job count must be in [1, 16]");
    std::unique_lock lock(mutex_);
    while (workers_.size() + 1 < count)
    {
        const auto index = workers_.size() + 1;
        workers_.emplace_back([this, index, generation = generation_]
                              { Worker(index, generation); });
    }
    errors_.assign(count, {});
    job_ = job;
    count_ = count;
    remaining_ = count - 1;
    ++generation_;
    lock.unlock();
    ready_.notify_all();
    Invoke(0);
    lock.lock();
    done_.wait(lock, [&] { return remaining_ == 0; });
    job_ = {};
    for (const auto& error : errors_)
        if (error)
            std::rethrow_exception(error);
}
} // namespace renderlab::rhi
