#pragma once
#include <future>
#include <stdexcept>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
namespace engine {
// Lock the index only. Coalesce identical decodes and propagate failures; failed
// entries are removed so future requests can retry. Decoder must not reenter its key.
template <class T> class AssetCache {
    struct Entry {
        std::shared_ptr<T> value;
        std::shared_future<std::shared_ptr<T>> loading;
    };

  public:
    template <class F> std::shared_ptr<T> get(const std::string &key, F &&decode) {
        std::shared_ptr<Entry> entry;
        std::shared_ptr<std::promise<std::shared_ptr<T>>> producer;
        std::shared_future<std::shared_ptr<T>> future;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto &current = entries_[key];
            if (!current)
                current = std::make_shared<Entry>();
            entry = current;
            if (entry->value)
                return entry->value;
            if (!entry->loading.valid()) {
                producer = std::make_shared<std::promise<std::shared_ptr<T>>>();
                entry->loading = producer->get_future().share();
            }
            future = entry->loading;
        }
        if (!producer)
            return future.get();
        try {
            auto value = decode();
            if (!value)
                throw std::runtime_error("Asset decoder returned null");
            {
                std::lock_guard<std::mutex> lock(mutex_);
                entry->value = value;
                entry->loading = {};
            }
            producer->set_value(value);
            return value;
        } catch (...) {
            auto error = std::current_exception();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = entries_.find(key);
                if (it != entries_.end() && it->second == entry)
                    entries_.erase(it);
            }
            producer->set_exception(error);
            std::rethrow_exception(error);
        }
    }
    std::shared_ptr<T> find(const std::string &key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(key);
        return it == entries_.end() ? nullptr : it->second->value;
    }
    size_t releaseUnused() {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t count = 0;
        for (auto it = entries_.begin(); it != entries_.end();)
            if (it->second->value && it->second->value.use_count() == 1) {
                it = entries_.erase(it);
                ++count;
            } else
                ++it;
        return count;
    }
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }

  private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<Entry>> entries_;
};
} // namespace engine
