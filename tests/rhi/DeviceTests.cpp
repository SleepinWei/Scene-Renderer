#include "rhi/Device.h"
#include <future>
#include "rhi/Validation.h"
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Error, class F> void rejects(F action) {
    try { action(); } catch (const Error&) { return; }
    throw std::runtime_error("expected contract rejection");
}
class TestDevice final : public rhi::Device {
public:
    TestDevice() : Device({4096, 1024, 256, 8}) {}
    rhi::Backend backend() const override { return rhi::Backend::Metal; }
    size_t allocations() const { return data_.size(); }
    bool manual=false;uint64_t signaled=0,completed=0;
    bool failAllocation=false;unsigned allocationAttempts=0;
    unsigned waits = 0, frames = 0, presents = 0, closes = 0;
protected:
    NativeBuffer createBufferImpl(const rhi::BufferDesc& desc, const void* bytes) override {
        ++allocationAttempts;if(failAllocation)throw std::runtime_error("native allocation failed");
        const auto id = ++next_;
        auto& data = data_[id]; data.resize(desc.size, 0);
        if (bytes) std::memcpy(data.data(), bytes, desc.size);
        return id;
    }
    void destroyBufferImpl(NativeBuffer id) noexcept override { data_.erase(id); }
    void writeBufferImpl(NativeBuffer id, size_t offset, size_t size, const void* data) override {
        std::memcpy(data_.at(id).data() + offset, data, size);
    }
    void readBufferImpl(NativeBuffer id, size_t offset, size_t size, void* data) override {
        std::memcpy(data, data_.at(id).data() + offset, size);
    }
    void bindUniformBufferImpl(uint32_t, NativeBuffer, size_t, size_t) override {}
    void beginFrameImpl() override { ++frames; }
    void presentImpl() override { ++presents; }
    uint64_t signalCompletionImpl()override{if(!manual)completed=signaled+1;return ++signaled;}
    bool completionReadyImpl(uint64_t serial)override{return serial<=completed;}
    void waitCompletionImpl(uint64_t serial)override{completed=serial;}
    void waitIdleImpl() override { ++waits;completed=signaled; }
    void closeImpl() override { ++closes; }
private:
    NativeBuffer next_ = 0;
    std::unordered_map<NativeBuffer, std::vector<uint8_t>> data_;
};
}
int main() {
    try {
        using namespace rhi;
        {
            TestDevice budget;budget.setResourceBudget(128);
            auto a=budget.createBuffer({64,BufferUsage::Vertex,"budget first"});
            const auto attempts=budget.allocationAttempts;
            rejects<ResourceBudgetExceeded>([&]{budget.createBuffer({65,BufferUsage::Vertex,"over budget"});});
            check(budget.allocationAttempts==attempts && budget.resourceMemory().usedBytes()==64,
                  "Budget rejected after native allocation or charged failed request");
            budget.failAllocation=true;
            rejects<std::runtime_error>([&]{budget.createBuffer({64,BufferUsage::Vertex,"native failure"});});
            check(budget.resourceMemory().usedBytes()==64 && budget.resourceMemory().peakBytes==64,
                  "Native allocation failure polluted memory accounting");
            budget.failAllocation=false;
            auto b=budget.createBuffer({64,BufferUsage::Vertex,"budget second"});
            rejects<std::invalid_argument>([&]{budget.setResourceBudget(127);});
            budget.destroyBuffer(a);budget.destroyBuffer(a);
            check(budget.resourceMemory().usedBytes()==64 && budget.resourceMemory().peakBytes==128,
                  "Double destroy corrupted resource budget or peak");
            auto c=budget.createBuffer({32,BufferUsage::Vertex,"retired buffer"});
            budget.manual=true;budget.beginFrame();
            budget.retireResources([&]{budget.destroyBuffer(b);budget.destroyBuffer(c);});budget.endFrame();
            rejects<ResourceBudgetExceeded>([&]{budget.createBuffer({40,BufferUsage::Vertex,"pending retirement"});});
            check(budget.resourceMemory().usedBytes()==96,"In-flight retirement released quota early");
            budget.waitIdle();check(budget.resourceMemory().usedBytes()==0,"Completed retirement retained quota");
            budget.createBuffer({128,BufferUsage::Vertex,"reclaimed quota"});budget.close();
            check(budget.resourceMemory().usedBytes()==0 && budget.resourceMemory().peakBytes==128,
                  "Close leaked resource accounting");
        }
        {
            TestDevice pressure;
            pressure.setResourceBudget(128);
            auto idle = pressure.createBuffer({64, BufferUsage::Vertex, "reclaimable"});
            auto live = pressure.createBuffer({64, BufferUsage::Vertex, "live"});
            size_t requested = 0;
            pressure.setResourcePressureHandler([&](size_t bytes) {
                requested = bytes;
                pressure.destroyBuffer(idle);
            });
            auto admitted = pressure.createBuffer({32, BufferUsage::Vertex, "after pressure"});
            check(requested == 32 && pressure.resourceMemory().usedBytes() == 96 &&
                      pressure.resourceMemory().pressureEvents == 1 &&
                      pressure.resourceMemory().pressureRecoveries == 1,
                  "Pressure did not reclaim before admission or recorded wrong recovery");
            rejects<ResourceBudgetExceeded>([&] {
                pressure.createBuffer({64, BufferUsage::Vertex, "still too large"});
            });
            pressure.setResourcePressureHandler([&](size_t) {
                pressure.createBuffer({1, BufferUsage::Vertex, "recursive allocation"});
            });
            rejects<std::logic_error>([&] {pressure.createBuffer({64, BufferUsage::Vertex, "recursive"});});
            pressure.setResourcePressureHandler({}); // Throwing handler must restore its reentry guard.
            check(pressure.resourceMemory().usedBytes() == 96,
                  "Rejected pressure request polluted accounting");
            pressure.destroyBuffer(live);
            pressure.destroyBuffer(admitted);
            pressure.close();
        }
        auto owner = std::make_shared<TestDevice>();
        TestDevice other;
        validateBufferTransfers(*owner);
        check(owner->allocations() == 0 && owner->frames == 1 && owner->presents == 1,
              "transfer validation leaked a buffer or missed frame submission");
        const auto usage = BufferUsage::Uniform | BufferUsage::CopySource | BufferUsage::CopyDestination;
        auto a = owner->createBuffer({512, usage, "contract"});
        auto b = other.createBuffer({512, usage, "foreign"});
        uint32_t data = 5;
        rejects<std::invalid_argument>([&] { other.writeBuffer(a, 0, 4, &data); });
        rejects<std::invalid_argument>([&] { owner->writeBuffer(b, 0, 4, &data); });
        rejects<std::out_of_range>([&] { owner->writeBuffer(a, 510, 4, &data); });
        rejects<std::out_of_range>([&] { owner->readBuffer(a, std::numeric_limits<size_t>::max(), 4, &data); });
        rejects<std::invalid_argument>([&] { owner->writeBuffer(a, 0, 4, nullptr); });
        rejects<std::invalid_argument>([&] { owner->readBuffer(a, 0, 4, nullptr); });
        owner->writeBuffer(a, 512, 0, nullptr);
        owner->readBuffer(a, 512, 0, nullptr);
        rejects<std::invalid_argument>([&] { owner->bindUniformBuffer(0, a, 4, 16); });
        rejects<std::invalid_argument>([&] { owner->bindUniformBuffer(0, a, 512); });
        rejects<std::out_of_range>([&] { owner->bindUniformBuffer(8, a); });
        owner->bindUniformBuffer(0, a, 256, 256);
        owner->bindUniformBuffer(0, {});
        rejects<std::invalid_argument>([&] { owner->bindUniformBuffer(0, {}, 256); });
        auto readOnly = owner->createBuffer({64, BufferUsage::CopySource, "read only"});
        rejects<std::invalid_argument>([&] { owner->writeBuffer(readOnly, 0, 4, &data); });
        rejects<std::invalid_argument>([&] { owner->bindUniformBuffer(0, readOnly); });
        auto writeOnly = owner->createBuffer({64, BufferUsage::CopyDestination, "write only"});
        rejects<std::invalid_argument>([&] { owner->readBuffer(writeOnly, 0, 4, &data); });
        rejects<std::invalid_argument>([&] { owner->createBuffer({0, usage, "empty"}); });
        rejects<std::invalid_argument>([&] { owner->createBuffer({1025, usage, "too large uniform"}); });
        rejects<std::invalid_argument>([&] { owner->createBuffer({4097, BufferUsage::Vertex, "too large"}); });
        rejects<std::invalid_argument>([&] { owner->createBuffer({64, BufferUsage::None, "missing usage"}); });
        owner->destroyBuffer(a);
        rejects<std::invalid_argument>([&] { owner->readBuffer(a, 0, 4, &data); });
        auto replacement = owner->createBuffer({512, usage, "replacement"});
        check(replacement.value != a.value, "stale handle was reused");
        installDevice(owner);
        rejects<std::logic_error>([&] { installDevice(std::make_shared<TestDevice>()); });
        shutdown();
        check(!owner->isOpen() && owner->allocations() == 0 && owner->waits == 1 && owner->closes == 1,
              "device shutdown did not release resources exactly once");
        owner->destroyBuffer(replacement); // late renderer RAII destruction
        owner->close();
        rejects<std::logic_error>([&] { owner->beginFrame(); });
        rejects<std::logic_error>([&] { owner->createBuffer({64, usage, "closed"}); });
        rejects<std::logic_error>([&] { device(); });
        TestDevice threaded;
        auto foreign=std::async(std::launch::async,[&]{rejects<std::logic_error>([&]{threaded.createBuffer({64,BufferUsage::Vertex,"wrong thread"});});});foreign.get();
        auto transfer=std::async(std::launch::async,[&]{threaded.adoptCurrentThread();auto buffer=threaded.createBuffer({64,BufferUsage::Vertex,"worker owner"});threaded.destroyBuffer(buffer);});transfer.get();threaded.adoptCurrentThread();threaded.close();
        TestDevice asynchronous;asynchronous.manual=true;int retired=0;
        std::vector<CompletionToken> tokens;
        for(int i=0;i<3;++i){asynchronous.beginFrame();asynchronous.retireResources([&]{++retired;});tokens.push_back(asynchronous.endFrame());}
        check(retired==0 && asynchronous.framesInFlight()==3,"in-flight resources released before completion");
        asynchronous.beginFrame();check(retired==1 && asynchronous.framesInFlight()==2,"frame ring did not wait the oldest completion");asynchronous.endFrame();asynchronous.wait(tokens[2]);check(retired==3,"completion did not retire queued resources in order");
        rejects<std::invalid_argument>([&]{asynchronous.isComplete({tokens[0].device+1,tokens[0].serial});});
        rejects<std::invalid_argument>([&]{asynchronous.isComplete({tokens[0].device,999});});
        asynchronous.retireResources([&]{++retired;});asynchronous.close();check(retired==4,"shutdown lost pending resource retirement");
        other.close();
        std::cout << "RHI buffer ownership, ranges, usage, transfers and shutdown passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
