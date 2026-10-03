#include "engine/AssetCache.h"
#include "engine/BoundedQueue.h"
#include "engine/JobSystem.h"
#include "engine/RenderGraph.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
using namespace engine;
static void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        {
            AssetCache<int> cache;
            std::atomic<int> decoded{0};
            std::promise<void> gate;
            auto start = gate.get_future().share();
            std::vector<std::future<std::shared_ptr<int>>> consumers;
            for (int i = 0; i < 16; ++i)
                consumers.push_back(std::async(std::launch::async, [&] {
                    start.wait();
                    return cache.get("shared", [&] {
                        ++decoded;
                        return std::make_shared<int>(42);
                    });
                }));
            gate.set_value();
            auto first = consumers.front().get();
            for (size_t i = 1; i < consumers.size(); ++i)
                check(consumers[i].get() == first, "Same-key decode was duplicated");
            check(decoded == 1 && *first == 42, "Cache coalescing failed");
            check(cache.releaseUnused() == 0, "Cache evicted externally owned asset");
            first.reset();
            check(cache.releaseUnused() == 1 && cache.size() == 0, "Unused asset cache not released");
            auto a = std::async(std::launch::async,
                                [&] { return cache.get("a", [] { return std::make_shared<int>(1); }); });
            auto b = std::async(std::launch::async,
                                [&] { return cache.get("b", [] { return std::make_shared<int>(2); }); });
            check(*a.get() == 1 && *b.get() == 2, "Independent cache keys lost");
            bool failed = false;
            try {
                cache.get("retry",
                          []() -> std::shared_ptr<int> { throw std::runtime_error("decode failure"); });
            } catch (const std::runtime_error &) {
                failed = true;
            }
            check(failed && !cache.find("retry") &&
                      *cache.get("retry", [] { return std::make_shared<int>(3); }) == 3,
                  "Failed cache entry poisoned retry");
        }
        {
            JobSystem jobs(1, 2);
            std::promise<void> running, release;
            auto gate = release.get_future().share();
            auto active = jobs.submit([&] {
                running.set_value();
                gate.wait();
                return 7;
            });
            running.get_future().wait();
            std::atomic<int> drained{0};
            check(jobs.tryEnqueue([&] { ++drained; }) && jobs.tryEnqueue([&] { ++drained; }) &&
                      !jobs.tryEnqueue([] {}),
                  "Job backlog exceeded capacity");
            auto closing = std::async(std::launch::async, [&] { jobs.shutdown(); });
            release.set_value();
            closing.get();
            check(active.get() == 7 && drained == 2 && !jobs.tryEnqueue([] {}),
                  "Shutdown lost accepted jobs or accepted late jobs");
            jobs.shutdown();
        }
        {
            JobSystem jobs(1, 2);
            auto failure = jobs.submit([]() -> int { throw std::runtime_error("worker failure"); });
            bool caught = false;
            try {
                failure.get();
            } catch (const std::runtime_error &) {
                caught = true;
            }
            check(caught && jobs.submit([] { return 9; }).get() == 9, "Worker failure terminated executor");
            auto nested = jobs.submit([&] { return jobs.submit([] { return 0; }).get(); });
            caught = false;
            try {
                nested.get();
            } catch (const std::logic_error &) {
                caught = true;
            }
            check(caught, "Nested same-pool wait did not reject deadlock");
        }
        {
            RenderGraph graph;
            using A = RenderGraph::Access;
            std::vector<int> recorded;
            graph.import("assets");
            graph.add("geometry", {{"assets", A::Read}, {"depth", A::Write}}, [&] { recorded.push_back(1); });
            graph.add("lighting", {{"depth", A::Read}, {"hdr", A::Write}}, [&] { recorded.push_back(2); });
            graph.add("blend", {{"hdr", A::ReadWrite}}, [&] { recorded.push_back(3); });
            graph.execute();
            check(recorded == std::vector<int>({1, 2, 3}), "Render graph changed dependency order");
            RenderGraph invalid;
            invalid.add("invalid", {{"missing", A::Read}}, [&] { recorded.push_back(4); });
            bool rejected = false;
            try {
                invalid.execute();
            } catch (const std::logic_error &) {
                rejected = true;
            }
            check(rejected && recorded.size() == 3, "Render graph recorded before initialization validation");
            RenderGraph feedback;
            feedback.import("hdr");
            feedback.add("feedback", {{"hdr", A::Read}, {"hdr", A::Write}}, [] {});
            rejected = false;
            try {
                feedback.validate();
            } catch (const std::logic_error &) {
                rejected = true;
            }
            check(rejected, "Render graph accepted ambiguous feedback");
        }
        {
            BoundedQueue<int> queue(2);
            check(queue.push(1) && queue.push(2), "Queue refused capacity");
            auto producer = std::async(std::launch::async, [&] { return queue.push(3); });
            check(producer.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout &&
                      queue.size() == 2,
                  "Queue ignored backpressure");
            check(queue.pop() == 1 && producer.get() && queue.pop() == 2 && queue.pop() == 3,
                  "Queue lost FIFO ordering");
            auto consumer = std::async(std::launch::async, [&] { return queue.pop(); });
            queue.close();
            check(!consumer.get() && !queue.push(4), "Queue close did not wake waiting consumer");
            BoundedQueue<int> full(1);
            full.push(1);
            auto blocked = std::async(std::launch::async, [&] { return full.push(2); });
            full.close();
            check(!blocked.get() && full.pop() == 1 && !full.pop(),
                  "Queue close did not wake producer or drain accepted packet");
        }
        std::cout << "Cache coalescing/retry/release, bounded jobs/error/drain and frame queue backpressure "
                     "passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
