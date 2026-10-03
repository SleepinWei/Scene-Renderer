#pragma once
#include "engine/BoundedQueue.h"
#include "renderer/rhi/SceneSnapshot.h"
#include "renderer/rhi/GuiRenderer.h"
#include "renderer/rhi/AtmosphereBake.h"
#include <future>
#include <atomic>
#include <thread>
namespace engine {
struct AtmosphereCapture {
    render::FrameData frame;
    std::promise<render::BakedAtmosphere> completion;
};
struct RenderPacket {
    std::shared_ptr<const render::RenderWorldSnapshot> world;
    render::GuiFrame gui;
    std::string screenshot;
    std::shared_ptr<AtmosphereCapture> atmosphereCapture;
};
class RenderRuntime {
  public:
    RenderRuntime(std::shared_ptr<rhi::GraphicsDevice>, std::unique_ptr<render::GuiRenderer>,
                  size_t queueCapacity = 2);
    ~RenderRuntime();
    bool submit(RenderPacket);
    // Synchronous CPU request; GPU bake executes exclusively on the render owner thread.
    render::BakedAtmosphere captureAtmosphere(const render::FrameData &);
    void notifySurfaceExtent(uint32_t width, uint32_t height) {
        surfaceExtent_.store((uint64_t(width) << 32) | height);
    }
    void rethrowFailure() const;
    void finish(); // Drain, join and return device ownership to caller.
    uint64_t framesRendered() const { return framesRendered_.load(); }
    double renderMilliseconds() const { return renderMilliseconds_.load(); }
    double renderP95Milliseconds() const { return renderP95Milliseconds_.load(); }
    double renderP99Milliseconds() const { return renderP99Milliseconds_.load(); }
    double peakQueueWaitMilliseconds() const { return peakQueueWaitMilliseconds_.load(); }
    uint64_t imageBytes() const { return imageBytes_.load(); }
    uint64_t imageUploads() const { return imageUploads_.load(); }
    uint64_t imageCacheHits() const { return imageCacheHits_.load(); }
    uint64_t peakResourceBytes() const { return peakResourceBytes_.load(); }
    uint64_t rejectedPublications() const { return rejectedPublications_.load(); }
    uint64_t fallbackFrames() const { return fallbackFrames_.load(); }
    uint64_t memoryPressureEvents() const { return memoryPressureEvents_.load(); }
    uint64_t meshUploadBytes() const { return meshUploadBytes_.load(); }
    uint64_t meshUploadChunks() const { return meshUploadChunks_.load(); }
    uint64_t pendingMeshUploads() const { return pendingMeshUploads_.load(); }
    uint64_t pipelineBuilds() const { return pipelineBuilds_.load(); }
    uint64_t pipelineCacheHits() const { return pipelineCacheHits_.load(); }
    std::string lastRecoveryMessage() const;

  private:
    void run(std::unique_ptr<render::GuiRenderer>);
    std::shared_ptr<rhi::GraphicsDevice> device_;
    BoundedQueue<RenderPacket> queue_;
    std::thread thread_;
    mutable std::mutex failureMutex_;
    std::exception_ptr failure_;
    std::atomic<uint64_t> framesRendered_{0};
    std::atomic<double> renderMilliseconds_{0};
    std::atomic<double> renderP95Milliseconds_{0}, renderP99Milliseconds_{0}, peakQueueWaitMilliseconds_{0};
    std::atomic<uint64_t> imageBytes_{0}, imageUploads_{0}, imageCacheHits_{0};
    std::atomic<uint64_t> peakResourceBytes_{0};
    std::atomic<uint64_t> rejectedPublications_{0}, fallbackFrames_{0}, memoryPressureEvents_{0};
    std::atomic<uint64_t> meshUploadBytes_{0}, meshUploadChunks_{0}, pendingMeshUploads_{0};
    std::atomic<uint64_t> pipelineBuilds_{0}, pipelineCacheHits_{0};
    std::string recoveryMessage_;
    std::atomic<uint64_t> surfaceExtent_{UINT64_MAX};
};
} // namespace engine
