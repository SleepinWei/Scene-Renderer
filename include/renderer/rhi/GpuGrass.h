#pragma once
#include "renderer/rhi/GpuTerrain.h"
namespace render {
class GpuGrass {
public:
    GpuGrass(std::shared_ptr<rhi::GraphicsDevice>,const std::string&,std::shared_ptr<GpuTerrain>,const glm::mat4& terrainModel,uint32_t capacity=524288);
    void update(const glm::mat4&,float seconds);
    std::shared_ptr<GpuMesh> mesh()const{return mesh_;}
    rhi::DrawIndexedIndirectArguments readArguments();
    std::vector<glm::mat4> readPoses(uint32_t count);
private:
    std::shared_ptr<GpuTerrain> terrain_;std::shared_ptr<GpuMesh> mesh_;Resources resources_;
    rhi::ComputePipelineHandle pipeline_;std::vector<rhi::BindingLayout> layouts_;
};
void validateGrassRhi(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
}
