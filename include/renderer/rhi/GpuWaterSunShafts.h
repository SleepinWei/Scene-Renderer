#pragma once
#include "renderer/rhi/OceanSurface.h"
namespace render {
// Refracted solar flux on eight horizontal water-volume slices. No seabed
// receiver or CPU readback is needed for this field.
class GpuWaterSunShafts {
public:
    GpuWaterSunShafts(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
    void record(Resources&,rhi::CommandList&,const OceanSurfaceSettings&,glm::vec3 camera,glm::vec3 towardSun,
                const std::array<rhi::TextureViewHandle,6>& fft,rhi::TextureViewHandle shore,glm::vec4 shorePatch,rhi::TextureViewHandle mask);
    rhi::TextureViewHandle view()const{return outputView_;}
    glm::vec4 patch()const{return patch_;}
    glm::vec4 projection()const{return projection_;}
    std::vector<float> read()const{return resources_.device->readTextureFloat(output_);}
private:
    struct alignas(16) Parameters {glm::mat4 coastInverse,coastModel;glm::vec4 coastPatch,coastPreviousPatch,coastFeatures,coastBedInfo,shaftPatch,waves,sun,shaftProjection,shaftSettings;glm::ivec4 grid;};
    Resources resources_;rhi::ComputePipelineHandle rays_,filter_;rhi::PipelineHandle project_;
    std::vector<rhi::BindingLayout> rayLayouts_,filterLayouts_;rhi::BindingLayout projectionLayout_;
    rhi::TextureHandle output_;rhi::TextureViewHandle source_,direction_,accumulation_,outputView_;
    rhi::BufferHandle vertices_;rhi::SamplerHandle repeat_,clamp_,nearest_;
    glm::vec4 patch_{0},projection_{0};
};
}
