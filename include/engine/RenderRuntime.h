#pragma once
#include "engine/BoundedQueue.h"
#include "renderer/rhi/SceneSnapshot.h"
#include "renderer/rhi/GuiRenderer.h"
#include <atomic>
#include <thread>
namespace engine {
struct RenderPacket {
    std::shared_ptr<const render::RenderWorldSnapshot> world;
    render::GuiFrame gui;
    std::string screenshot;
};
class RenderRuntime {
  public:
    RenderRuntime(std::shared_ptr<rhi::GraphicsDevice>, std::unique_ptr<render::GuiRenderer>,
                  size_t queueCapacity = 2);
    ~RenderRuntime();
    bool submit(RenderPacket);
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
    std::atomic<uint64_t> surfaceExtent_{UINT64_MAX};
};
} // namespace engine
