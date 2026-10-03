#pragma once
#include "renderer/rhi/ForwardPbrRenderer.h"
class RenderScene;
namespace render {
struct SceneFrame { FrameData frame;std::vector<DrawPacket> packets;float exposure = 1; };
class SceneAdapter {
public:
    explicit SceneAdapter(std::shared_ptr<rhi::GraphicsDevice>);
    ~SceneAdapter();
    SceneFrame collect(const std::shared_ptr<RenderScene>&,float timeOverride=-1);
    void invalidateAssets(); // Call after changing cached CPU geometry/images.
private:
    struct Cache;
    std::shared_ptr<rhi::GraphicsDevice> device_;
    std::unique_ptr<Cache> cache_;
};
std::shared_ptr<RenderScene> makeForwardDemoScene();
void runForwardScene(int argc, char** argv);
}
