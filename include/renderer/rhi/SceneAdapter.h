#pragma once
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "renderer/rhi/SceneSnapshot.h"
class RenderScene;
namespace render {
struct SceneFrame { FrameData frame;std::vector<DrawPacket> packets;float exposure = 1;size_t uploadBytes=0;uint32_t assetUploads=0,assetsPending=0; };
class SceneAdapter {
public:
    explicit SceneAdapter(std::shared_ptr<rhi::GraphicsDevice>);
    ~SceneAdapter();
    SceneFrame collect(const std::shared_ptr<RenderScene>&,float timeOverride=-1);
    SceneFrame resolve(const RenderWorldSnapshot&); // Render-thread only.
    void invalidateAssets(); // Call after changing cached CPU geometry/images.
private:
    struct Cache;
    std::shared_ptr<rhi::GraphicsDevice> device_;
    std::unique_ptr<Cache> cache_;
    SceneSnapshotBuilder builder_; // Single-thread compatibility capture.
};
void validateEngineBasics(std::shared_ptr<rhi::GraphicsDevice>);
void validateSceneSolarControls(std::shared_ptr<rhi::GraphicsDevice>);
std::shared_ptr<RenderScene> makeForwardDemoScene();
void runForwardScene(int argc, char** argv);
}
