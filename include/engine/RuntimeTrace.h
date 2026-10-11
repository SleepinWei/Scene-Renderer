#pragma once
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
namespace engine {
// Opt-in bounded timeline. CPU and native GPU timestamps have separate origins;
// completion acknowledgement is a CPU event, not a display/scanout timestamp.
class RuntimeTrace {
public:
    using Clock=std::chrono::steady_clock;
    static RuntimeTrace& instance(){static RuntimeTrace trace;return trace;}
    bool enabled()const{return !path_.empty();}
    uint64_t nextId(){return enabled()?++next_:0;}
    void cpu(const char* name,const char* lane,Clock::time_point start,Clock::time_point end,uint64_t id=0,double a=0,double b=0){
        try{if(enabled())record({name,lane,us(start-origin_),us(end-start),id,a,b,false});}catch(...){}
    }
    void gpu(const std::string& name,double startMs,double endMs){
        if(!enabled())return;std::lock_guard<std::mutex> lock(mutex_);
        if(!gpuOrigin_)gpuOrigin_=startMs;
        append({name,"gpu",(startMs-gpuOrigin_)*1000,(endMs-startMs)*1000,0,0,0,true});
    }
    std::function<void()> task(std::function<void()> fn){
        if(!enabled())return fn;
        const auto queued=Clock::now();const auto id=nextId();
        return [this,queued,id,fn=std::move(fn)](){
            auto start=Clock::now();cpu("task/queue-wait","workers",queued,start,id);
            try{fn();}catch(...){cpu("task/execute","workers",start,Clock::now(),id);throw;}
            cpu("task/execute","workers",start,Clock::now(),id);
        };
    }
    void write(){
        if(!enabled())return;std::lock_guard<std::mutex> lock(mutex_);std::ofstream out(path_);
        out<<"{\"time_unit\":\"microseconds\",\"gpu_clock\":\"independent native timestamp origin; do not align to CPU without calibration\",\"dropped_events\":"<<dropped_<<",\"traceEvents\":[";
        bool first=true;for(const auto& e:events_){if(!first)out<<',';first=false;
            out<<"{\"name\":"<<quote(e.name)<<",\"lane\":"<<quote(e.lane)<<",\"ts\":"<<e.start<<",\"dur\":"<<e.duration<<",\"id\":"<<e.id<<",\"a\":"<<e.a<<",\"b\":"<<e.b<<",\"ph\":\"X\",\"pid\":"<<(e.gpu?2:1)<<",\"tid\":"<<e.tid<<",\"gpu\":"<<(e.gpu?"true":"false")<<'}';}
        out<<"]}\n";
    }
private:
    struct Event {std::string name,lane;double start,duration;uint64_t id;double a,b;bool gpu;uint64_t tid=0;};
    RuntimeTrace():path_(std::getenv("SCENERENDERER_RUNTIME_TRACE_OUTPUT")?std::getenv("SCENERENDERER_RUNTIME_TRACE_OUTPUT"):""),origin_(Clock::now()){}
    static double us(Clock::duration value){return std::chrono::duration<double,std::micro>(value).count();}
    static std::string quote(const std::string& s){std::string result="\"";for(unsigned char c:s){if(c=='"'||c=='\\')result+='\\';if(c>=32)result+=char(c);}return result+'"';}
    void record(Event e){e.tid=std::hash<std::thread::id>{}(std::this_thread::get_id());try{std::lock_guard<std::mutex> lock(mutex_);append(std::move(e));}catch(...){}}
    void append(Event e){if(events_.size()==32768){events_.pop_front();++dropped_;}events_.push_back(std::move(e));}
    std::string path_;Clock::time_point origin_;std::mutex mutex_;std::deque<Event> events_;std::atomic<uint64_t> next_{0};uint64_t dropped_=0;double gpuOrigin_=0;
};
class TraceScope {
public:
    TraceScope(const char* name,const char* lane,uint64_t id=0):name_(name),lane_(lane),id_(id),trace_(RuntimeTrace::instance()),active_(trace_.enabled()) {if(active_)start_=RuntimeTrace::Clock::now();}
    ~TraceScope(){if(active_)trace_.cpu(name_,lane_,start_,RuntimeTrace::Clock::now(),id_);}
private:
    const char* name_;const char* lane_;uint64_t id_;RuntimeTrace& trace_;bool active_;RuntimeTrace::Clock::time_point start_;
};
}
