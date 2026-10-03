#pragma once
#include "renderer/rhi/GpuMesh.h"
#include "renderer/rhi/GpuMaterial.h"
namespace render {
// Portable compute subdivision keeps a common integer level on shared edges.
// The displaced indexed mesh is reused by geometry, shadow and motion passes.
class GpuSubdivision {
public:
    GpuSubdivision(std::shared_ptr<rhi::GraphicsDevice>,const std::string& directory,const std::vector<MeshVertex>&,const std::vector<uint32_t>&,const ImageRGBA8& height,uint32_t maxLevel=10);
    void update(uint32_t level,float heightScale=.05f);
    std::shared_ptr<GpuMesh> mesh()const{return mesh_;}
    uint32_t level()const{return level_;}
    std::vector<MeshVertex> readVertices();
private:
    Resources resources_;std::shared_ptr<GpuMesh> mesh_;
    rhi::ComputePipelineHandle pipeline_;rhi::BindingLayout parametersLayout_,inputsLayout_;
    rhi::BufferHandle vertices_,indices_;rhi::TextureViewHandle height_;rhi::SamplerHandle sampler_;
    uint32_t vertexCount_,indexCount_,maxLevel_,level_=0;float heightScale_=0;
};
void validateSubdivisionRhi(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
}
