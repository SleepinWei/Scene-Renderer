#pragma once
#include "renderer/rhi/OceanSurface.h"
namespace render {
// Forward solar rays through the live wave surface, projected as an additive
// mesh onto cascaded, sun-aligned receivers (terrain and opaque meshes).
class GpuWaterCaustics {
public:
    GpuWaterCaustics(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
    void record(Resources&,rhi::CommandList&,const OceanSurfaceSettings&,glm::vec3 camera,glm::vec3 towardSun,
                const std::array<rhi::TextureViewHandle,6>& fft,rhi::TextureViewHandle bed,rhi::TextureViewHandle shore,glm::vec4 shorePatch,rhi::TextureViewHandle mask,const std::vector<DrawPacket>& packets={});
    rhi::TextureViewHandle view()const{return outputView_;}
    glm::vec4 patch()const{return patches_[0];}
    const std::array<glm::vec4,3>& patches()const{return patches_;}
    glm::vec4 projection()const{return projection_;}
    uint32_t cascadeCount()const{return cascadeCount_;}
    std::vector<float> read(uint32_t cascade=0)const;
private:
    struct alignas(16) Parameters {glm::mat4 coastInverse,coastModel;glm::vec4 coastPatch,coastPreviousPatch,coastFeatures,coastBedInfo,photonPatch,waves,sun,extinction,controls,receiverProjection,sourceSettings;glm::ivec4 grid;};
    Resources resources_;rhi::ComputePipelineHandle rays_,normalize_;rhi::PipelineHandle project_,receiver_,receiverInstanced_;
    std::vector<rhi::BindingLayout> rayLayouts_,normalizeLayouts_;rhi::BindingLayout projectionLayout_,receiverLayout_;
    rhi::TextureHandle output_;rhi::TextureViewHandle hit_,flux_,accumulated_,outputView_,receiverPosition_,receiverNormal_,receiverDepth_;
    std::array<rhi::BufferHandle,2> vertices_;rhi::SamplerHandle repeat_,clamp_,nearest_;
    std::array<glm::vec4,3> patches_{};glm::vec4 projection_{0};uint32_t cascadeCount_=0;
};
}
