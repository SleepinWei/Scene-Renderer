#pragma once
#include "renderer/rhi/Resources.h"
#include <glm/glm.hpp>
namespace render {
struct MeshVertex { glm::vec3 position, normal;glm::vec2 uv; };
class GpuMesh {
public:
    GpuMesh(std::shared_ptr<rhi::GraphicsDevice>, const std::vector<MeshVertex>&, const std::vector<uint32_t>&);
    static std::shared_ptr<GpuMesh> beginUpload(std::shared_ptr<rhi::GraphicsDevice>, uint32_t vertices, uint32_t indices);
    // Ordered ranges may be replayed after publication rollback; holes are rejected.
    void uploadVertices(uint32_t offset, const MeshVertex*, uint32_t count);
    void uploadIndices(uint32_t offset, const uint32_t*, uint32_t count);
    bool ready() const { return ready_; }
    GpuMesh(std::shared_ptr<rhi::GraphicsDevice>,uint32_t vertexCapacity,uint32_t indexCapacity,glm::vec3 boundsMin,glm::vec3 boundsMax);
    GpuMesh(std::shared_ptr<rhi::GraphicsDevice>,const std::vector<MeshVertex>&,const std::vector<uint32_t>&,uint32_t instanceCapacity,glm::vec3 minimum,glm::vec3 maximum);
    rhi::BufferHandle instances()const{return instances_;}
    uint32_t instanceCapacity()const{return instanceCapacity_;}
    rhi::BufferHandle vertexBuffer() const{return vertices_;}
    rhi::BufferHandle indexBuffer() const{return indices_;}
    rhi::BufferHandle indirectBuffer() const{return indirect_;}
    void draw(rhi::CommandList&) const;
    const rhi::GraphicsDevice* owner() const { return resources_.device.get(); }
    void setBounds(glm::vec3 minimum,glm::vec3 maximum);
    glm::vec3 boundsMin() const { return boundsMin_; }
    glm::vec3 boundsMax() const { return boundsMax_; }
    uint32_t indexCount() const { return count_; }
    static std::vector<rhi::VertexAttribute> attributes();
private:
    struct UploadTag {};
    GpuMesh(std::shared_ptr<rhi::GraphicsDevice>, uint32_t vertices, uint32_t indices, UploadTag);
    void finishRange();
    bool ready_ = true;
    uint32_t uploadVertexCapacity_ = 0, uploadIndexCapacity_ = 0, uploadedVertices_ = 0, uploadedIndices_ = 0;
    Resources resources_;
    rhi::BufferHandle vertices_, indices_,indirect_,instances_;
    uint32_t instanceCapacity_=0;
    uint32_t count_ = 0;
    glm::vec3 boundsMin_,boundsMax_;
};
}
