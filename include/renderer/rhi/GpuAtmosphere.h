#pragma once
#include "renderer/rhi/Resources.h"
#include <glm/glm.hpp>
#include <map>
namespace render {
// Coefficients and distances use kilometres; solar irradiance is linear RGB HDR.
struct alignas(16) AtmosphereSettings {
    glm::vec4 radii{20,.005f,6460,6360};
    glm::vec4 densities{8,1.2f,25,.8f};
    glm::vec4 rayleigh{.005802f,.013558f,.0331f,0};
    glm::vec4 mie{.003996f,.003996f,.003996f,0};
    glm::vec4 extinction{.0044f,.0044f,.0044f,0}; // Total Mie extinction includes scattering.
    glm::vec4 absorption{.00065f,.001881f,.000085f,15};
};
struct SunState {
    glm::vec3 direction{0,.17364818f,-.98480775f}; // Towards the sun, world Y up.
    float observerHeightKm=.002f;
    glm::vec3 irradiance{20}; // At the top of the atmosphere.
    float multipleScattering=1,groundAlbedo=.2f;
};
// Independent double-precision integration for direct light and analytic tests.
glm::vec3 solarTransmittance(const AtmosphereSettings&,const SunState&,bool includeDiskVisibility=true);
float atmosphereHorizon(const AtmosphereSettings&,float observerHeightKm);
class GpuAtmosphere {
public:
    GpuAtmosphere(std::shared_ptr<rhi::GraphicsDevice>,const std::string& directory);
    void update(const AtmosphereSettings&,const SunState&);
    void update(const AtmosphereSettings&,float sunAngle);
    rhi::TextureViewHandle sky() const{return views_[1];}
    rhi::TextureViewHandle irradiance() const{return views_[3];}
    std::vector<float> read(uint32_t i){return resources_.device->readTextureFloat(textures_.at(i));}
    struct UpdateCounts {uint64_t transmittance=0,multiple=0,sky=0;};
    UpdateCounts updateCounts() const{return counts_;}
private:
    struct Kernel{rhi::ComputePipelineHandle pipeline;std::vector<rhi::BindingLayout> layouts;std::array<uint32_t,3> threads;};
    Resources resources_;rhi::BufferHandle parameters_;rhi::SamplerHandle sampler_,skySampler_;
    std::array<rhi::TextureHandle,4> textures_;std::array<rhi::TextureViewHandle,4> views_;
    std::map<std::string,Kernel> kernels_;AtmosphereSettings previous_;SunState previousSun_;bool initialized_=false;UpdateCounts counts_;
};
void validateAtmosphereRhi(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
}
