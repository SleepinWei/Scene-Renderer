#pragma once
#include "engine/BoundedQueue.h"
#include "renderer/rhi/SceneSnapshot.h"
#include "renderer/rhi/GuiRenderer.h"
#include "renderer/rhi/AtmosphereBake.h"
#include "PT/ProceduralCapture.h"
#include "PT/GpuPathTracer.h"
#include "PT/ViewportPostprocess.h"
#include <future>
#include <atomic>
#include <thread>
namespace engine {
struct ScenePreparation {
    uint64_t token=0;
    std::shared_ptr<std::atomic<bool>> cancelled;
    std::shared_ptr<std::promise<void>> completion;
};
struct ScenePreparationTicket {
    uint64_t token=0;
    std::future<void> ready;
    std::shared_ptr<std::atomic<bool>> cancelled;
    void cancel() const {if(cancelled)cancelled->store(true);}
};
struct AtmosphereCapture {
    render::FrameData frame;
    std::promise<render::BakedAtmosphere> completion;
};
struct PathTracingCapture {
    render::RenderWorldSnapshot world;
    pt::CaptureOptions options;
    std::promise<render::RenderWorldSnapshot> completion;
};
struct PathTracingViewportLighting {
    std::shared_ptr<pt::Environment> environment;
    glm::vec3 sunDirection{0,1,0},sunIrradiance{0};
    float sunRadius=0;
};
struct PathTracingViewportRequest {
    // Immutable after publication except the atomic controls. Replacing a
    // request must cancel its predecessor so queued frames cannot resurrect it.
    uint64_t epoch=0;
    std::chrono::steady_clock::time_point createdAt=std::chrono::steady_clock::now();
    std::shared_ptr<const render::RenderWorldSnapshot> world;
    // Optional frozen package/HDR lighting. Treat its Environment as immutable;
    // a replacement owner invalidates camera-only reuse.
    std::shared_ptr<const PathTracingViewportLighting> lighting;
    pt::Options options;
    bool forceRecapture=true; // Explicit R refreshes camera-dependent procedural captures.
    bool reusePreparationPreview=true,batchRasterPreview=true, cullRasterPreview=true;
    size_t preparationByteBudget=4*1024*1024;
    double preparationSecondsBudget=.004;
    std::atomic<bool> paused{false},cancelled{false};
    std::atomic<bool> denoise{true}; // Display control; does not reset raw film.
};
struct PathTracingViewportExport {
    uint64_t epoch=0;
    std::string prefix;
    std::atomic<bool> accepted{false}; // Same ticket may accompany skipped/queued frames.
    std::shared_ptr<std::promise<std::string>> completion=std::make_shared<std::promise<std::string>>();
};
struct PathTracingViewportStatus {
    uint64_t epoch=0;
    uint32_t samples=0,targetSamples=0;
    std::string state="Raster",message;
    uint32_t cameraMediaCount=0;
    uint64_t previewFrames=0;
    bool previewFrozen=false;
    uint64_t tlasBuilds=0,tlasRefits=0,tlasReuses=0,emitterReuses=0;
    uint64_t sceneUploads=0,sceneCacheHits=0,sceneUploadBytes=0,gpuCacheBytes=0,blasCacheBytes=0;
    uint64_t preparationBytes=0,preparationTotalBytes=0,preparationSteps=0,lastPreparationWorkBytes=0;
    double pipelineSeconds=0,allocationSeconds=0,uploadSeconds=0,cacheCompareSeconds=0;
    double requestAcceptedSeconds=0,captureSeconds=0,cpuSceneSeconds=0,cpuExportSeconds=0;
    double readyLatencySeconds=0,firstSampleLatencySeconds=0,maxPreparationFrameSeconds=0;
    double startupTraceSeconds=0,startupDisplaySeconds=0;
    std::string startupWarmupError;
    uint64_t cloneBytes=0,patchBytes=0,patchBuffers=0,maxPreparationWorkBytes=0;
    uint32_t denoisedSamples=0;
    uint64_t filmReadbacks=0,filmReadbackBytes=0,denoiseDiscarded=0;
    double readbackSeconds=0,denoiseSeconds=0,denoisedUploadSeconds=0;
    uint64_t denoisedUploadBytes=0;
    pt::DenoiserStats denoiserContext;
    bool denoiserBusy=false,denoiserAvailable=false;
    std::string denoiseMessage;
};
struct RenderPacket {
    std::shared_ptr<const render::RenderWorldSnapshot> world;
    render::GuiFrame gui;
    std::string screenshot; // Nonempty requests a scene-only PPM and suppresses this frame's GUI.
    std::shared_ptr<AtmosphereCapture> atmosphereCapture;
    std::shared_ptr<PathTracingCapture> pathTracingCapture;
    std::shared_ptr<PathTracingViewportRequest> pathTracingViewport;
    std::shared_ptr<PathTracingViewportExport> pathTracingExport;
    bool pathTracingNavigation=false; // Raster preview while PT camera settles.
    std::shared_ptr<ScenePreparation> prepare;
    uint64_t activatePrepared=0;
    std::chrono::steady_clock::time_point sampledAt;
    uint64_t traceId=0;
};
class RenderRuntime {
  public:
    RenderRuntime(std::shared_ptr<rhi::GraphicsDevice>, std::unique_ptr<render::GuiRenderer>,
                  size_t queueCapacity = 2,bool prewarmPathTracing = false);
    ~RenderRuntime();
    bool submit(RenderPacket);
    bool trySubmitFrame(RenderPacket); // Full queue skips a render snapshot, never blocks event/simulation ticks.
    // Synchronous CPU request; GPU bake executes exclusively on the render owner thread.
    render::BakedAtmosphere captureAtmosphere(const render::FrameData &);
    render::RenderWorldSnapshot capturePathTracingScene(const render::RenderWorldSnapshot &,const pt::CaptureOptions & = {});
    ScenePreparationTicket prepareScene(std::shared_ptr<const render::RenderWorldSnapshot>,std::shared_ptr<std::atomic<bool>> cancelled={});
    void activatePrepared(uint64_t token);
    void notifySurfaceExtent(uint32_t width, uint32_t height) {
        surfaceExtent_.store((uint64_t(width) << 32) | height);
    }
    void rethrowFailure() const;
    void finish(); // Drain, join and return device ownership to caller.
    uint64_t skippedSnapshots() const {return skippedSnapshots_.load();}
    uint64_t framesRendered() const { return framesRendered_.load(); }
    double renderMilliseconds() const { return renderMilliseconds_.load(); }
    double renderP95Milliseconds() const { return renderP95Milliseconds_.load(); }
    double renderP99Milliseconds() const { return renderP99Milliseconds_.load(); }
    double peakQueueWaitMilliseconds() const { return peakQueueWaitMilliseconds_.load(); }
    uint64_t imageBytes() const { return imageBytes_.load(); }
    uint64_t imageUploads() const { return imageUploads_.load(); }
    uint64_t imageCacheHits() const { return imageCacheHits_.load(); }
    uint64_t peakResourceBytes() const { return peakResourceBytes_.load(); }
    uint64_t currentResourceBytes() const { return currentResourceBytes_.load(); }
    uint64_t rejectedPublications() const { return rejectedPublications_.load(); }
    uint64_t fallbackFrames() const { return fallbackFrames_.load(); }
    uint64_t memoryPressureEvents() const { return memoryPressureEvents_.load(); }
    uint64_t meshUploadBytes() const { return meshUploadBytes_.load(); }
    uint64_t meshUploadChunks() const { return meshUploadChunks_.load(); }
    uint64_t pendingMeshUploads() const { return pendingMeshUploads_.load(); }
    uint64_t pendingAssetUploads()const{return pendingAssetUploads_.load();}
    uint64_t rasterFrames()const{return rasterFrames_.load();}
    uint64_t rasterCulledPackets()const{return rasterCulledPackets_.load();}
    uint64_t rasterPackets()const{return rasterPackets_.load();}
    uint64_t rasterSourcePackets()const{return rasterSourcePackets_.load();}
    uint64_t instancePoseUploadBytes()const{return instancePoseUploadBytes_.load();}
    uint64_t rasterMaterialBuilds()const{return rasterMaterialBuilds_.load();}
    uint64_t rasterMaterialCacheHits()const{return rasterMaterialCacheHits_.load();}
    uint64_t rasterMaterialVariants()const{return rasterMaterialVariants_.load();}
    uint64_t rasterMaterialUploadBytes()const{return rasterMaterialUploadBytes_.load();}
    double peakGpuSubmissionMilliseconds() const {return peakGpuMilliseconds_.load();}
    double peakCompletionLatencyMilliseconds() const {return peakCompletionLatency_.load();}
    uint32_t qualityLevel() const {return qualityLevel_.load();}
    bool nativeMemorySupported() const {return nativeMemorySupported_.load();}
    uint64_t peakNativeBytes() const {return peakNativeBytes_.load();}
    uint64_t currentNativeBytes() const {return currentNativeBytes_.load();}
    uint64_t pipelineBuilds() const { return pipelineBuilds_.load(); }
    uint64_t pipelineCacheHits() const { return pipelineCacheHits_.load(); }
    std::string lastRecoveryMessage() const;
    PathTracingViewportStatus pathTracingStatus() const;

