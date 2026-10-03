#pragma once
#include "renderer/rhi/GpuTerrain.h"
#include "component/VegetationSettings.h"
#include "renderer/rhi/GpuImageCache.h"
namespace render {
class GpuGrass {
public:
    GpuGrass(std::shared_ptr<rhi::GraphicsDevice>,const std::string&,std::shared_ptr<GpuTerrain>,const glm::mat4& terrainModel,uint32_t capacity=65536, VegetationSettings settings={}, std::shared_ptr<const ImageRGBA8> waterMask={}, std::shared_ptr<const ImageRGBA8> exclusionMask={});
    void update(const glm::mat4&,float seconds);
    void update(const glm::mat4&,float seconds,const VegetationSettings&);
    std::shared_ptr<GpuMesh> mesh()const{return mesh_;}
    rhi::DrawIndexedIndirectArguments readArguments();
    std::vector<glm::mat4> readPoses(uint32_t count);
private:
    std::shared_ptr<GpuTerrain> terrain_;std::shared_ptr<GpuMesh> mesh_;Resources resources_;
    rhi::BufferHandle candidates_;rhi::ComputePipelineHandle countPipeline_;std::vector<rhi::BindingLayout> countLayouts_;
    VegetationSettings settings_;bool hasMask_=false;std::shared_ptr<GpuImageCache> imageCache_;std::shared_ptr<GpuImage> mask_,exclusion_;rhi::SamplerHandle maskSampler_;
    rhi::ComputePipelineHandle pipeline_;std::vector<rhi::BindingLayout> layouts_;
};
void validateGrassRhi(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
}
