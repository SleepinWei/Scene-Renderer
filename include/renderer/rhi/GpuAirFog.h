#pragma once
#include "renderer/rhi/Resources.h"
#include "renderer/rhi/GpuAtmosphere.h"
namespace render {
struct FrameData;
struct alignas(16) AerialParameters {
    AtmosphereSettings atmosphere;
    glm::vec4 sunMultiple,solar,domain,visibility; // sun index, volumetric shadows enabled, reserved
};
AerialParameters aerialParameters(const FrameData&,const SunState&);
class GpuAirFog {
public:
    GpuAirFog(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
    ~GpuAirFog();
    void record(Resources&,rhi::CommandList&,const FrameData&,rhi::TextureHandle hdr,rhi::TextureViewHandle hdrView,rhi::TextureViewHandle depth,rhi::TextureViewHandle motion,const SunState&,rhi::TextureViewHandle transmittance,rhi::TextureViewHandle multiple,rhi::BufferHandle shadows,rhi::TextureViewHandle shadowAtlas);
    std::vector<float> readAerial(bool transmission=false) const;
private:
    struct Targets;std::unique_ptr<Targets> targets_;
    Resources resources_;rhi::PipelineHandle pipeline_;rhi::ComputePipelineHandle aerialPipeline_;
    rhi::BindingLayout layout_;std::vector<rhi::BindingLayout> aerialLayouts_;
    rhi::SamplerHandle sampler_,linear_;rhi::BufferHandle quad_;
};
void validateWeatherLighting(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
}
