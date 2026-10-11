#include "PT/ViewportPostprocess.h"
#include <json/json.hpp>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>
namespace pt {
struct ViewportPostprocessWorker::Impl {
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::optional<ViewportPostprocessJob> job;
    std::optional<ViewportPostprocessResult> result;
    bool occupied=false,stopping=false;
    std::thread thread;
    Impl():thread([this]{run();}){}
    ~Impl(){ {std::lock_guard<std::mutex> lock(mutex);stopping=true;}changed.notify_one();thread.join();}
    void run() {
        DenoiserContext context;
        for(;;){
            ViewportPostprocessJob work;
            {std::unique_lock<std::mutex> lock(mutex);changed.wait(lock,[&]{return stopping||job.has_value();});if(!job)return;work=std::move(*job);job.reset();}
            ViewportPostprocessResult done;done.epoch=work.epoch;done.identity=work.identity;
            try {
                if(work.cancelled&&work.cancelled())done.discarded=true;
                else {
                    if(work.filter)context.execute(work.image,work.options);
                    if(work.cancelled&&work.cancelled())done.discarded=true;
                    else if(!work.outputPrefix.empty()){
                        const auto start=std::chrono::steady_clock::now();
                        auto report=work.metadata.empty()?nlohmann::json::object():nlohmann::json::parse(work.metadata);
                        writeImage(work.image,work.exposure,work.outputPrefix);
                        const auto &image=work.image;
                        report["epoch"]=work.epoch;report["width"]=image.width;report["height"]=image.height;report["samples"]=image.samples;
                        report["total_samples"]=image.totalSamples;report["rays"]=image.rays;report["non_finite_samples"]=image.nonFiniteSamples;report["execution"]=image.execution;
                        report["denoiser"]=image.denoiser;report["denoise_device"]=image.denoiseDevice;report["denoise_seconds"]=image.denoiseSeconds;report["denoise_auxiliary"]=image.denoiseAuxiliary;
                        report["film_readbacks"]=image.readbacks;report["film_readback_bytes"]=image.readbackBytes;report["readback_including_wait_seconds"]=image.readbackSeconds;
                        report["exposure"]=work.exposure;report["linear_output"]="raw PFM; filtered PFM is a separate derived image";
                        std::ofstream file(work.outputPrefix+".json");file<<report.dump(2)<<'\n';file.close();if(!file)throw std::runtime_error("PT viewport: cannot write export report");
                        done.exportSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
                    }
                }
                if(work.exported){if(done.discarded)throw std::runtime_error("PT viewport export cancelled");work.exported->set_value(work.outputPrefix);}
                if(!done.discarded)done.image=std::move(work.image);
            }catch(const std::exception &error){done.error=error.what();if(work.exported)work.exported->set_exception(std::current_exception());}
            done.context=context.statistics();
            {std::lock_guard<std::mutex> lock(mutex);result=std::move(done);}
        }
    }
};
ViewportPostprocessWorker::ViewportPostprocessWorker():impl_(std::make_unique<Impl>()){}
ViewportPostprocessWorker::~ViewportPostprocessWorker()=default;
bool ViewportPostprocessWorker::busy()const{std::lock_guard<std::mutex> lock(impl_->mutex);return impl_->occupied;}
bool ViewportPostprocessWorker::submit(ViewportPostprocessJob &&job){std::lock_guard<std::mutex> lock(impl_->mutex);if(impl_->occupied||impl_->stopping)return false;impl_->job=std::move(job);impl_->occupied=true;impl_->changed.notify_one();return true;}
std::optional<ViewportPostprocessResult> ViewportPostprocessWorker::poll(){std::lock_guard<std::mutex> lock(impl_->mutex);if(!impl_->result)return {};auto result=std::move(impl_->result);impl_->result.reset();impl_->occupied=false;return result;}
} // namespace pt