  private:
    void run(std::unique_ptr<render::GuiRenderer>);
    std::shared_ptr<rhi::GraphicsDevice> device_;
    bool prewarmPathTracing_=false;
    BoundedQueue<RenderPacket> queue_;
    std::thread thread_;
    mutable std::mutex failureMutex_;
    std::exception_ptr failure_;
    uint64_t nextPreparation_=0; // Producer-thread only, like submit().
    std::atomic<uint64_t> framesRendered_{0},skippedSnapshots_{0};
    std::atomic<double> renderMilliseconds_{0};
    std::atomic<double> peakGpuMilliseconds_{0},peakCompletionLatency_{0};
    std::atomic<double> renderP95Milliseconds_{0}, renderP99Milliseconds_{0}, peakQueueWaitMilliseconds_{0};
    std::atomic<uint64_t> imageBytes_{0}, imageUploads_{0}, imageCacheHits_{0};
    std::atomic<uint64_t> peakResourceBytes_{0};
    std::atomic<uint64_t> currentResourceBytes_{0};
    std::atomic<uint64_t> rejectedPublications_{0}, fallbackFrames_{0}, memoryPressureEvents_{0};
    std::atomic<uint64_t> meshUploadBytes_{0}, meshUploadChunks_{0}, pendingMeshUploads_{0};
    std::atomic<uint64_t> rasterFrames_{0},rasterPackets_{0},rasterSourcePackets_{0},rasterCulledPackets_{0},instancePoseUploadBytes_{0};
    std::atomic<uint64_t> rasterMaterialBuilds_{0},rasterMaterialCacheHits_{0},rasterMaterialVariants_{0},rasterMaterialUploadBytes_{0};
    std::atomic<uint64_t> pendingAssetUploads_{0};
    std::atomic<uint64_t> peakNativeBytes_{0};
    std::atomic<uint64_t> currentNativeBytes_{0};
    std::atomic<bool> nativeMemorySupported_{false};
    std::atomic<uint32_t> qualityLevel_{0};
    std::atomic<uint64_t> pipelineBuilds_{0}, pipelineCacheHits_{0};
    std::string recoveryMessage_;
    mutable std::mutex pathTracingMutex_;
    PathTracingViewportStatus pathTracingStatus_;
    std::atomic<uint64_t> surfaceExtent_{UINT64_MAX};
};
} // namespace engine
