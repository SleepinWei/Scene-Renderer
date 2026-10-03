#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <optional>
namespace engine {
enum class CommandStatus { Applied, MissingTarget, StaleWorld, Cancelled, QueueFull, Closed, Invalid };
struct CommandResult {
    CommandStatus status = CommandStatus::Applied;
    std::string message;
};
struct CommandTicket {
    std::future<CommandResult> result;
    std::shared_ptr<std::atomic<bool>> cancelled;
    void cancel() const {
        if (cancelled)
            cancelled->store(true);
    }
};
template <class T> class CommandInbox;
template <class T> struct CommandPort {
    std::weak_ptr<CommandInbox<T>> inbox;
    uint64_t generation = 0;
    CommandTicket post(T value) const {
        if (auto sink = inbox.lock())
            return sink->post(std::move(value), generation);
        std::promise<CommandResult> result;
        CommandTicket ticket{result.get_future(), std::make_shared<std::atomic<bool>>(false)};
        result.set_value({CommandStatus::Closed, "World no longer exists"});
        return ticket;
    }
};
// Multiple producers post values without waiting. Only the owning logic thread
// drains. The executor runs outside the mutex; each command has its own result.
template <class T> class CommandInbox : public std::enable_shared_from_this<CommandInbox<T>> {
  public:
    explicit CommandInbox(size_t capacity = 256) : capacity_(capacity) {
        if (!capacity)
            throw std::invalid_argument("Command capacity must be positive");
    }
    ~CommandInbox() { close(); }
    CommandTicket post(T value, std::optional<uint64_t> expectedGeneration = {}) {
        auto result = std::make_shared<std::promise<CommandResult>>();
        CommandTicket ticket{result->get_future(), std::make_shared<std::atomic<bool>>(false)};
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_)
            result->set_value({CommandStatus::Closed, "World command inbox closed"});
        else if (expectedGeneration && *expectedGeneration != generation_)
            result->set_value({CommandStatus::StaleWorld, "Command belongs to an earlier world"});
        else if (queue_.size() >= capacity_)
            result->set_value({CommandStatus::QueueFull, "World command inbox full"});
        else
            queue_.push_back({std::move(value), result, ticket.cancelled});
        return ticket;
    }
    template <class F> size_t drain(size_t limit, F &&execute) {
        checkConsumer();
        size_t count = 0;
        while (count < limit) {
            std::unique_lock<std::mutex> lock(mutex_);
            if (queue_.empty())
                break;
            auto pending = std::move(queue_.front());
            queue_.pop_front();
            lock.unlock();
            ++count;
            try {
                if (pending.cancelled->load())
                    pending.result->set_value(
                        {CommandStatus::Cancelled, "Command cancelled before execution"});
                else
                    pending.result->set_value(execute(pending.value));
            } catch (...) {
                pending.result->set_exception(std::current_exception());
            }
        }
        return count;
    }
    // A publication boundary invalidates all earlier pending work atomically.
    void invalidate() {
        checkConsumer();
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;
        for (auto &pending : queue_)
            pending.result->set_value({CommandStatus::StaleWorld, "World replaced before command execution"});
        queue_.clear();
    }
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        for (auto &pending : queue_)
            pending.result->set_value({CommandStatus::Closed, "World closed before command execution"});
        queue_.clear();
    }
    size_t pending() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
    CommandPort<T> port() {
        checkConsumer();
        std::lock_guard<std::mutex> lock(mutex_);
        return {this->shared_from_this(), generation_};
    }

  private:
    void checkConsumer() const {
        if (consumer_ != std::this_thread::get_id())
            throw std::logic_error("World commands must drain on their logic thread");
    }
    struct Pending {
        T value;
        std::shared_ptr<std::promise<CommandResult>> result;
        std::shared_ptr<std::atomic<bool>> cancelled;
    };
    const size_t capacity_;
    const std::thread::id consumer_ = std::this_thread::get_id();
    mutable std::mutex mutex_;
    std::deque<Pending> queue_;
    bool closed_ = false;
    uint64_t generation_ = 1;
};
} // namespace engine
