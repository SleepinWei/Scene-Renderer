#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>
#include "PT/ViewportPostprocess.h"
#include <json/json.hpp>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
namespace {
void check(bool b,const char *s){if(!b)throw std::runtime_error(s);}
pt::ViewportPostprocessResult wait(pt::ViewportPostprocessWorker &worker) {
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(std::chrono::steady_clock::now()<end){if(auto r=worker.poll())return std::move(*r);std::this_thread::sleep_for(std::chrono::milliseconds(1));}throw std::runtime_error("Viewport postprocess worker stalled");
}
pt::Image image(){pt::Image i;i.width=32;i.height=24;i.samples=4;i.radiance.assign(32*24,glm::vec3(.5f));i.albedo.assign(32*24,glm::vec3(.3f));i.normal.assign(32*24,glm::vec3(0,0,1));i.sampleCounts.assign(32*24,4);return i;}
}
int main(){try {
    pt::ViewportPostprocessWorker worker;auto original=image();auto tag=std::make_shared<int>(1);
    pt::ViewportPostprocessJob job;job.epoch=1;job.identity=tag;job.image=original;job.filter=pt::denoiserAvailable();
    check(worker.submit(std::move(job)),"Worker rejected first job");pt::ViewportPostprocessJob pending;pending.image=original;check(!worker.submit(std::move(pending))&&pending.image.radiance==original.radiance,"Worker queue was unbounded / rejected job moved source");
    auto first=wait(worker);check(first.error.empty()&&first.epoch==1&&first.identity==tag&&first.image.radiance==original.radiance,"Worker lost identity / raw film");
    job={};job.epoch=2;job.image=original;job.filter=pt::denoiserAvailable();check(worker.submit(std::move(job)),"Worker remained occupied after poll");auto hot=wait(worker);
    if(pt::denoiserAvailable())check(hot.context.deviceBuilds==1&&hot.context.filterBuilds==3&&hot.context.executions==2,"Worker did not retain one OIDN context");
    auto cancel=std::make_shared<std::atomic<bool>>(true);job={};job.epoch=3;job.image=original;job.cancelled=[cancel]{return cancel->load();};check(worker.submit(std::move(job)),"Worker rejected cancelled job");auto cancelled=wait(worker);check(cancelled.discarded&&cancelled.image.radiance.empty()&&cancelled.context.executions==hot.context.executions,"Cancelled job executed OIDN / retained film");
    auto calls=std::make_shared<int>(0);job={};job.epoch=3;job.image=original;job.filter=pt::denoiserAvailable();job.cancelled=[calls]{return ++*calls>1;};worker.submit(std::move(job));auto late=wait(worker);check(late.discarded&&late.image.denoised.empty()&&*calls==2,"Cancellation after inference published an old image");
    auto prefix=(std::filesystem::temp_directory_path()/"pt-viewport-worker-test").string();auto exported=std::make_shared<std::promise<std::string>>();auto ready=exported->get_future();
    job={};job.epoch=4;job.image=original;job.filter=pt::denoiserAvailable();job.outputPrefix=prefix;job.metadata="{\"seed\":7}";job.exported=exported;check(worker.submit(std::move(job)),"Export rejected");auto output=wait(worker);check(output.error.empty()&&ready.get()==prefix&&pt::readPfm(prefix+".pfm").radiance==original.radiance,"Worker export changed raw linear film");
    if(pt::denoiserAvailable())check(pt::readPfm(prefix+"-denoised.pfm").radiance==output.image.denoised,"Worker export lost derived linear film");
    std::ifstream file(prefix+".json");nlohmann::json report;file>>report;check(report["epoch"]==4&&report["seed"]==7&&report["samples"]==4,"Export report lost frozen metadata");
    exported=std::make_shared<std::promise<std::string>>();ready=exported->get_future();job={};job.image=original;job.filter=false;job.outputPrefix=prefix;job.metadata="invalid json";job.exported=exported;worker.submit(std::move(job));auto failure=wait(worker);check(!failure.error.empty(),"Export error disappeared");bool rejected=false;try{ready.get();}catch(const std::exception &){rejected=true;}check(rejected,"Export failure did not reach promise");
    job={};job.filter=false;job.outputPrefix=prefix;job.image=original;job.image.normal.pop_back();worker.submit(std::move(job));auto invalid=wait(worker);check(!invalid.error.empty(),"Invalid raw-only export AOV accepted");
    // Accepted exports must settle even if the viewport is closed immediately.
    exported=std::make_shared<std::promise<std::string>>();ready=exported->get_future();{pt::ViewportPostprocessWorker closing;job={};job.image=original;job.filter=false;job.outputPrefix=prefix;job.exported=exported;closing.submit(std::move(job));}check(ready.get()==prefix,"Shutdown abandoned accepted export");
    for(auto suffix:{".png",".pfm",".json","-albedo.png","-albedo.pfm","-normal.png","-normal.pfm","-samples.png","-denoised.png","-denoised.pfm"})std::filesystem::remove(prefix+suffix);
    std::cout<<"Viewport worker: bounded jobs, persistent filters, cancellation, immutable raw export, errors and shutdown passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
