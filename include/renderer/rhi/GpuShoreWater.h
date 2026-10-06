#pragma once
#include "renderer/rhi/OceanSurface.h"
#include <map>

namespace render {
// Conservative, hydrostatically reconstructed shallow-water solver. Only foam
// is semi-Lagrangian; mass and momentum use finite-volume face fluxes.
class GpuShoreWater {
public:
    GpuShoreWater(std::shared_ptr<rhi::GraphicsDevice>,const std::string&,const OceanSurfaceSettings&,rhi::TextureViewHandle bed);
    bool compatible(const OceanSurfaceSettings&) const;
    void simulate(float seconds,const OceanSurfaceSettings&,glm::vec3 camera,rhi::TextureViewHandle fft,rhi::TextureViewHandle previousFFT);
    rhi::TextureViewHandle state() const {return stateViews_[current_];}
    rhi::TextureViewHandle previous() const {return previousView_;}
    rhi::TextureViewHandle foam() const {return foamViews_[current_];}
    glm::vec4 patch() const {return patch_;}
    glm::vec4 previousPatch() const {return previousPatch_;}
    std::vector<float> read(bool foam=false) const;
    uint32_t lastSteps() const {return lastSteps_;}
private:
    struct alignas(16) Parameters {glm::mat4 inverseModel,model;glm::vec4 patch,oldPatch,clock,waves,flow,foam,swell;glm::ivec4 mode;};
    void dispatch(Resources&,rhi::CommandList&,const Parameters&,uint32_t input,uint32_t output,rhi::TextureViewHandle fft,rhi::TextureViewHandle oldFFT);
    Resources resources_;OceanSurfaceSettings initial_;rhi::TextureViewHandle bed_;
    rhi::ComputePipelineHandle pipeline_;std::vector<rhi::BindingLayout> layouts_;rhi::SamplerHandle sampler_,repeat_;
    std::array<rhi::TextureHandle,2> states_,foams_;std::array<rhi::TextureViewHandle,2> stateViews_,foamViews_;
    rhi::TextureHandle previous_;rhi::TextureViewHandle previousView_;
    glm::vec4 patch_{0},previousPatch_{0};uint32_t current_=0,lastSteps_=0;
    bool initialized_=false;double time_=0,previousTarget_=0;
};
}
