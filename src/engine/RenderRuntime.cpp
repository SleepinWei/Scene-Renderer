#include "engine/RenderRuntime.h"
#include "engine/RuntimeTrace.h"
#include "PT/SceneIdentity.h"
#include "engine/QualityPolicy.h"
#include "renderer/rhi/SceneAdapter.h"
#include "rhi/ShaderAssets.h"
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <array>
#include <cmath>
#include <iostream>
#include <json/json.hpp>
namespace engine {
namespace {
// Publication owns pixels independently of mutable terrain/ocean/temporal state.
// A rejected candidate may have submitted compute; never redraw its old packets.
struct PublishedImage {
    render::Resources resources;
    rhi::TextureHandle texture;
    uint32_t width, height;
    PublishedImage(std::shared_ptr<rhi::GraphicsDevice> device, uint32_t w, uint32_t h)
        : resources(std::move(device)), width(w), height(h) {
        texture = resources.texture({w, h, rhi::Format::RGBA8UNorm,
                                     rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDestination |
                                         rhi::TextureUsage::CopySource | rhi::TextureUsage::ColorAttachment,
                                     "Last successful scene publication"});
    }
};
}
RenderRuntime::RenderRuntime(std::shared_ptr<rhi::GraphicsDevice> device,
                             std::unique_ptr<render::GuiRenderer> gui, size_t capacity,bool prewarmPathTracing)
    : device_(std::move(device)), prewarmPathTracing_(prewarmPathTracing),queue_(capacity) {
    if (!device_ || device_->backend() == rhi::Backend::OpenGL)
        throw std::invalid_argument("Threaded renderer requires a native Metal or Vulkan device");
    device_->waitIdle(); // Quiescent ownership transfer after window/font bootstrap.
    if(prewarmPathTracing_)pathTracingStatus_.state="Warming";
    thread_ = std::thread([this, gui = std::move(gui)]() mutable { run(std::move(gui)); });
}
RenderRuntime::~RenderRuntime() {
    try {
        finish();
    } catch (...) {
    }
}
void RenderRuntime::run(std::unique_ptr<render::GuiRenderer> gui) {
#ifdef __APPLE__
    @autoreleasepool {
#endif
        device_->adoptCurrentThread();
        std::array<double, 256> timings{};
        size_t samples = 0;
        auto publishTimings = [&] {
            const auto count = std::min(samples, timings.size());
            if (!count)
                return;
            auto sorted = timings;
            std::sort(sorted.begin(), sorted.begin() + count);
            renderP95Milliseconds_ = sorted[size_t(std::ceil(count * .95)) - 1];
            renderP99Milliseconds_ = sorted[size_t(std::ceil(count * .99)) - 1];
        };
        try {
            std::unique_ptr<pt::GpuPipelineWarmup> ptWarmup;
            pt::GpuPipelineWarmupStats ptWarmStats;std::string ptWarmError;
            try{if(prewarmPathTracing_){ptWarmup=std::make_unique<pt::GpuPipelineWarmup>(device_);ptWarmStats=ptWarmup->statistics();}}
            catch(const std::exception &error){ptWarmError=error.what();}
            {std::lock_guard<std::mutex> lock(pathTracingMutex_);pathTracingStatus_.state="Raster";pathTracingStatus_.startupTraceSeconds=ptWarmStats.traceSeconds;pathTracingStatus_.startupDisplaySeconds=ptWarmStats.displaySeconds;pathTracingStatus_.startupWarmupError=ptWarmError;}
            auto adapter=std::make_unique<render::SceneAdapter>(device_);
            QualityPolicy quality;
            std::shared_ptr<const render::TerrainPayload> originalTerrain,qualityTerrain;
            uint32_t terrainQuality=0;
            auto adaptQuality=[&](const render::RenderWorldSnapshot& original) {
                auto result=original;
                if(!quality.level())return result;
                result.frame.historyKey^=uint64_t(quality.level())*0x517cc1b727220a95ull;
                for(auto& ocean:result.frame.oceans){ocean.spectrum.size=quality.fft(ocean.spectrum.size);ocean.meshSize=quality.oceanMesh(ocean.meshSize);}
                if(result.terrain) {
                    if(originalTerrain!=result.terrain->source || terrainQuality!=quality.level()) {
                        originalTerrain=result.terrain->source;terrainQuality=quality.level();
                        auto reduced=std::make_shared<render::TerrainPayload>(*originalTerrain);
                        reduced->capacity=quality.terrainLeaves(reduced->capacity);reduced->virtualColumns=quality.virtualColumns(reduced->virtualColumns);
                        qualityTerrain=std::move(reduced);
                    }
                    result.terrain->source=qualityTerrain;
                }return result;
            };
            struct Candidate {
                std::shared_ptr<ScenePreparation> request;
                std::shared_ptr<const render::RenderWorldSnapshot> world;
                std::unique_ptr<render::SceneAdapter> adapter;
                std::unique_ptr<render::ForwardPbrRenderer> renderer;
                bool ready=false,settled=false;
                void reject(std::exception_ptr error) {if(!settled){request->completion->set_exception(error);settled=true;}}
                ~Candidate(){if(!settled)reject(std::make_exception_ptr(std::runtime_error("Scene preparation cancelled or renderer stopped")));}
            };
            std::unique_ptr<Candidate> candidate;
            std::unique_ptr<render::ForwardPbrRenderer> renderer;
            std::unique_ptr<PublishedImage> published;
            std::unique_ptr<PublishedImage> ptPreview,ptPreviewBack;
            std::shared_ptr<PathTracingViewportRequest> ptRequest,ptBuilding;
            struct PreparedPt {std::shared_ptr<pt::CpuScene> scene;std::shared_ptr<const pt::GpuSceneUpload> upload;double cpuSeconds=0;};
            auto ptGeometryCache=std::make_shared<pt::GeometryCache>();
            auto ptLodCache=std::make_shared<pt::WorldLodCache>();
            auto ptGpuCache=std::make_shared<pt::GpuSceneCache>(device_);
            std::shared_ptr<pt::CpuScene> ptScene;
            std::shared_ptr<PathTracingViewportRequest> ptResident;
            pt::SceneIdentity ptResidentIdentity,ptCaptureIdentity;
            std::vector<render::SnapshotDraw> ptProceduralDraws;
            bool ptHaveCapture=false;
            std::future<PreparedPt> ptPrepared;
            std::unique_ptr<pt::GpuRenderSession> ptSession;
            pt::ViewportPostprocessWorker ptPostprocess;
            pt::DenoiserStats ptDenoiserStats;
            uint32_t ptLastRead=0;uint64_t ptNextRead=16,ptDiscarded=0;
            double ptDenoiseSeconds=0;
            std::string ptDenoiseMessage;
            struct PendingPtExport {
                std::shared_ptr<PathTracingViewportExport> ticket;
                std::shared_ptr<PathTracingViewportRequest> owner;
                float exposure=1;
                bool submitted=false;
                ~PendingPtExport(){if(ticket&&!submitted)try{ticket->completion->set_exception(std::make_exception_ptr(std::runtime_error("PT viewport export cancelled before snapshot capture")));}catch(...) {}}
            };
            std::unique_ptr<PendingPtExport> ptExport;
            bool ptFailed=false,ptCameraReuse=false;
            PathTracingViewportStatus ptTiming;uint64_t ptFramePreparationWork=0;
            auto requestAge=[&]{return ptRequest?std::max(0.0,std::chrono::duration<double>(std::chrono::steady_clock::now()-ptRequest->createdAt).count()):0.0;};
            auto ptStatus=[&](const char *state,const std::string &message=std::string{}) {
                std::lock_guard<std::mutex> lock(pathTracingMutex_);
                pathTracingStatus_={ptRequest?ptRequest->epoch:0,ptSession&&ptRequest?ptSession->samples():0,ptRequest?ptRequest->options.samples:0,state,message};
                pathTracingStatus_.requestAcceptedSeconds=ptTiming.requestAcceptedSeconds;pathTracingStatus_.captureSeconds=ptTiming.captureSeconds;pathTracingStatus_.cpuSceneSeconds=ptTiming.cpuSceneSeconds;pathTracingStatus_.cpuExportSeconds=ptTiming.cpuExportSeconds;
                pathTracingStatus_.previewFrames=ptTiming.previewFrames;
                pathTracingStatus_.readyLatencySeconds=ptTiming.readyLatencySeconds;pathTracingStatus_.firstSampleLatencySeconds=ptTiming.firstSampleLatencySeconds;pathTracingStatus_.maxPreparationFrameSeconds=ptTiming.maxPreparationFrameSeconds;pathTracingStatus_.maxPreparationWorkBytes=ptTiming.maxPreparationWorkBytes;
                pathTracingStatus_.startupTraceSeconds=ptWarmStats.traceSeconds;pathTracingStatus_.startupDisplaySeconds=ptWarmStats.displaySeconds;pathTracingStatus_.startupWarmupError=ptWarmError;
                if(ptSession){const auto &p=ptSession->preparationStatistics();pathTracingStatus_.preparationBytes=p.completedBytes;pathTracingStatus_.preparationTotalBytes=p.totalBytes;pathTracingStatus_.preparationSteps=p.steps;pathTracingStatus_.lastPreparationWorkBytes=ptFramePreparationWork;pathTracingStatus_.cloneBytes=p.clonedBytes;pathTracingStatus_.patchBytes=p.patchBytes;pathTracingStatus_.patchBuffers=p.patchBuffers;pathTracingStatus_.pipelineSeconds=p.pipelineSeconds;pathTracingStatus_.allocationSeconds=p.allocationSeconds;pathTracingStatus_.uploadSeconds=p.uploadSeconds;pathTracingStatus_.cacheCompareSeconds=p.cacheCompareSeconds;
                    const auto &stats=ptSession->statistics();pathTracingStatus_.sceneUploads=stats.sceneBufferUploads;pathTracingStatus_.sceneCacheHits=stats.sceneBufferCacheHits;pathTracingStatus_.sceneUploadBytes=stats.sceneUploadBytes;}
                if(ptScene&&ptSession){for(auto media:ptScene->cameraData().media)for(int i=0;i<4;++i)if(media[i])++pathTracingStatus_.cameraMediaCount;const auto a=ptScene->accelerationStats();pathTracingStatus_.tlasBuilds=a.tlasBuilds;pathTracingStatus_.tlasRefits=a.tlasRefits;pathTracingStatus_.tlasReuses=a.tlasReuses;pathTracingStatus_.emitterReuses=a.emitterReuses;if(ptCameraReuse){pathTracingStatus_.tlasBuilds=pathTracingStatus_.tlasRefits=0;pathTracingStatus_.tlasReuses=pathTracingStatus_.emitterReuses=1;}}
                if(ptCameraReuse){pathTracingStatus_.cloneBytes=pathTracingStatus_.patchBytes=pathTracingStatus_.patchBuffers=0;pathTracingStatus_.preparationBytes=pathTracingStatus_.preparationTotalBytes=pathTracingStatus_.preparationSteps=pathTracingStatus_.lastPreparationWorkBytes=0;pathTracingStatus_.pipelineSeconds=pathTracingStatus_.allocationSeconds=pathTracingStatus_.uploadSeconds=pathTracingStatus_.cacheCompareSeconds=0;}
                pathTracingStatus_.denoiserAvailable=pt::denoiserAvailable();pathTracingStatus_.denoiserBusy=ptPostprocess.busy();pathTracingStatus_.denoiserContext=ptDenoiserStats;pathTracingStatus_.denoiseDiscarded=ptDiscarded;pathTracingStatus_.denoiseSeconds=ptDenoiseSeconds;pathTracingStatus_.denoiseMessage=ptDenoiseMessage;
                if(ptSession){const auto &stats=ptSession->statistics();pathTracingStatus_.denoisedSamples=ptSession->denoisedSamples();pathTracingStatus_.filmReadbacks=stats.readbacks;pathTracingStatus_.filmReadbackBytes=stats.readbackBytes;pathTracingStatus_.readbackSeconds=stats.readbackSeconds;pathTracingStatus_.denoisedUploadSeconds=stats.denoisedUploadSeconds;pathTracingStatus_.denoisedUploadBytes=stats.denoisedUploadBytes;}
                pathTracingStatus_.gpuCacheBytes=ptGpuCache->retainedBytes();pathTracingStatus_.blasCacheBytes=ptGeometryCache->retainedBytes();
            };
            auto ptFailure=[&](const std::exception &error){ptExport.reset();ptSession.reset();ptFailed=true;ptStatus("Error",error.what());};
            auto updatePt=[&](const RenderPacket &packet) {
                ptFramePreparationWork=0;
                auto request=packet.pathTracingViewport;
                // Cancelled queued packets cannot erase the retained scene before
                // their replacement arrives. Explicit null requests release it.
                if(request&&request->cancelled.load()){
                    if(request==ptRequest){if(ptSession&&!ptSession->ready()){ptSession->cancel();ptSession.reset();}ptRequest.reset();ptStatus("Raster");}
                    return;
                }
                if(!request){ptSession.reset();ptScene.reset();ptResident.reset();}
                if(request!=ptRequest) {
                    ptExport.reset();ptLastRead=0;ptNextRead=16;ptDenoiseSeconds=0;ptDenoiseMessage.clear();
                    ptRequest=request;ptFailed=false;ptCameraReuse=false;ptTiming={};ptTiming.requestAcceptedSeconds=requestAge();
                    bool reuse=false;
                    if(request&&ptSession&&ptSession->ready()&&ptScene&&ptResident&&!request->forceRecapture&&request->lighting==ptResident->lighting&&ptResidentIdentity==pt::sceneIdentity(*request->world)&&pt::sameRenderOptions(request->options,ptResident->options)) {
                        try{ptScene->updateCamera(request->world->frame);ptSession->updateCamera(*ptScene,float(request->options.width)/request->options.height);ptResident=request;reuse=true;ptCameraReuse=true;ptTiming.readyLatencySeconds=requestAge();}
                        catch(const std::exception &error){ptFailure(error);}
                    }
                    if(!reuse)ptSession.reset();
                    if(!ptFailed)ptStatus(reuse?"Rendering":request?"Preparing":"Raster");
                }
                // Only one CPU scene build runs at a time. Cancelled builds are
                // reaped without waiting in the frame loop or publishing old data.
                if(ptPrepared.valid()&&ptPrepared.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
                    try {
                        auto prepared=ptPrepared.get();
                        if(prepared.scene&&ptRequest==ptBuilding&&!ptRequest->cancelled.load()) {
                            ptTiming.cpuSceneSeconds=prepared.cpuSeconds;ptTiming.cpuExportSeconds=prepared.upload?prepared.upload->exportSeconds:0;
                            ptSession=std::make_unique<pt::GpuRenderSession>(*prepared.scene,ptRequest->options,device_,ptGpuCache,std::move(prepared.upload));
                            ptScene=std::move(prepared.scene);ptResident=ptRequest;ptResidentIdentity=pt::sceneIdentity(*ptRequest->world);
                        }
                    }catch(const std::exception &error){if(ptRequest==ptBuilding)ptFailure(error);}
                    ptBuilding.reset();
                }
                if(ptSession&&!ptSession->ready()&&ptRequest&&ptResident==ptRequest&&!ptRequest->cancelled.load()) {
                    try{
                        const auto start=std::chrono::steady_clock::now();
                        // Cheap prewarmed pipeline / identity hits / binding
                        // publication need not each wait for another Raster frame.
                        // Share one total byte/time budget across all API calls.
                        while(!ptSession->ready()){
                            const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
                            const auto bytes=ptRequest->preparationByteBudget-ptFramePreparationWork;
                            if(bytes<4||elapsed>=ptRequest->preparationSecondsBudget)break;
                            ptSession->prepareStep(bytes,ptRequest->preparationSecondsBudget-elapsed);
                            ptFramePreparationWork+=ptSession->preparationStatistics().lastStepWorkBytes;
                        }
                        ptTiming.maxPreparationWorkBytes=std::max(ptTiming.maxPreparationWorkBytes,ptFramePreparationWork);
                        ptTiming.maxPreparationFrameSeconds=std::max(ptTiming.maxPreparationFrameSeconds,std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count());
                        if(ptSession->ready())ptTiming.readyLatencySeconds=requestAge();ptStatus(ptSession->ready()?"Rendering":"Uploading");
                    }
                    catch(const std::exception &error){ptFailure(error);}
                }
                if(ptRequest&&!ptRequest->cancelled.load()&&!ptFailed&&!ptSession&&!ptPrepared.valid()) {
                    try {
                        if(ptRequest->lighting){const auto &l=*ptRequest->lighting;
                            for(auto v:{l.sunDirection,l.sunIrradiance})for(int c=0;c<3;++c)if(!std::isfinite(v[c]))throw std::invalid_argument("Invalid frozen PT lighting");
                            if(glm::any(glm::lessThan(l.sunIrradiance,glm::vec3(0)))||!std::isfinite(l.sunRadius)||l.sunRadius<0||l.sunRadius>.1f||std::abs(glm::length(l.sunDirection)-1)>1e-3f||(glm::any(glm::greaterThan(l.sunIrradiance,glm::vec3(0)))&&l.sunRadius<=0))throw std::invalid_argument("Invalid frozen PT sun");
                        }
                        pt::validateOptions(ptRequest->options);
                        if(ptRequest->preparationByteBudget<4||!std::isfinite(ptRequest->preparationSecondsBudget)||ptRequest->preparationSecondsBudget<=0)throw std::invalid_argument("Invalid GPU PT preparation budget");
                        const auto captureStart=std::chrono::steady_clock::now();
                        const auto identity=pt::sceneIdentity(*ptRequest->world);
                        render::RenderWorldSnapshot frozen;
                        if(ptHaveCapture&&!ptRequest->forceRecapture&&identity.procedural==ptCaptureIdentity.procedural){
                            frozen=*ptRequest->world;frozen.draws.insert(frozen.draws.end(),ptProceduralDraws.begin(),ptProceduralDraws.end());frozen.terrain.reset();frozen.frame.oceans.clear();
                        }else{
                            frozen=pt::captureProcedural(*ptRequest->world,device_);
                            ptProceduralDraws.assign(frozen.draws.begin()+ptRequest->world->draws.size(),frozen.draws.end());ptCaptureIdentity=identity;ptHaveCapture=true;
                        }
                        auto sky=ptRequest->lighting?render::BakedAtmosphere{}:render::bakeAtmosphere(device_,frozen.frame);
                        ptTiming.captureSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-captureStart).count();
                        auto request=ptRequest;ptBuilding=request;
                        ptPrepared=std::async(std::launch::async,[request,previous=ptScene,cache=ptGeometryCache,lodCache=ptLodCache,frozen=std::move(frozen),sky=std::move(sky)]() mutable {
                            if(request->cancelled.load())return PreparedPt{};
                            const auto start=std::chrono::steady_clock::now();
                            if(request->options.worldLod.enabled)frozen=pt::buildLowLodWorld(frozen,request->options.worldLod,nullptr,lodCache);
                            auto scene=std::make_shared<pt::CpuScene>(frozen,std::vector<pt::DielectricMaterial>{},cache,previous.get());
                            const auto cpuSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
                            if(request->lighting){scene->environment=request->lighting->environment;scene->sunDirection=request->lighting->sunDirection;scene->sunIrradiance=request->lighting->sunIrradiance;scene->sunRadius=request->lighting->sunRadius;}
                            else if(!sky.radiance.empty()) {
                                scene->environment=std::make_shared<pt::Environment>(sky.width,sky.height,std::move(sky.radiance));
                                scene->sunDirection=sky.sunDirection;scene->sunIrradiance=sky.sunIrradiance;scene->sunRadius=sky.sunRadius;scene->cloudShadow=sky.cloudShadow;
                            }
                            if(request->cancelled.load())return PreparedPt{};
                            auto upload=std::make_shared<pt::GpuSceneUpload>(pt::prepareGpuScene(*scene,&request->options));
                            if(request->cancelled.load())return PreparedPt{};
                            return PreparedPt{std::move(scene),std::move(upload),cpuSeconds};
                        });
                    }catch(const std::exception &error){ptBuilding.reset();ptFailure(error);}
                }
                if(ptRequest&&ptRequest->cancelled.load()){if(ptSession&&!ptSession->ready()){ptSession->cancel();ptSession.reset();}ptRequest.reset();ptStatus("Raster");}
            };
            auto updatePtPostprocess=[&](const RenderPacket &packet,float exposure) {
                if(auto result=ptPostprocess.poll()){
                    ptDenoiserStats=result->context;
                    if(result->discarded||!ptRequest||result->identity!=std::shared_ptr<const void>(ptRequest)||result->epoch!=ptRequest->epoch||ptRequest->cancelled.load()||!ptSession||!ptSession->ready())++ptDiscarded;
                    else if(!result->error.empty())ptDenoiseMessage=result->error;
                    else if(!result->image.denoised.empty()){
                        try{ptSession->queueDenoisedDisplay(result->image);ptDenoiseSeconds=result->image.denoiseSeconds;ptDenoiseMessage.clear();}
                        catch(const std::exception &error){ptDenoiseMessage=error.what();}
                    }
                }
                if(auto ticket=packet.pathTracingExport;ticket&&!ticket->accepted.exchange(true)){
                    if(ptExport||!ptRequest||ticket->epoch!=ptRequest->epoch||ptRequest->cancelled.load()||!ptSession||!ptSession->ready()||!ptSession->samples()||ticket->prefix.empty())ticket->completion->set_exception(std::make_exception_ptr(std::runtime_error("PT viewport export needs the current sampled epoch and an unused export slot")));
                    else {ptExport=std::make_unique<PendingPtExport>();ptExport->ticket=ticket;ptExport->owner=ptRequest;ptExport->exposure=exposure;}
                }
                if(ptExport&&(!ptRequest||ptExport->owner!=ptRequest||ptRequest->cancelled.load()))ptExport.reset();
                if(!ptRequest||ptRequest->cancelled.load()||!ptSession||!ptSession->ready())return;
                try{ptSession->uploadDenoisedDisplay();}catch(const std::exception &error){ptDenoiseMessage=error.what();}
                const bool enabled=ptRequest->denoise.load()&&pt::denoiserAvailable();
                const auto samples=ptSession->samples();
                const bool checkpoint=enabled&&!packet.pathTracingNavigation&&samples>ptLastRead&&(samples>=ptNextRead||ptSession->complete()||ptRequest->paused.load());
                if(ptPostprocess.busy()||(!ptExport&&!checkpoint))return;
                try {
                    pt::ViewportPostprocessJob job;job.epoch=ptRequest->epoch;job.identity=ptRequest;job.image=ptSession->readback();job.filter=enabled;
                    if(ptExport){
                        job.outputPrefix=ptExport->ticket->prefix;job.exposure=ptExport->exposure;job.exported=ptExport->ticket->completion;
                        const auto &o=ptRequest->options;const auto camera=ptScene->cameraData();
                        job.metadata=nlohmann::json{{"source","frozen progressive GPU viewport"},{"seed",o.seed},{"max_depth",o.maxDepth},{"adaptive",o.adaptive},{"sample_target",o.samples},{"frozen_time_seconds",ptScene->capturedTime()},{"camera_position",{camera.position.x,camera.position.y,camera.position.z}}}.dump();
                        // An explicit export freezes this snapshot. Later camera
                        // changes do not retarget or abandon the accepted file job.
                    }else job.cancelled=[request=ptRequest]{return request->cancelled.load();};
                    if(ptPostprocess.submit(std::move(job))){
                        if(ptExport){ptExport->submitted=true;ptExport.reset();}
                        ptLastRead=samples;while(ptNextRead<=samples)ptNextRead*=4;
                    }
                }catch(const std::exception &error){ptDenoiseMessage=error.what();ptLastRead=samples;ptExport.reset();}
            };
            uint32_t rendererWidth = 0, rendererHeight = 0;
            auto nextAttempt = std::chrono::steady_clock::time_point::min();
            for (;;) {
                std::optional<RenderPacket> packet;
                {TraceScope trace("render/queue-pop","render");packet=queue_.pop();}
                if(!packet)break;
                TraceScope frameTrace("render/frame","render",packet->traceId);
                if(RuntimeTrace::instance().enabled())for(const auto& sample:device_->drainGpuProfile()){
                    if(sample.kind=="render"){
                        RuntimeTrace::instance().gpu(sample.label+"/vertex",sample.vertexStartMilliseconds,sample.vertexEndMilliseconds);
                        RuntimeTrace::instance().gpu(sample.label+"/fragment",sample.fragmentStartMilliseconds,sample.fragmentEndMilliseconds);
                    }else RuntimeTrace::instance().gpu(sample.label+"/"+sample.kind,sample.startMilliseconds,sample.endMilliseconds);
                }
#ifdef __APPLE__
                @autoreleasepool {
#endif
                    if(packet->prepare) {
                        candidate.reset();
                        candidate=std::make_unique<Candidate>();candidate->request=packet->prepare;candidate->world=packet->world;
                        continue;
                    }
                    if(packet->activatePrepared) {
                        if(!candidate || !candidate->ready || candidate->request->cancelled->load() ||
                           candidate->request->token!=packet->activatePrepared)
                            throw std::logic_error("Stale or unprepared scene activation");
                        adapter=std::move(candidate->adapter);renderer=std::move(candidate->renderer);
                        rendererWidth=candidate->world->frame.viewportWidth;rendererHeight=candidate->world->frame.viewportHeight;
                        candidate.reset();nextAttempt=std::chrono::steady_clock::time_point::min();
                        continue;
                    }
                    if(packet->pathTracingCapture){auto request=packet->pathTracingCapture;try{request->completion.set_value(pt::captureProcedural(request->world,device_,request->options));}catch(...){request->completion.set_exception(std::current_exception());}continue;}
                    if(packet->atmosphereCapture) {
                        auto request=packet->atmosphereCapture;
                        try {request->completion.set_value(render::bakeAtmosphere(device_,request->frame));}
                        catch(...) {request->completion.set_exception(std::current_exception());}
                        continue;
                    }
                    // Superseded PT packets are obsolete snapshots, not a
                    // request to redraw the heavy scene as Raster. The next
                    // current/null packet publishes navigation or cancellation.
                    if(packet->pathTracingViewport&&packet->pathTracingViewport->cancelled.load()){
                        updatePt(*packet);
                        if(ptExport&&ptExport->owner->cancelled.load())ptExport.reset();
                        if(auto ticket=packet->pathTracingExport;ticket&&!ticket->accepted.exchange(true))ticket->completion->set_exception(std::make_exception_ptr(std::runtime_error("PT viewport export belongs to a cancelled request")));
                        ++skippedSnapshots_;continue;
                    }
                    const auto started = std::chrono::steady_clock::now();
                    RuntimeTrace::instance().cpu("frame/input-to-consume","render",packet->sampledAt,started,packet->traceId);
                    quality.setEnabled(packet->world->automaticQuality);qualityLevel_=quality.level();
                    auto snapshot=adaptQuality(*packet->world);
                    auto width = snapshot.frame.viewportWidth, height = snapshot.frame.viewportHeight;
                    const auto surface = surfaceExtent_.load();
                    device_->setPresentationExtent(surface == UINT64_MAX ? width : uint32_t(surface >> 32),
                                                   surface == UINT64_MAX ? height : uint32_t(surface));
                    {
                        auto& trace=RuntimeTrace::instance();rhi::FrameStartTiming timing;
                        const auto frameStart=trace.enabled()?RuntimeTrace::Clock::now():RuntimeTrace::Clock::time_point{};
                        device_->beginFrame(trace.enabled()?&timing:nullptr);
                        if(trace.enabled())trace.cpu("render/acquire-and-flight-wait","render",frameStart,RuntimeTrace::Clock::now(),packet->traceId,timing.flightMilliseconds,timing.acquireMilliseconds);
                    }
                    if(candidate) {
                        if(candidate->request->cancelled->load())candidate.reset();
                        else if(!candidate->ready)try {
                            if(!candidate->adapter)candidate->adapter=std::make_unique<render::SceneAdapter>(device_);
                            auto frame=candidate->adapter->resolve(*candidate->world);
                            if(!frame.assetsPending) {
                                candidate->renderer=std::make_unique<render::ForwardPbrRenderer>(device_,rhi::defaultShaderDirectory(),
                                    frame.frame.viewportWidth,frame.frame.viewportHeight,render::PbrPath::Scene);
                                candidate->renderer->render(frame.frame,frame.packets,frame.exposure,2.2f,frame.auxiliaryPackets.empty()?nullptr:&frame.auxiliaryPackets);
                                device_->waitIdle(); // Acknowledge actual GPU preparation before the CPU world changes.
                                candidate->ready=true;candidate->settled=true;candidate->request->completion->set_value();
                            }
                        } catch(...) {candidate->reject(std::current_exception());candidate.reset();device_->waitIdle();}
                    }
                    updatePt(*packet);
                    updatePtPostprocess(*packet,snapshot.exposure);
                    if(ptRequest&&ptRequest->batchRasterPreview)snapshot.rasterInstanceBatching=true;
                    if(ptRequest&&ptRequest->cullRasterPreview)snapshot.rasterFrustumCulling=true;
                    if(!ptRequest){ptPreview.reset();ptPreviewBack.reset();}
                    if(packet->pathTracingNavigation&&ptRequest)ptStatus("Navigating");
                    bool fallback = published && started < nextAttempt;
                    const bool holdPreview=!fallback&&pendingAssetUploads_.load()==0&&published&&ptPreview&&ptPreview->width==width&&ptPreview->height==height&&published->width==width&&published->height==height&&ptRequest&&ptRequest->reusePreparationPreview&&!ptFailed&&!packet->pathTracingNavigation&&(!ptSession||!ptSession->ready());
                    if(holdPreview){
                        auto copy=device_->createCommandList();copy.copyTexture(ptPreview->texture,published->texture);device_->submit(copy);
                        try{if(gui&&packet->screenshot.empty())gui->render(packet->gui,published->texture);}
                        catch(const rhi::ResourceBudgetExceeded& error){fallback=true;++rejectedPublications_;std::lock_guard<std::mutex> lock(failureMutex_);recoveryMessage_=error.what();}
                        ++ptTiming.previewFrames;
                        std::lock_guard<std::mutex> lock(pathTracingMutex_);pathTracingStatus_.previewFrames=ptTiming.previewFrames;pathTracingStatus_.previewFrozen=true;
                    }
                    if (!fallback&&!holdPreview) {
                        bool retryCold=false;
                        do {
                        retryCold=false;
                        adapter->beginPublication();
                        bool rendering = false;
                        std::unique_ptr<PublishedImage> replacement;
                        try {
                            if (!published || published->width != width || published->height != height)
                                replacement = std::make_unique<PublishedImage>(device_, width, height);
                            rhi::TextureHandle output;
                            if(ptSession&&ptSession->ready()&&ptRequest&&ptResident==ptRequest&&!ptRequest->cancelled.load()&&!packet->pathTracingNavigation) {
                                try {
                                    ptSession->pause(ptRequest->paused.load());ptSession->step();
                                    if(ptSession->samples()&&!ptTiming.firstSampleLatencySeconds)ptTiming.firstSampleLatencySeconds=requestAge();
                                    output=ptSession->display(width,height,snapshot.exposure,ptRequest->denoise.load());
                                    ptStatus(ptSession->complete()?"Complete":ptSession->paused()?"Paused":"Rendering");
                                }catch(const std::exception &error){ptFailure(error);}
                            }
                            if(!output) {
                                if (!renderer) {
                                    renderer = std::make_unique<render::ForwardPbrRenderer>(
                                        device_, rhi::defaultShaderDirectory(), width, height, render::PbrPath::Scene);
                                    rendererWidth = width;
                                    rendererHeight = height;
                                }
                                renderer->resize(width, height);
                                rendererWidth = width;
                                rendererHeight = height;
                                auto frame = [&]{TraceScope trace("render/resolve","render",packet->traceId);return adapter->resolve(snapshot);}();
                                ++rasterFrames_;rasterPackets_=frame.rasterBatches.outputPackets;rasterSourcePackets_=frame.rasterBatches.sourcePackets;rasterCulledPackets_=frame.rasterCulledPackets;instancePoseUploadBytes_+=frame.rasterBatches.poseUploadBytes;
                                rasterMaterialBuilds_+=frame.rasterMaterials.builds;rasterMaterialCacheHits_+=frame.rasterMaterials.cacheHits;
                                rasterMaterialVariants_=frame.rasterMaterials.liveVariants;rasterMaterialUploadBytes_+=frame.rasterMaterials.parameterUploadBytes;
                                meshUploadBytes_ += frame.meshUploadBytes;
                                meshUploadChunks_ += frame.meshUploadChunks;
                                pendingMeshUploads_ = frame.meshUploadsPending;
                                pendingAssetUploads_=frame.assetsPending;
                                imageBytes_ = frame.gpuImages.residentBytes;
                                imageUploads_ = frame.gpuImages.uploads;
                                imageCacheHits_ = frame.gpuImages.hits;
                                rendering = true;
                                {TraceScope trace("render/record","render",packet->traceId);renderer->render(frame.frame, frame.packets, frame.exposure,2.2f,frame.auxiliaryPackets.empty()?nullptr:&frame.auxiliaryPackets);}
                                auto feedbackFrame=frame.frame;feedbackFrame.viewProjection=renderer->renderedViewProjection();
                                {TraceScope trace("render/feedback","render",packet->traceId);adapter->recordVirtualFeedback(feedbackFrame,renderer->depthView(),renderer->shadowVisibilityViews());}
                                output=renderer->output();
                            }
                            // Keep a clean double-buffered preview. ImGui draws
                            // into output, so caching the final publication would
                            // accumulate old UI pixels when preparation advances.
                            if(ptRequest){
                                if(!ptPreviewBack||ptPreviewBack->width!=width||ptPreviewBack->height!=height)ptPreviewBack=std::make_unique<PublishedImage>(device_,width,height);
                                auto clean=device_->createCommandList();clean.copyTexture(output,ptPreviewBack->texture);device_->submit(clean);
                            }
                            // Screenshot frames publish only the scene; editor overlays stay out of captures.
                            if(gui&&packet->screenshot.empty())gui->render(packet->gui,output);
                            auto &target = replacement ? *replacement : *published;
                            auto copy = device_->createCommandList();
                            copy.copyTexture(output, target.texture);
                            device_->submit(copy);
                            adapter->commitPublication();
                            if (replacement)
                                published = std::move(replacement);
                            if(ptRequest)ptPreview.swap(ptPreviewBack);
                            nextAttempt = std::chrono::steady_clock::time_point::min();
                        } catch (const rhi::ResourceBudgetExceeded &error) {
                            adapter->rollbackPublication();
                            replacement.reset();
                            // If candidate rendering mutated effects, rebuild them on the next attempt.
                            if (rendering || (published && (rendererWidth != published->width ||
                                                           rendererHeight != published->height))) {
                                renderer.reset();
                                rendererWidth = rendererHeight = 0;
                            }
                            auto images = render::GpuImageCache::forDevice(device_);
                            images->releaseIdle();
                            device_->waitIdle(); // Complete candidate work and retirement before retry.
                            const bool reduced=quality.onPressure();
                            if(reduced) {
                                qualityLevel_=quality.level();renderer.reset();adapter->invalidateAssets();images->releaseIdle();device_->waitIdle();
                                snapshot=adaptQuality(*packet->world);
                            }
                            if(!published) {
                                if(!reduced)throw;
                                retryCold=true;continue; // At most three smaller tiers; retry this same packet.
                            }
                            ++rejectedPublications_;
                            {
                                std::lock_guard<std::mutex> lock(failureMutex_);
                                if (recoveryMessage_ != error.what())
                                    std::cerr << "Scene GPU publication rejected; keeping last image: "
                                              << error.what() << '\n';
                                recoveryMessage_ = error.what();
                            }
                            fallback = true;
                            // Avoid uploading the same oversized candidate on every UI tick.
                            nextAttempt = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
                        }
                        } while(retryCold);
                    }
                    if (fallback)
                        ++fallbackFrames_;
                    width = published->width;
                    height = published->height;
                    if (!packet->screenshot.empty()) {
                        auto pixels = device_->readTexture(published->texture);
                        auto path = std::filesystem::path(packet->screenshot);
                        if (!path.parent_path().empty())
                            std::filesystem::create_directories(path.parent_path());
                        std::ofstream file(path, std::ios::binary);
                        file << "P6\n" << width << " " << height << "\n255\n";
                        for (size_t i = 0; i < pixels.size(); i += 4)
                            file.write(reinterpret_cast<const char *>(pixels.data() + i), 3);
                        if (!file)
                            throw std::runtime_error("Cannot write render screenshot");
                    }
                    device_->copyToBackbuffer(published->texture);
                    const auto sampledAt=packet->sampledAt;
                    const auto traceId=packet->traceId;
                    auto completed=[this,sampledAt,traceId]{
                        if(sampledAt!=std::chrono::steady_clock::time_point{}) {
                            const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-sampledAt).count();
                            peakCompletionLatency_=std::max(peakCompletionLatency_.load(),elapsed);
                            RuntimeTrace::instance().cpu("frame/input-to-completion-ack","completion",sampledAt,std::chrono::steady_clock::now(),traceId);
                        }
                    };
                    {TraceScope trace("render/present-and-flight-wait","render",traceId);
                        // Retain the old split only for controlled diagnostic A/B.
                        if(std::getenv("SCENERENDERER_RUNTIME_SPLIT_PRESENT")){device_->checkpoint(std::move(completed));device_->present();}
                        else device_->present(std::move(completed));
                    }
                    ++framesRendered_;
                    const auto memory = device_->resourceMemory();
                    peakResourceBytes_ = uint64_t(memory.peakBytes);
                    currentResourceBytes_ = uint64_t(memory.usedBytes());
                    memoryPressureEvents_ = memory.pressureEvents;
                    const auto timing=device_->gpuTimingStats();if(timing.supported)peakGpuMilliseconds_=std::max(peakGpuMilliseconds_.load(),timing.peakMilliseconds);
                    const auto native=device_->nativeMemoryStats();
                    if(native.supported){nativeMemorySupported_=true;currentNativeBytes_=native.usedBytes;peakNativeBytes_=std::max(peakNativeBytes_.load(),native.usedBytes);}
                    const auto pipelines = device_->pipelineCacheStats();
                    pipelineBuilds_ = pipelines.graphicsBuilds + pipelines.computeBuilds;
                    pipelineCacheHits_ = pipelines.hits;
                    renderMilliseconds_ =
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                            .count();
                    timings[samples % timings.size()] = renderMilliseconds_.load();
                    ++samples;
                    if (samples % 32 == 0)
                        publishTimings();
#ifdef __APPLE__
                }
#endif
            }
            device_->waitIdle();
            if(RuntimeTrace::instance().enabled())for(const auto& sample:device_->drainGpuProfile()){
                if(sample.kind=="render"){
                    RuntimeTrace::instance().gpu(sample.label+"/vertex",sample.vertexStartMilliseconds,sample.vertexEndMilliseconds);
                    RuntimeTrace::instance().gpu(sample.label+"/fragment",sample.fragmentStartMilliseconds,sample.fragmentEndMilliseconds);
                }else RuntimeTrace::instance().gpu(sample.label+"/"+sample.kind,sample.startMilliseconds,sample.endMilliseconds);
            }
            renderer.reset();
        } catch (...) {
            std::lock_guard<std::mutex> lock(failureMutex_);
            failure_ = std::current_exception();
            queue_.close();
        }
        // ImGui context stays on the UI thread; GPU backend destruction never touches it.
        publishTimings();
        gui.reset();
        try {
            if (device_->frameActive())
                device_->endFrame();
            device_->waitIdle();
        } catch (...) {
            std::lock_guard<std::mutex> lock(failureMutex_);
            if (!failure_)
                failure_ = std::current_exception();
        }
#ifdef __APPLE__
    }
