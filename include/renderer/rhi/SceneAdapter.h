#pragma once
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "renderer/rhi/SceneSnapshot.h"
#include "renderer/rhi/GpuImageCache.h"
class RenderScene;
namespace render {
struct MeshUploadBudget {
    // Async static meshes only. CPU time is a soft resolve-wide target;
    // at least one chunk can advance, and native allocation is indivisible.
    size_t bytesPerFrame = 8 * 1024 * 1024, bytesPerChunk = 256 * 1024;
    double cpuMilliseconds = 2;
    uint32_t maxPendingMeshes = 4;
};
struct SceneFrame {
    FrameData frame;
    std::vector<DrawPacket> packets;
    float exposure = 1;
    size_t uploadBytes = 0;
    uint32_t assetUploads = 0, assetsPending = 0;
    GpuImageCacheStats gpuImages;
    size_t imageUploadBytes=0;uint32_t imageUploadChunks=0;
    size_t meshUploadBytes = 0;
    uint32_t meshUploadChunks = 0, meshUploadsPending = 0;
    double resolveCpuMilliseconds = 0; // Whole resolve, not a GPU upload timestamp.
};
class SceneAdapter {
  public:
    explicit SceneAdapter(std::shared_ptr<rhi::GraphicsDevice>);
    ~SceneAdapter();
    SceneFrame collect(const std::shared_ptr<RenderScene> &, float timeOverride = -1);
    SceneFrame resolve(const RenderWorldSnapshot &); // Render-thread only.
    // Extend cache publication across renderer/GUI work. Every begin must commit or rollback.
    // Dynamic compute output is mutable; use an independent successful image for frame fallback.
    void setMeshUploadBudget(MeshUploadBudget);
    void beginPublication();
    void commitPublication();
    void rollbackPublication();
    void recordVirtualFeedback(const FrameData&,rhi::TextureViewHandle,const std::vector<glm::mat4>& auxiliaryViews={});
    void invalidateAssets(); // Call after changing cached CPU geometry/images.
  private:
    struct Cache;
    std::shared_ptr<rhi::GraphicsDevice> device_;
    std::unique_ptr<Cache> cache_, previous_;
    SceneFrame resolveCandidate(const RenderWorldSnapshot &);
    MeshUploadBudget meshUploadBudget_;
    SceneSnapshotBuilder builder_; // Single-thread compatibility capture.
};
void validateEngineBasics(std::shared_ptr<rhi::GraphicsDevice>);
void validateSceneSolarControls(std::shared_ptr<rhi::GraphicsDevice>);
std::shared_ptr<RenderScene> makeForwardDemoScene();
void runForwardScene(int argc, char **argv);
} // namespace render
