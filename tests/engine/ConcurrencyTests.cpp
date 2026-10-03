#include "engine/AssetCache.h"
#include "engine/AssetPath.h"
#include "engine/BoundedQueue.h"
#include "engine/JobSystem.h"
#include "engine/RenderGraph.h"
#include "engine/FixedStepClock.h"
#include "engine/QualityPolicy.h"
#include "engine/CommandInbox.h"
#include "engine/LogicAsset.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <array>
using namespace engine;
static void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        {
            BoundedQueue<int> queue(2);check(queue.tryPush(1) && queue.tryPush(2) && !queue.tryPush(3),"Nonblocking render queue exceeded capacity");
            check(queue.pop()==1 && queue.tryPush(4) && queue.pop()==2 && queue.pop()==4,"Skipped snapshot changed accepted ordering");
            queue.close();check(!queue.tryPush(5) && !queue.pop(),"Closed nonblocking queue accepted data or failed to drain");
        }
        {
            QualityPolicy quality;check(!quality.onPressure() && quality.fft(2048)==2048,"Quality policy changed requested settings by default");
            quality.setEnabled(true);check(quality.onPressure() && quality.fft(1024)==512 && quality.terrainLeaves(2048)==1024,"Quality policy did not reduce resource allocations");
            quality.onPressure();quality.onPressure();check(!quality.onPressure() && quality.level()==3 && quality.virtualColumns(8)==2,"Quality tiers are unbounded");
            quality.setEnabled(false);check(quality.level()==0 && quality.oceanMesh(257)==257,"Disabling automatic quality did not restore requested settings");
        }
        {
            auto cancelled=std::make_shared<std::atomic<bool>>(true);
            {CancellationScope scope(cancelled);
             bool rejected=false;try{CancellationScope::check();}catch(const std::runtime_error&){rejected=true;}
             check(rejected,"Cancelled decode continued");
             {CancellationScope nested({});check(!CancellationScope::cancelled(),"Nested cancellation scope lost isolation");}
             check(CancellationScope::cancelled(),"Nested cancellation scope did not restore previous flag");
             check(!std::async(std::launch::async,[]{return CancellationScope::cancelled();}).get(),"Cancellation leaked between worker threads");}
            check(!CancellationScope::cancelled(),"Cancellation scope leaked after job completion");
        }
        {
            FixedStepClock a,b;unsigned ticks=0;
            a.advance(.1,[&](double dt){check(std::abs(dt-1.0/60)<1e-12,"Variable logic step");++ticks;});
            for(int i=0;i<10;++i)b.advance(.01,[](double){});
            check(ticks==6 && std::abs(a.seconds()-b.seconds())<1e-12,"Logic depends on render partition");
            a.setPaused(true);a.advance(100,[](double){throw std::runtime_error("Paused clock ticked");});
            a.setPaused(false);check(a.advance(10,[](double){})==8 && a.droppedSeconds()>9,"Slow GPU causes unbounded logic catch-up");
            auto before=b.seconds();b.setSpeed(2);b.advance(.05,[](double){});
            check(std::abs(b.seconds()-before-.1)<1e-12,"Simulation speed changes the fixed timestep");
        }
        {
            RenderGraph graph;graph.describe("mips",{2,2,false,{}});
            graph.add("mip0",{{"mips",RenderGraph::Access::Write,{0,1,0,2}}},[]{});
            graph.add("mip1",{{"mips",RenderGraph::Access::Read,{0,1,0,2}},
                              {"mips",RenderGraph::Access::Write,{1,1,0,2}}},[]{});
            auto plan=graph.compile();check(plan.dependencies[1].count(0),"Graph omitted subresource hazard");
            graph.add("bad-layer",{{"mips",RenderGraph::Access::Read,{1,1,2,1}}},[]{});
            bool denied=false;try{graph.validate();}catch(const std::out_of_range&){denied=true;}
            check(denied,"Graph accepted an invalid texture layer");
            RenderGraph uninitialized;uninitialized.describe("mips",{2,1,false,{}});
            uninitialized.import("mips",{0,1,0,1});
            uninitialized.add("read-other-mip",{{"mips",RenderGraph::Access::Read,{1,1,0,1}}},[]{});
            denied=false;try{uninitialized.validate();}catch(const std::logic_error&){denied=true;}
            check(denied,"Importing one mip initialized another");
            RenderGraph transient;
            for(auto name:{"a","b","c"})transient.describe(name,{1,1,true,"rgba16-64x64"});
            transient.add("first",{{"a",RenderGraph::Access::Write},{"c",RenderGraph::Access::Write}},[]{});
            transient.add("consume",{{"a",RenderGraph::Access::Read}},[]{});
            transient.add("second",{{"b",RenderGraph::Access::Write},{"c",RenderGraph::Access::Read}},[]{});
            auto aliases=transient.compile();size_t slotA=SIZE_MAX,slotB=SIZE_MAX,slotC=SIZE_MAX;
            for(auto& life:aliases.lifetimes){if(life.resource=="a")slotA=life.aliasSlot;if(life.resource=="b")slotB=life.aliasSlot;if(life.resource=="c")slotC=life.aliasSlot;}
            check(slotA==slotB && slotA!=slotC,"Transient plan aliases overlapping lifetimes or misses reuse");
        }
        {
            LogicAsset asset;
            const auto revision=asset.getContentRevision();asset.invalidate();
            LogicAsset copy(asset);
            check(copy.assetId!=asset.assetId && copy.getContentRevision()==revision+1,
                  "Logic asset copy reused identity or lost content version");
            auto wrong=std::async(std::launch::async,[&] {
                unsigned denied=0;
                try {asset.invalidate();} catch(const std::logic_error&) {++denied;}
                try {asset.getContentRevision();} catch(const std::logic_error&) {++denied;}
                try {LogicAsset copy(asset);} catch(const std::logic_error&) {++denied;}
                return denied==3;
            });
            check(wrong.get() && asset.getContentRevision()==revision+1,
                  "Foreign asset API accessed or mutated live state");
        }
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
            auto inbox = std::make_shared<CommandInbox<int>>(2);
            auto old = inbox->port();
            auto first = old.post(1), second = old.post(2), full = old.post(3);
            check(full.result.get().status == CommandStatus::QueueFull && inbox->pending() == 2,
                  "Command inbox exceeded capacity or blocked its producer");
            std::vector<int> applied;
            auto execute = [&](int value) {
                applied.push_back(value);
                return CommandResult{};
            };
            check(inbox->drain(1, execute) == 1 && first.result.get().status == CommandStatus::Applied &&
                      inbox->pending() == 1,
                  "Command drain ignored its per-tick limit");
            inbox->invalidate();
            check(second.result.get().status == CommandStatus::StaleWorld &&
                      old.post(4).result.get().status == CommandStatus::StaleWorld,
                  "World replacement accepted pending or late work from an old port");
            auto current = inbox->port();
            auto cancelled = current.post(5);
            cancelled.cancel();
            inbox->drain(1, execute);
            check(cancelled.result.get().status == CommandStatus::Cancelled && applied == std::vector<int>{1},
                  "Cancelled command changed the world");
            auto failure = current.post(6), after = current.post(7);
            inbox->drain(2, [&](int value) {
                if (value == 6)
                    throw std::runtime_error("command failure");
                return execute(value);
            });
            bool rejected = false;
            try {
                failure.result.get();
            } catch (const std::runtime_error &) {
                rejected = true;
            }
            check(rejected && after.result.get().status == CommandStatus::Applied && applied.back() == 7,
                  "Command failure lost its result or stopped later work");
            auto pending = current.post(8);
            inbox->close();
            check(pending.result.get().status == CommandStatus::Closed &&
                      current.post(9).result.get().status == CommandStatus::Closed,
                  "Close left command futures unresolved");
            inbox.reset();
            check(current.post(10).result.get().status == CommandStatus::Closed,
                  "Expired port accessed destroyed world");
        }
        {
            auto inbox = std::make_shared<CommandInbox<int>>(128);
            auto port = inbox->port();
            std::vector<std::future<std::vector<CommandTicket>>> producers;
            for (int producer = 0; producer < 4; ++producer)
                producers.push_back(std::async(std::launch::async, [port, producer] {
                    std::vector<CommandTicket> tickets;
                    for (int i = 0; i < 16; ++i)
                        tickets.push_back(port.post(producer * 100 + i));
                    return tickets;
                }));
            std::vector<std::vector<CommandTicket>> tickets;
            for (auto &producer : producers)
                tickets.push_back(producer.get());
            std::array<int, 4> next{};
            check(inbox->drain(128,
                               [&](int value) {
                                   check(value % 100 == next[value / 100]++,
                                         "Command inbox reordered a producer's messages");
                                   return CommandResult{};
                               }) == 64,
                  "Concurrent producers lost commands");
            for (auto &group : tickets)
                for (auto &ticket : group)
                    check(ticket.result.get().status == CommandStatus::Applied,
                          "Concurrent command lost result");
            auto foreign = std::async(std::launch::async, [&] {
                try {
                    inbox->drain(1, [](int) { return CommandResult{}; });
                } catch (const std::logic_error &) {
                    return true;
                }
                return false;
            });
            check(foreign.get(), "Commands executed outside their logic thread");
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
