#pragma once
#include "renderer/rhi/Resources.h"
#include "engine/WaveParticles.h"
namespace render {
class GpuWaveParticles {
public:
    GpuWaveParticles(std::shared_ptr<rhi::GraphicsDevice>,const std::string&,uint32_t resolution);
    void simulate(double,const WaveParticleSettings&,glm::vec4 patch,rhi::TextureViewHandle shore,bool hasShore);
    uint32_t resolution()const{return size_;}
    rhi::TextureViewHandle state()const{return view_;}
    rhi::TextureViewHandle previous()const{return previousView_;}
    glm::vec4 patch()const{return patch_;}
    glm::vec4 previousPatch()const{return previousPatch_;}
    const engine::WaveParticles& simulation()const{return simulation_;}
    uint32_t binReferences()const{return references_;}
    std::vector<float> read(bool previous=false)const{return resources_.device->readTextureFloat(previous?previous_:texture_);}
private:
    Resources resources_;engine::WaveParticles simulation_;
    uint32_t size_,references_=0;bool initialized_=false;double time_=-1,tick_=-1;uint32_t particleCount_=0;
    rhi::TextureHandle texture_,previous_;rhi::TextureViewHandle view_,previousView_;
    rhi::SamplerHandle sampler_;rhi::ComputePipelineHandle pipeline_;std::vector<rhi::BindingLayout> layouts_;
    glm::vec4 patch_{0},previousPatch_{0};
};
void validateWaveParticles(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
}
