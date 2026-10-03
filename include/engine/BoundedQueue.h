#pragma once
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>
#include <cstddef>
#include <stdexcept>
namespace engine {
// Lossless backpressure, used for logic -> render packets. Closing wakes both
// sides and the consumer drains queued packets before returning nullopt.
template <class T> class BoundedQueue {
  public:
    explicit BoundedQueue(size_t capacity) : capacity_(capacity) {
        if (!capacity)
            throw std::invalid_argument("Empty queue capacity");
    }
    bool push(T value) {
        std::unique_lock<std::mutex> lock(mutex_);
        space_.wait(lock, [this] { return closed_ || queue_.size() < capacity_; });
        if (closed_)
            return false;
        queue_.push_back(std::move(value));
        ready_.notify_one();
        return true;
    }
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [this] { return closed_ || !queue_.empty(); });
        if (queue_.empty())
            return {};
        T result = std::move(queue_.front());
        queue_.pop_front();
        space_.notify_one();
        return result;
    }
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        ready_.notify_all();
        space_.notify_all();
    }
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

  private:
    size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable ready_, space_;
    std::deque<T> queue_;
    bool closed_ = false;
};
} // namespace engine
