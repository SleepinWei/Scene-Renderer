#pragma once
#include "renderer/rhi/Resources.h"
#include <glm/glm.hpp>
#include <map>
namespace render {
struct alignas(16) AtmosphereSettings {
    glm::vec4 radii{20,.005f,6460,6360};
    glm::vec4 densities{8,1.2f,25,.8f};
    glm::vec4 rayleigh{.005802f,.013558f,.0331f,0};
    glm::vec4 mie{.003996f,.003996f,.003996f,0};
    glm::vec4 extinction{.008396f,.008396f,.008396f,0};
    glm::vec4 absorption{.00065f,.001881f,.000085f,15};
};
class GpuAtmosphere {
public:
    GpuAtmosphere(std::shared_ptr<rhi::GraphicsDevice>,const std::string& directory);
    void update(const AtmosphereSettings&,float sunAngle);
    rhi::TextureViewHandle sky() const{return views_[1];}
    rhi::TextureViewHandle irradiance() const{return views_[3];}
    std::vector<float> read(uint32_t i){return resources_.device->readTextureFloat(textures_.at(i));}
private:
    struct Kernel{rhi::ComputePipelineHandle pipeline;std::vector<rhi::BindingLayout> layouts;std::array<uint32_t,3> threads;};
    Resources resources_;rhi::BufferHandle parameters_;rhi::SamplerHandle sampler_;
    std::array<rhi::TextureHandle,4> textures_;std::array<rhi::TextureViewHandle,4> views_;
    std::map<std::string,Kernel> kernels_;AtmosphereSettings previous_;float previousSun_=0;bool initialized_=false;
};
void validateAtmosphereRhi(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
}
