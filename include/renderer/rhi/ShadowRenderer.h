#pragma once
#include "renderer/rhi/ForwardPbrRenderer.h"
namespace render {
constexpr uint32_t MaxShadowTiles=180;
struct alignas(16) ShadowParameters {
    glm::mat4 cameraView{1};
    std::array<glm::mat4,MaxShadowTiles> matrices;
    std::array<glm::vec4,MaxShadowTiles> rects;
    std::array<glm::ivec4,30> lights{}; // base tile, count, type, unused.
    std::array<glm::vec4,30> splits{};
    glm::vec4 settings{.001f,1,0,128}; // bias, enabled, RSM intensity, tile pixels.
    glm::mat4 rsmMatrix{1};
    glm::vec4 rsmRect{0};
    glm::vec4 rsmSettings{.3f,128,.1f,0}; // UV radius, samples, minimum distance, indirect only.
    std::array<glm::vec4,30> lightDepth; // near, far, orthographic flag, tan(fov/2).
    glm::vec4 filter{1,.00465f,.05f,24}; // PCSS enabled, sun angular radius, local emitter radius, radius cap in texels.
    glm::vec4 cascades{.1f,300,.1f,.1f}; // camera near, shadow distance, cascade overlap, far fade fraction.
};
class ShadowRenderer {
public:
    ShadowRenderer(std::shared_ptr<rhi::GraphicsDevice>,const std::string& directory,uint32_t tilePixels=128,rhi::TextureViewHandle skyIrradiance={});
    void render(const FrameData&,const std::vector<DrawPacket>&);
    rhi::TextureViewHandle view() const {return view_;}
    rhi::TextureViewHandle rsmView(uint32_t i) const {return rsmViews_.at(i);}
    rhi::TextureViewHandle rsmSourceView(uint32_t i) const {return sourceRsmViews_.at(i);}
    std::vector<float> readRsmSource(uint32_t i){return resources_.device->readTextureFloat(sourceRsm_.at(i));}
    rhi::BufferHandle parameters() const {return parameters_;}
    const ShadowParameters& data() const {return data_;}
    std::vector<float> readDepth() {return resources_.device->readTextureFloat(atlas_);}
    std::vector<float> readRsm(uint32_t i){return resources_.device->readTextureFloat(rsm_.at(i));}
    uint32_t rsmExtent()const{return sourceExtent_;}
    uint32_t extent() const {return extent_;}
    static rhi::BindingLayout objectLayout();
private:
    Resources resources_;
    rhi::TextureHandle atlas_;
    rhi::TextureViewHandle view_;
    std::array<rhi::TextureHandle,3> rsm_;
    std::array<rhi::TextureViewHandle,3> rsmViews_;
    std::array<rhi::TextureHandle,3> sourceRsm_;std::array<rhi::TextureViewHandle,3> sourceRsmViews_;
    rhi::TextureHandle sourceDepth_;rhi::TextureViewHandle sourceDepthView_;uint32_t sourceExtent_=1024;
    rhi::PipelineHandle pipeline_,instanced_;
    rhi::BufferHandle parameters_;
    ShadowParameters data_;
    uint32_t tilePixels_,extent_;
    rhi::TextureViewHandle skyIrradiance_;rhi::SamplerHandle skySampler_;
};
}
