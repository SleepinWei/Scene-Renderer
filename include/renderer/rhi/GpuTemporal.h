#pragma once
#include "renderer/rhi/Resources.h"
#include <glm/glm.hpp>
namespace render {
struct FrameData;
class GpuTemporal {
public:
    GpuTemporal(std::shared_ptr<rhi::GraphicsDevice>,const std::string& directory);
    ~GpuTemporal();void resize(uint32_t,uint32_t);void reset(){valid_=false;samples_=0;}
    rhi::TextureViewHandle record(Resources&,rhi::CommandList&,const FrameData&,rhi::TextureViewHandle color,rhi::TextureViewHandle depth,rhi::TextureViewHandle motion);
    void commit(const FrameData&);bool valid()const{return valid_;}uint32_t samples()const{return samples_;}
    glm::mat4 previousVP()const{return previousVP_;}glm::mat4 previousView()const{return previousView_;}
    std::vector<float> read();static glm::vec2 jitter(uint32_t);
private:
    Resources resources_;struct Targets;std::unique_ptr<Targets> targets_;
    rhi::ComputePipelineHandle pipeline_;rhi::SamplerHandle sampler_;std::vector<rhi::BindingLayout> layouts_;
    glm::mat4 previousVP_{1},previousView_{1};uint32_t index_=0,samples_=0;bool valid_=false;
};
}
