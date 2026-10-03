#include "engine/JobSystem.h"
#include <algorithm>
#include <stdexcept>
namespace engine {
thread_local const JobSystem *JobSystem::current_ = nullptr;
JobSystem::JobSystem(size_t workers, size_t capacity) : capacity_(capacity) {
    if (!capacity)
        throw std::invalid_argument("Job queue capacity must be positive");
    if (!workers) {
        auto cores = std::thread::hardware_concurrency();
        workers = cores > 2 ? cores - 2 : 1;
    }
    workers = std::clamp(workers, size_t(1), size_t(8));
    try {
        for (size_t i = 0; i < workers; ++i)
            workers_.emplace_back([this] { run(); });
    } catch (...) {
        shutdown();
        throw;
    }
}
JobSystem::~JobSystem() { shutdown(); }
void JobSystem::enqueue(std::function<void()> task) {
    if (current_ == this)
        throw std::logic_error("Nested blocking submission to the same job pool");
    std::unique_lock<std::mutex> lock(mutex_);
    space_.wait(lock, [this] { return stopping_ || queue_.size() < capacity_; });
    if (stopping_)
        throw std::logic_error("Job pool stopped");
    queue_.push_back(std::move(task));
    ready_.notify_one();
}
bool JobSystem::tryEnqueue(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || queue_.size() >= capacity_)
        return false;
    queue_.push_back(std::move(task));
    ready_.notify_one();
    return true;
}
void JobSystem::run() {
    current_ = this;
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty())
                break;
            task = std::move(queue_.front());
            queue_.pop_front();
            space_.notify_one();
        }
        // tryEnqueue tasks own their error channel. Do not terminate the executor.
        try {
            task();
        } catch (...) {
        }
    }
    current_ = nullptr;
}
void JobSystem::shutdown() {
    if (current_ == this)
        std::terminate();
    std::lock_guard<std::mutex> shutdownLock(shutdownMutex_);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    ready_.notify_all();
    space_.notify_all();
    for (auto &worker : workers_)
        if (worker.joinable())
            worker.join();
}
size_t JobSystem::pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}
JobSystem &JobSystem::io() {
    static JobSystem pool(2, 64);
    return pool;
}
} // namespace engine