#endif
}
bool RenderRuntime::trySubmitFrame(RenderPacket packet) {
    rethrowFailure();
    if(!packet.world || packet.prepare || packet.activatePrepared || packet.atmosphereCapture || packet.pathTracingCapture)
        throw std::invalid_argument("Nonblocking submission accepts ordinary frame snapshots only");
    if(packet.pathTracingViewport && (!packet.pathTracingViewport->world || !packet.pathTracingViewport->epoch))throw std::invalid_argument("PT viewport needs a frozen world and epoch");
    if(packet.sampledAt==std::chrono::steady_clock::time_point{})packet.sampledAt=std::chrono::steady_clock::now();
    packet.traceId=RuntimeTrace::instance().nextId();
    const bool accepted=queue_.tryPush(std::move(packet));if(!accepted)++skippedSnapshots_;
    rethrowFailure();return accepted;
}
bool RenderRuntime::submit(RenderPacket packet) {
    rethrowFailure();
    if (!packet.world && !packet.activatePrepared)
        throw std::invalid_argument("Render packet needs an immutable world");
    if(packet.pathTracingViewport && (!packet.pathTracingViewport->world || !packet.pathTracingViewport->epoch))
        throw std::invalid_argument("PT viewport needs a frozen world and epoch");
    if(packet.sampledAt==std::chrono::steady_clock::time_point{})packet.sampledAt=std::chrono::steady_clock::now();
    const auto started = std::chrono::steady_clock::now();
    bool accepted = queue_.push(std::move(packet));
    const double waited =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    peakQueueWaitMilliseconds_ = std::max(peakQueueWaitMilliseconds_.load(), waited);
    rethrowFailure();
    return accepted;
}
ScenePreparationTicket RenderRuntime::prepareScene(std::shared_ptr<const render::RenderWorldSnapshot> world,
                                                        std::shared_ptr<std::atomic<bool>> cancelled) {
    if(!world)throw std::invalid_argument("Scene preparation needs a detached world");
    auto request=std::make_shared<ScenePreparation>();request->token=++nextPreparation_;
    request->cancelled=cancelled?std::move(cancelled):std::make_shared<std::atomic<bool>>(false);
    request->completion=std::make_shared<std::promise<void>>();
    ScenePreparationTicket ticket{request->token,request->completion->get_future(),request->cancelled};
    auto snapshot=std::make_shared<render::RenderWorldSnapshot>(*world);snapshot->asynchronousStreaming=true;
    RenderPacket packet;packet.world=std::move(snapshot);packet.prepare=request;
    if(!submit(std::move(packet)))throw std::runtime_error("Renderer closed during scene preparation");
    return ticket;
}
void RenderRuntime::activatePrepared(uint64_t token) {
    if(!token)throw std::invalid_argument("Empty prepared scene token");
    RenderPacket packet;packet.activatePrepared=token;
    if(!submit(std::move(packet)))throw std::runtime_error("Renderer closed during scene activation");
}
render::RenderWorldSnapshot RenderRuntime::capturePathTracingScene(const render::RenderWorldSnapshot &world,const pt::CaptureOptions &options){
    if(std::this_thread::get_id()==thread_.get_id())throw std::logic_error("PT capture cannot block its GPU owner thread");
    rethrowFailure();auto request=std::make_shared<PathTracingCapture>();request->world=world;request->options=options;auto result=request->completion.get_future();RenderPacket packet;packet.pathTracingCapture=request;
    if(!queue_.push(std::move(packet)))throw std::runtime_error("Render runtime closed during PT capture");while(result.wait_for(std::chrono::milliseconds(50))!=std::future_status::ready)rethrowFailure();rethrowFailure();return result.get();
}
render::BakedAtmosphere RenderRuntime::captureAtmosphere(const render::FrameData &frame) {
    if(std::this_thread::get_id()==thread_.get_id())throw std::logic_error("Atmosphere capture cannot block its own GPU thread");
    rethrowFailure();auto request=std::make_shared<AtmosphereCapture>();request->frame=frame;
    auto result=request->completion.get_future();RenderPacket packet;packet.atmosphereCapture=request;
    if(!queue_.push(std::move(packet)))throw std::runtime_error("Render runtime closed during atmosphere capture");
    while(result.wait_for(std::chrono::milliseconds(50))!=std::future_status::ready)rethrowFailure();
    rethrowFailure();return result.get();
}
PathTracingViewportStatus RenderRuntime::pathTracingStatus() const {
    std::lock_guard<std::mutex> lock(pathTracingMutex_);return pathTracingStatus_;
}
std::string RenderRuntime::lastRecoveryMessage() const {
    std::lock_guard<std::mutex> lock(failureMutex_);
    return recoveryMessage_;
}
void RenderRuntime::rethrowFailure() const {
    std::lock_guard<std::mutex> lock(failureMutex_);
    if (failure_)
        std::rethrow_exception(failure_);
}
void RenderRuntime::finish() {
    queue_.close();
    if (thread_.joinable()) {
        thread_.join();
        RuntimeTrace::instance().write();
        device_->adoptCurrentThread();
    }
    rethrowFailure();
}
} // namespace engine
