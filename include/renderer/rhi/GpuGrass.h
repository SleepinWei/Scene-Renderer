#pragma once
#include "renderer/rhi/GpuTerrain.h"
#include "component/VegetationSettings.h"
#include "renderer/rhi/GpuImageCache.h"
namespace render {
struct MeshPayload;
class GpuGrass {
public:
    GpuGrass(std::shared_ptr<rhi::GraphicsDevice>,const std::string&,std::shared_ptr<GpuTerrain>,const glm::mat4& terrainModel,uint32_t capacity=65536, VegetationSettings settings={}, std::shared_ptr<const ImageRGBA8> waterMask={}, std::shared_ptr<const ImageRGBA8> exclusionMask={},std::shared_ptr<const MeshPayload> geometry={});
    void update(const glm::mat4&,float seconds);
    void update(const glm::mat4&,float seconds,const VegetationSettings&);
    bool historyReliable()const{return historyReliable_;}
    void sharePopulationBudget(std::shared_ptr<GpuGrass> owner);
    std::shared_ptr<GpuMesh> mesh()const{return mesh_;}
    rhi::DrawIndexedIndirectArguments readArguments();
    std::vector<glm::mat4> readPoses(uint32_t count);
private:
    std::shared_ptr<GpuTerrain> terrain_;std::shared_ptr<GpuMesh> mesh_;Resources resources_;
    bool history_=false,historyReliable_=false;float previousSeconds_=0,previousHeight_=0,previousWidth_=0;uint64_t previousHeightVersion_=0;glm::mat4 previousModel_{1};glm::vec4 previousWind_{0};
    glm::vec2 previousCamera_{0};float previousFarWidth_=1,previousDenseRadius_=0,previousSparseRadius_=1,previousFadeStart_=0,previousDistance_=1;
    float previousSpacing_=0;uint32_t previousSeed_=0,previousSamples_=0;bool previousStableLattice_=false;
    std::shared_ptr<GpuGrass> budgetOwner_;
    rhi::BufferHandle candidates_;rhi::ComputePipelineHandle countPipeline_;std::vector<rhi::BindingLayout> countLayouts_;
    VegetationSettings settings_;bool hasMask_=false,treeGeometry_=false;std::shared_ptr<GpuImageCache> imageCache_;std::shared_ptr<GpuImage> mask_,exclusion_;rhi::SamplerHandle maskSampler_;
    rhi::ComputePipelineHandle pipeline_;std::vector<rhi::BindingLayout> layouts_;
};
void validateGrassRhi(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
}
