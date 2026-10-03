#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace engine {
// A bounded CPU executor. GPU work never enters this pool. Submission from a
// pool worker is rejected: nested blocking jobs otherwise deadlock a full pool.
class JobSystem {
  public:
    explicit JobSystem(size_t workers = 0, size_t capacity = 128);
    ~JobSystem();
    JobSystem(const JobSystem &) = delete;
    JobSystem &operator=(const JobSystem &) = delete;
    template <class F> auto submit(F &&function) -> std::future<decltype(function())> {
        using Result = decltype(function());
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<F>(function));
        auto future = task->get_future();
        enqueue([task] { (*task)(); });
        return future;
    }
    // Streaming uses tryEnqueue to avoid blocking the render thread on backlog.
    bool tryEnqueue(std::function<void()>);
    void shutdown(); // Stop accepting, drain, join. Idempotent, caller is not a worker.
    size_t pending() const;
    size_t capacity() const { return capacity_; }
    static JobSystem &io();

  private:
    void enqueue(std::function<void()>);
    void run();
    static thread_local const JobSystem *current_;
    size_t capacity_;
    mutable std::mutex mutex_;
    std::mutex shutdownMutex_;
    std::condition_variable ready_, space_;
    std::deque<std::function<void()>> queue_;
    std::vector<std::thread> workers_;
    bool stopping_ = false;
};
} // namespace engine
