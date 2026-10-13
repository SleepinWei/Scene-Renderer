#pragma once
#include "rhi/Device.h"
#include <functional>
#include <mutex>

namespace rhi {
struct ProcessMemorySample {
    uint64_t physicalBytes=0,footprintBytes=0;
    bool footprintSupported=false;
    uint32_t pressure=0; // 0 normal, 1 warning, 2 critical.
};
struct MemorySafetyStats {
    uint64_t processBudgetBytes=0,gpuBudgetBytes=0,reservedBytes=0,peakReservedBytes=0,rejections=0;
    ProcessMemorySample process;
};
// Cooperative admission, not an OS RSS limit. The process sample includes
// compressed memory on macOS; GPU payload quota is an independent upper bound.
class MemorySafety {
public:
    using Sampler=std::function<ProcessMemorySample()>;
    explicit MemorySafety(Sampler={});
    class Reservation {
    public:
        Reservation()=default;
        Reservation(const Reservation&)=delete;
        Reservation& operator=(const Reservation&)=delete;
        Reservation(Reservation&&) noexcept;
        Reservation& operator=(Reservation&&) noexcept;
        ~Reservation();
    private:
        friend class MemorySafety;
        Reservation(MemorySafety* owner,uint64_t bytes):owner_(owner),bytes_(bytes){}
        MemorySafety* owner_=nullptr;uint64_t bytes_=0;
    };
    void configure(uint64_t processBytes=0,uint64_t gpuBytes=0); // 0 = conservative automatic limit.
    Reservation reserve(uint64_t bytes,const std::string& label);
    void check(uint64_t additionalBytes,const std::string& label);
    void checkGpu(uint64_t nativeBytes,uint64_t additionalBytes,const std::string& label);
    MemorySafetyStats stats() const;
    static uint64_t bytes(size_t count,size_t stride);
private:
    void admit(uint64_t,const std::string&,const ProcessMemorySample&);
    void release(uint64_t) noexcept;
    Sampler sample_;mutable std::mutex mutex_;MemorySafetyStats state_;
};
MemorySafety& memorySafety();
ProcessMemorySample sampleProcessMemory();
}
