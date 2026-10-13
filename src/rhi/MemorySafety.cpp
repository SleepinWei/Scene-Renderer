#include "rhi/MemorySafety.h"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <limits>
#ifdef __APPLE__
#include <dispatch/dispatch.h>
#include <mach/mach.h>
#include <sys/sysctl.h>
#elif defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#endif

namespace rhi {
namespace {
constexpr uint64_t MiB=1024*1024,GiB=1024*MiB;
std::atomic<uint32_t> pressure{0};
#ifdef __APPLE__
void startPressureMonitor(){
    // Process-lifetime source; its context stays valid until process exit.
    static const dispatch_source_t source=[](){
        auto source=dispatch_source_create(DISPATCH_SOURCE_TYPE_MEMORYPRESSURE,0,
            DISPATCH_MEMORYPRESSURE_NORMAL|DISPATCH_MEMORYPRESSURE_WARN|DISPATCH_MEMORYPRESSURE_CRITICAL,
            dispatch_get_global_queue(QOS_CLASS_UTILITY,0));
        if(source){dispatch_set_context(source,source);dispatch_source_set_event_handler_f(source,[](void* context){
            auto flags=dispatch_source_get_data(static_cast<dispatch_source_t>(context));
            pressure.store((flags&DISPATCH_MEMORYPRESSURE_CRITICAL)?2:(flags&DISPATCH_MEMORYPRESSURE_WARN)?1:0);
        });dispatch_resume(source);}return source;
    }();(void)source;
}
#endif
}
ProcessMemorySample sampleProcessMemory(){
    ProcessMemorySample sample;
#ifdef __APPLE__
    size_t size=sizeof(sample.physicalBytes);sysctlbyname("hw.memsize",&sample.physicalBytes,&size,nullptr,0);
    task_vm_info_data_t info{};mach_msg_type_number_t count=TASK_VM_INFO_COUNT;
    if(task_info(mach_task_self(),TASK_VM_INFO,reinterpret_cast<task_info_t>(&info),&count)==KERN_SUCCESS&&count>=TASK_VM_INFO_REV1_COUNT){sample.footprintSupported=true;sample.footprintBytes=info.phys_footprint;}
    startPressureMonitor();sample.pressure=pressure.load();
    // Also query the current dispatch-level flags. A newly created listener
    // must not assume normal pressure until the next state transition.
    uint32_t flags=0;size=sizeof(flags);
    if(sysctlbyname("kern.memorystatus_vm_pressure_level",&flags,&size,nullptr,0)==0)
        sample.pressure=(flags&DISPATCH_MEMORYPRESSURE_CRITICAL)?2:(flags&DISPATCH_MEMORYPRESSURE_WARN)?1:0;
#elif defined(_WIN32)
    MEMORYSTATUSEX system{};system.dwLength=sizeof(system);
    if(GlobalMemoryStatusEx(&system)){sample.physicalBytes=system.ullTotalPhys;sample.pressure=system.dwMemoryLoad>=95?2:system.dwMemoryLoad>=85?1:0;}
    PROCESS_MEMORY_COUNTERS_EX process{};
    if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&process),sizeof(process))){sample.footprintSupported=true;sample.footprintBytes=std::max(uint64_t(process.WorkingSetSize),uint64_t(process.PrivateUsage));}
#else
    const long pages=sysconf(_SC_PHYS_PAGES),page=sysconf(_SC_PAGESIZE);
    if(pages>0&&page>0)sample.physicalBytes=uint64_t(pages)*uint64_t(page);
    std::ifstream status("/proc/self/status");std::string line;uint64_t resident=0,swap=0;
    while(std::getline(status,line)){
        if(line.rfind("VmRSS:",0)==0)resident=std::stoull(line.substr(6))*1024;
        if(line.rfind("VmSwap:",0)==0)swap=std::stoull(line.substr(7))*1024;
    }
    sample.footprintSupported=resident!=0;sample.footprintBytes=resident+swap;
    std::ifstream system("/proc/meminfo");uint64_t available=0;
    while(std::getline(system,line))if(line.rfind("MemAvailable:",0)==0)available=std::stoull(line.substr(13))*1024;
    if(available&&sample.physicalBytes)sample.pressure=available<sample.physicalBytes/20?2:available<sample.physicalBytes/10?1:0;
