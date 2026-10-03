#pragma once
#include "renderer/rhi/GpuMesh.h"
#include "renderer/rhi/GpuVirtualTexture.h"
#include <map>
namespace render {
struct FrameData;
class GpuTerrain {
public:
    GpuTerrain(std::shared_ptr<rhi::GraphicsDevice>,const std::string&,uint32_t width,uint32_t height,const std::vector<float>& heightData,uint32_t maxLeaves=2048);
    GpuTerrain(std::shared_ptr<rhi::GraphicsDevice>,const std::string&,VirtualTextureSource,uint32_t maxLeaves=2048,uint32_t virtualColumns=8);
    ~GpuTerrain();void update(const FrameData&,const glm::mat4& model);
    std::shared_ptr<GpuMesh> mesh()const{return mesh_;}
    rhi::BufferHandle leafQueue()const{return finalNodes_;}
    std::shared_ptr<GpuVirtualTexture> heightTexture()const{return heightTexture_;}
    rhi::TextureViewHandle lodView()const{return lodView_;}
    glm::mat4 surfaceView()const{return previousView_;}glm::mat4 surfaceVP()const{return previousVP_;}
    glm::vec4 surfaceScreen()const{return {float(previousWidth_),float(previousHeight_),2.f,1.f};}
    bool geometryStable()const{return geometryStable_;}
    uint32_t leafCapacity()const{return capacity_;}
    uint32_t width()const{return width_;}uint32_t height()const{return height_;}
    std::vector<uint32_t> readNodes();
    rhi::DrawIndexedIndirectArguments readArguments();
    std::vector<MeshVertex> readVertices(uint32_t count);
    std::vector<uint32_t> readIndices(uint32_t count);
private:
    struct Kernel{rhi::ComputePipelineHandle pipeline;std::vector<rhi::BindingLayout> layouts;};
    std::shared_ptr<GpuMesh> mesh_;Resources resources_;std::map<std::string,Kernel> kernels_;
    std::shared_ptr<GpuVirtualTexture> heightTexture_;uint32_t capacity_;
    rhi::BufferHandle descriptors_,finalNodes_,bounds_;
    glm::mat4 previousVP_{1},previousView_{1},previousModel_{1};uint32_t previousWidth_=0,previousHeight_=0;uint64_t previousHeightVersion_=0;bool updated_=false,geometryStable_=false;rhi::TextureHandle lod_;rhi::TextureViewHandle lodView_;uint32_t width_,height_;
};
void validateTerrainRhi(std::shared_ptr<rhi::GraphicsDevice>,const std::string&);
}
