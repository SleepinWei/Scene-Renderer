#pragma once
#include "renderer/rhi/GpuMesh.h"
#include <map>
namespace render {
struct FrameData;
class GpuTerrain {
public:
    GpuTerrain(std::shared_ptr<rhi::GraphicsDevice>,const std::string&,uint32_t width,uint32_t height,const std::vector<float>& heightData);
    ~GpuTerrain();void update(const FrameData&,const glm::mat4& model);
    std::shared_ptr<GpuMesh> mesh()const{return mesh_;}
    rhi::BufferHandle leafQueue()const{return finalNodes_;}
    rhi::BufferHandle heightBuffer()const{return heights_;}
    uint32_t width()const{return width_;}uint32_t height()const{return height_;}
    std::vector<uint32_t> readNodes();
    rhi::DrawIndexedIndirectArguments readArguments();
    std::vector<MeshVertex> readVertices(uint32_t count);
    std::vector<uint32_t> readIndices(uint32_t count);
private:
    struct Kernel{rhi::ComputePipelineHandle pipeline;std::vector<rhi::BindingLayout> layouts;};
    std::shared_ptr<GpuMesh> mesh_;Resources resources_;std::map<std::string,Kernel> kernels_;
    rhi::BufferHandle heights_,descriptors_,finalNodes_;rhi::TextureHandle lod_;rhi::TextureViewHandle lodView_;uint32_t width_,height_;
};
void validateTerrainRhi(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
}
