#pragma once
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "renderer/rhi/SceneSnapshot.h"
#include "renderer/rhi/GpuImageCache.h"
#include "renderer/rhi/RasterInstanceBatcher.h"
#include "renderer/rhi/GpuVegetation.h"
class RenderScene;
namespace render {
struct MeshUploadBudget {
    // Async static meshes only. CPU time is a soft resolve-wide target;
    // at least one chunk can advance, and native allocation is indivisible.
    size_t bytesPerFrame = 8 * 1024 * 1024, bytesPerChunk = 256 * 1024;
    double cpuMilliseconds = 2;
    uint32_t maxPendingMeshes = 4;
};
struct RasterMaterialStats {
    uint64_t builds = 0, cacheHits = 0, liveVariants = 0;
    // Parameter and extension UBOs only; excludes images, bindings and driver allocations.
    uint64_t parameterUploadBytes = 0;
};
struct SceneFrame {
    FrameData frame;
    std::vector<DrawPacket> packets;
    // Present only when main-view culling is active. Shadows and water use this full list.
    std::vector<DrawPacket> auxiliaryPackets;
    uint64_t rasterCulledPackets=0;
    float exposure = 1;
    size_t uploadBytes = 0;
    uint32_t assetUploads = 0, assetsPending = 0;
    GpuImageCacheStats gpuImages;
    size_t imageUploadBytes=0;uint32_t imageUploadChunks=0;
    size_t meshUploadBytes = 0;
    uint32_t meshUploadChunks = 0, meshUploadsPending = 0;
    double resolveCpuMilliseconds = 0; // Whole resolve, not a GPU upload timestamp.
    RasterBatchStats rasterBatches;
    RasterMaterialStats rasterMaterials;
};
// Render-thread diagnostic leases; inspecting these must not mutate residency.
struct TerrainVirtualTextures {
    std::shared_ptr<const GpuVirtualTexture> height, material;
    glm::mat4 model{1};
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
    TerrainVirtualTextures terrainVirtualTextures() const;
    std::vector<VegetationBatchStats> vegetationStats();
  private:
    struct Cache;
    std::shared_ptr<rhi::GraphicsDevice> device_;
    std::unique_ptr<Cache> cache_, previous_;
    SceneFrame resolveCandidate(const RenderWorldSnapshot &);
    MeshUploadBudget meshUploadBudget_;
    SceneSnapshotBuilder builder_; // Single-thread compatibility capture.
    RasterInstanceBatcher batches_, auxiliaryBatches_;
};
void validateEngineBasics(std::shared_ptr<rhi::GraphicsDevice>);
void validateSceneSolarControls(std::shared_ptr<rhi::GraphicsDevice>);
std::shared_ptr<RenderScene> makeForwardDemoScene();
void runForwardScene(int argc, char **argv);
} // namespace render
