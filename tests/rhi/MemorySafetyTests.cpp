#include "rhi/MemorySafety.h"
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <limits>
#include <thread>

using namespace rhi;
constexpr uint64_t MiB=1024*1024,GiB=1024*MiB;
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class E,class F>void rejects(F function){bool rejected=false;try{function();}catch(const E&){rejected=true;}check(rejected,"Missing memory admission rejection");}
int main(){try{
    ProcessMemorySample process{16*GiB,100*MiB,true,0};
    MemorySafety safety([&]{return process;});
    check(safety.stats().processBudgetBytes==3*GiB&&safety.stats().gpuBudgetBytes==1536*MiB,"Unsafe automatic limits");
    safety.check(2*GiB,"small enough candidate");
    rejects<ResourceBudgetExceeded>([&]{safety.check(3*GiB,"oversized candidate");});
    {
        auto first=safety.reserve(1800*MiB,"worker one");
        rejects<ResourceBudgetExceeded>([&]{auto second=safety.reserve(1300*MiB,"worker two");});
        auto moved=std::move(first);check(safety.stats().reservedBytes==1800*MiB,"Move lost reservation");
        process.footprintBytes=1500*MiB;
        rejects<ResourceBudgetExceeded>([&]{safety.check(0,"observed process growth");});
        process.footprintBytes=100*MiB;
    }
    check(safety.stats().reservedBytes==0,"Scratch quota leaked after scope");
    process.pressure=1;
    rejects<ResourceBudgetExceeded>([&]{safety.check(0,"pressure stops next render");});
    rejects<ResourceBudgetExceeded>([&]{safety.check(128*MiB,"pressure stops large upload");});
    safety.check(16*1024,"small GUI bookkeeping");
    process.pressure=2;
    rejects<ResourceBudgetExceeded>([&]{safety.check(64*1024,"critical pressure");});
    process.pressure=0;
    safety.check(1*MiB,"recovered system");
    rejects<ResourceBudgetExceeded>([&]{safety.checkGpu(1530*MiB,7*MiB,"native staging exceeds GPU quota");});
    rejects<std::invalid_argument>([&]{safety.configure(9*GiB);});
    rejects<std::invalid_argument>([&]{safety.configure(GiB,2*GiB);});
    rejects<ResourceBudgetExceeded>([&]{MemorySafety::bytes(std::numeric_limits<size_t>::max(),2);});
    // Competing jobs reserve their peak scratch before allocating any memory.
    std::mutex mutex;std::condition_variable ready;unsigned attempted=0;
    std::atomic<unsigned> accepted{0},denied{0};
    auto worker=[&]{MemorySafety::Reservation lease;
        try{lease=safety.reserve(1600*MiB,"concurrent worker");++accepted;}catch(const ResourceBudgetExceeded&){++denied;}
        std::unique_lock<std::mutex> lock(mutex);++attempted;ready.notify_all();ready.wait(lock,[&]{return attempted==2;});
    };
    std::thread one(worker),two(worker);one.join();two.join();
    check(accepted==1&&denied==1&&safety.stats().reservedBytes==0,"Concurrent admission exceeded total budget");
    safety.configure(512*MiB,256*MiB);
    check(safety.stats().processBudgetBytes==512*MiB,"Explicit lower budget ignored");
    safety.configure();check(safety.stats().processBudgetBytes==3*GiB,"Zero did not restore automatic budget");
    std::cout<<"Memory safety policy, concurrent reservations, pressure recovery and native GPU headroom passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