#endif
    return sample;
}
MemorySafety::MemorySafety(Sampler sampler):sample_(sampler?std::move(sampler):sampleProcessMemory){configure();}
void MemorySafety::configure(uint64_t process,uint64_t gpu){
    std::lock_guard<std::mutex> lock(mutex_);auto sample=sample_();
    if(!process)process=sample.physicalBytes?std::min(3*GiB,sample.physicalBytes/5):GiB;
    if(!process||process<64*MiB)throw std::invalid_argument("Memory budget must be at least 64 MiB");
    if(sample.physicalBytes&&(process>sample.physicalBytes/2||(sample.physicalBytes>=4*GiB&&sample.physicalBytes-process<2*GiB)))throw std::invalid_argument("Memory budget must leave at least half of physical memory and, on 4 GiB+ systems, 2 GiB for the system");
    if(!gpu)gpu=process/2;
    if(gpu>process)throw std::invalid_argument("GPU resource budget cannot exceed process memory budget");
    if(state_.reservedBytes>process)throw std::invalid_argument("Memory budget below outstanding transient reservations");
    state_.processBudgetBytes=process;state_.gpuBudgetBytes=gpu;state_.process=sample;
}
void MemorySafety::admit(uint64_t additional,const std::string& label,const ProcessMemorySample& sample){
    state_.process=sample;
    const auto limit=state_.processBudgetBytes,headroom=std::min(128*MiB,limit/16);
    const auto footprint=sample.footprintSupported?sample.footprintBytes:0;
    auto remaining=limit-headroom;
    const bool blocked=sample.pressure&&(additional==0||additional>=64*1024);
    bool fits=footprint<=remaining;
    remaining=fits?remaining-footprint:0;
    fits=fits&&state_.reservedBytes<=remaining;
    remaining=fits?remaining-state_.reservedBytes:0;
    if(blocked||!fits||additional>remaining){
        ++state_.rejections;
        throw ResourceBudgetExceeded("Memory safety admission refused '"+label+"': requested "+std::to_string(additional)+
            " bytes, footprint "+std::to_string(footprint/MiB)+" MiB, pending scratch "+std::to_string(state_.reservedBytes/MiB)+
            " MiB, process limit "+std::to_string(limit/MiB)+" MiB, system pressure "+std::to_string(sample.pressure));
    }
}
void MemorySafety::check(uint64_t bytes,const std::string& label){std::lock_guard<std::mutex> lock(mutex_);admit(bytes,label,sample_());}
void MemorySafety::checkGpu(uint64_t native,uint64_t bytes,const std::string& label){
    std::lock_guard<std::mutex> lock(mutex_);admit(bytes,label,sample_());
    if(native>state_.gpuBudgetBytes||bytes>state_.gpuBudgetBytes-native){++state_.rejections;throw ResourceBudgetExceeded("Native GPU memory safety budget exceeded for '"+label+"' (including staging and acceleration structures)");}
}
MemorySafety::Reservation MemorySafety::reserve(uint64_t bytes,const std::string& label){
    std::lock_guard<std::mutex> lock(mutex_);admit(bytes,label,sample_());state_.reservedBytes+=bytes;
    state_.peakReservedBytes=std::max(state_.peakReservedBytes,state_.reservedBytes);return Reservation(this,bytes);
}
MemorySafetyStats MemorySafety::stats()const{std::lock_guard<std::mutex> lock(mutex_);auto result=state_;result.process=sample_();return result;}
uint64_t MemorySafety::bytes(size_t count,size_t stride){if(stride&&count>std::numeric_limits<uint64_t>::max()/stride)throw ResourceBudgetExceeded("Memory safety allocation size overflow");return uint64_t(count)*stride;}
void MemorySafety::release(uint64_t bytes)noexcept{std::lock_guard<std::mutex> lock(mutex_);state_.reservedBytes-=bytes;}
MemorySafety::Reservation::Reservation(Reservation&& other)noexcept:owner_(other.owner_),bytes_(other.bytes_){other.owner_=nullptr;}
MemorySafety::Reservation& MemorySafety::Reservation::operator=(Reservation&& other)noexcept{if(this!=&other){if(owner_)owner_->release(bytes_);owner_=other.owner_;bytes_=other.bytes_;other.owner_=nullptr;}return *this;}
MemorySafety::Reservation::~Reservation(){if(owner_)owner_->release(bytes_);}
MemorySafety& memorySafety(){static MemorySafety safety;return safety;}
}
