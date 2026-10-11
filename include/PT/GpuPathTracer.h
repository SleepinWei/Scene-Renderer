#pragma once
#include "PT/CpuPathTracer.h"
namespace render {class Resources;}
namespace pt {
struct GpuPipelineWarmupStats {double traceSeconds=0,displaySeconds=0;};
// Owner-thread startup work. Keep leases alive so raster pipeline churn cannot
// evict the warmed trace/display pipelines; shader contents still key the cache.
class GpuPipelineWarmup {
  public:
    explicit GpuPipelineWarmup(std::shared_ptr<rhi::GraphicsDevice>);
    ~GpuPipelineWarmup();
    const GpuPipelineWarmupStats &statistics() const;
    GpuPipelineWarmup(const GpuPipelineWarmup &)=delete;
    GpuPipelineWarmup &operator=(const GpuPipelineWarmup &)=delete;
  private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
// Device-owner-thread cache of immutable scene buffers. Each semantic slot
// retains its latest exact byte contents; live sessions hold independent leases.
class GpuSceneCache {
  public:
    explicit GpuSceneCache(std::shared_ptr<rhi::GraphicsDevice>,size_t byteBudget=256*1024*1024);
    ~GpuSceneCache();
    GpuSceneCache(const GpuSceneCache &)=delete;
    GpuSceneCache &operator=(const GpuSceneCache &)=delete;
    size_t retainedBytes() const;
    struct Lease;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend struct GpuSceneCacheAccess;
};
class PhotonMap;
struct GpuSceneUpload {
    SceneData data;
    std::vector<glm::vec4> environment;
    std::shared_ptr<const PhotonMap> photons;
    double exportSeconds=0;
};
// Pure CPU work: may be called on a preparation worker before GPU construction.
GpuSceneUpload prepareGpuScene(const CpuScene &,const Options * = nullptr);
// Owner-thread PT probe entry. Uses the same Kernel, scene leases, BSDF and
// traversal as camera PT; emits six face-center directions into an 80-byte
// Pixel buffer. mean = RGB radiance + signed first-hit distance (zero on miss).
// The source scene must outlive this object. No film readback in record().
class GpuProbeTracer {
  public:
    GpuProbeTracer(const CpuScene &,std::shared_ptr<rhi::GraphicsDevice>,uint32_t resolution,
                   uint32_t samples=4,uint32_t depth=8,std::shared_ptr<GpuSceneCache> = {},
                   const GpuSceneUpload * = nullptr,GpuTraversal = GpuTraversal::Auto);
    ~GpuProbeTracer();
    rhi::BufferHandle record(render::Resources &,rhi::CommandList &,glm::vec3 origin,uint32_t sequence,float maxDistance);
    std::vector<glm::vec4> readCapture();
    uint32_t resolution() const;
    bool nativeRayTracing() const;
  private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
struct GpuPreparationStats {
    uint64_t totalBytes=0,completedBytes=0,transferredBytes=0;
    uint64_t steps=0,lastStepBytes=0,lastStepWorkBytes=0;
    uint64_t identityCacheHits=0,identityCacheBytes=0;
    uint64_t clonedBytes=0,patchBytes=0,patchBuffers=0,patchRanges=0;
    double cloneSeconds=0;
    double pipelineSeconds=0,allocationSeconds=0,cacheCompareSeconds=0,uploadSeconds=0,finalizeSeconds=0;
};
// All methods, including destruction, run on the device owner thread. Scene
// The pointer overload prepares synchronously for offline callers. The shared
// payload overload starts a deferred job; it retains the worker's payload.
// step() submits at most one sample batch. Display never reads the film to CPU.
class GpuRenderSession {
  public:
    GpuRenderSession(const CpuScene &,const Options &,std::shared_ptr<rhi::GraphicsDevice>,
                     std::shared_ptr<GpuSceneCache> = {},const GpuSceneUpload * = nullptr);
    GpuRenderSession(const CpuScene &,const Options &,std::shared_ptr<rhi::GraphicsDevice>,
                     std::shared_ptr<GpuSceneCache>,std::shared_ptr<const GpuSceneUpload>);
    ~GpuRenderSession();
    GpuRenderSession(const GpuRenderSession &)=delete;
    GpuRenderSession &operator=(const GpuRenderSession &)=delete;
    // Work budget includes exact comparisons, GPU clones (with CPU mirror) and
    // host uploads, all in aligned chunks. Time is cooperative: pipeline,
    // allocation, individual copies / writes, native AS build and training are indivisible.
    bool prepareStep(size_t byteBudget=4*1024*1024,double secondsBudget=.004);
    bool ready() const;
    const GpuPreparationStats &preparationStatistics() const;
    bool step();
    void pause(bool);
    void cancel();
    void reset(); // Keep frozen scene/training, restart the same sample sequence.
    void resize(uint32_t width,uint32_t height); // Reset film; frozen camera stays unchanged.
    void updateCamera(const CpuScene &,float aspect); // Same scene; reset film, retain geometry and lighting.
    bool paused() const;
    bool cancelled() const;
    bool complete() const;
    uint32_t samples() const;
    const Image &statistics() const; // Film/stat counters requiring readback may lag.
    Image readback();
    // Letterbox the frozen camera aspect; exposure affects display only.
    rhi::TextureHandle display(uint32_t width,uint32_t height,float exposure,bool filtered=true);
    // Derived display only; never writes the progressive film. Publish after
    // all Float4 pixels are uploaded. Film reset clears pending/ready results.
    void queueDenoisedDisplay(const Image &);
    bool uploadDenoisedDisplay(size_t byteBudget=4*1024*1024,double secondsBudget=.004);
    uint32_t denoisedSamples() const;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Compute PT with optional native Metal traversal and portable software fallback.
// Device must belong to the calling thread.
Image renderGpu(const CpuScene &,const Options &,std::shared_ptr<rhi::GraphicsDevice>,
                const std::function<void(const Image &)> &progress = {});
void validateGpuPathTracing(std::shared_ptr<rhi::GraphicsDevice>);
} // namespace pt
