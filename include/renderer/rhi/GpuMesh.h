#pragma once
#include "renderer/rhi/Resources.h"
#include <glm/glm.hpp>
namespace render {
struct MeshVertex { glm::vec3 position, normal;glm::vec2 uv; };
class GpuMesh {
public:
    GpuMesh(std::shared_ptr<rhi::GraphicsDevice>, const std::vector<MeshVertex>&, const std::vector<uint32_t>&);
    static std::shared_ptr<GpuMesh> beginUpload(std::shared_ptr<rhi::GraphicsDevice>, uint32_t vertices, uint32_t indices, bool materialTangents = false);
    // Immutable ordinary instance batch; shares base geometry and owns poses.
    static std::shared_ptr<GpuMesh> withInstances(std::shared_ptr<GpuMesh>,const std::vector<glm::mat4>&);
    // Ordered ranges may be replayed after publication rollback; holes are rejected.
    void uploadVertices(uint32_t offset, const MeshVertex*, uint32_t count);
    void uploadIndices(uint32_t offset, const uint32_t*, uint32_t count);
    bool ready() const { return ready_; }
    GpuMesh(std::shared_ptr<rhi::GraphicsDevice>,uint32_t vertexCapacity,uint32_t indexCapacity,glm::vec3 boundsMin,glm::vec3 boundsMax);
    GpuMesh(std::shared_ptr<rhi::GraphicsDevice>,const std::vector<MeshVertex>&,const std::vector<uint32_t>&,uint32_t instanceCapacity,glm::vec3 minimum,glm::vec3 maximum);
    static std::shared_ptr<GpuMesh> withGeneratedPoses(std::shared_ptr<GpuMesh>,const std::vector<MeshVertex>&,const std::vector<uint32_t>&);
    void syncGeneratedPoses();
    glm::vec3 instanceBoundsMin()const{return instanceBoundsMin_;}
    glm::vec3 instanceBoundsMax()const{return instanceBoundsMax_;}
    void enableInstanceHistory();
    void enableVertexHistory();
    rhi::BufferHandle previousVertices()const{return previousVertices_;}
    bool vertexHistoryReliable()const{return vertexHistoryReliable_;}
    void setVertexHistoryReliable(bool value){vertexHistoryReliable_=value;}
    void markGeometryChanged(){++geometryRevision_;}
    void padWindBounds(float);
    rhi::BufferHandle previousInstances()const{return previousInstances_;}
    rhi::BufferHandle instances()const{return instances_;}
    uint32_t instanceCapacity()const{return instanceCapacity_;}
    void uploadMaterialTangents(uint32_t offset,const glm::vec4*,uint32_t count);
    void setMaterialTangents(const std::vector<glm::vec4>&); // Local U and handedness, per vertex.
    rhi::BufferHandle materialTangents() const { return tangents_; }
    size_t materialTangentBytes() const { return tangentBytes_; }
    rhi::BufferHandle vertexBuffer() const{return vertices_;}
    rhi::BufferHandle indexBuffer() const{return indices_;}
    rhi::BufferHandle indirectBuffer() const{return indirect_;}
    void draw(rhi::CommandList&) const;
    const rhi::GraphicsDevice* owner() const { return resources_.device.get(); }
    void setBounds(glm::vec3 minimum,glm::vec3 maximum);
    glm::vec3 boundsMin() const { return boundsMin_; }
    glm::vec3 boundsMax() const { return boundsMax_; }
    uint32_t indexCount() const { return count_; }
    uint32_t vertexCount() const { return vertexCount_; }
    uint64_t geometryRevision() const {return geometryRevision_;}
    void draw(rhi::CommandList&,rhi::BufferHandle visibleIndices,rhi::BufferHandle visibleArguments,size_t argumentOffset=0) const;
    static std::vector<rhi::VertexAttribute> attributes();
private:
    struct UploadTag {};
    GpuMesh(std::shared_ptr<GpuMesh>,const std::vector<glm::mat4>&);
    std::shared_ptr<GpuMesh> base_; // Retire pose/argument resources before base geometry.
    GpuMesh(std::shared_ptr<rhi::GraphicsDevice>, uint32_t vertices, uint32_t indices, bool materialTangents, UploadTag);
    void finishRange();
    bool ready_ = true;
    uint32_t uploadVertexCapacity_ = 0, uploadIndexCapacity_ = 0, uploadedVertices_ = 0, uploadedIndices_ = 0;
    void initializeMaterialTangents();
    uint32_t vertexCount_=0,uploadedTangents_=0;
    uint64_t geometryRevision_=1;
    bool uploadingTangents_=false;
    size_t tangentBytes_=32;
    Resources resources_;
    rhi::BufferHandle vertices_, indices_,indirect_,instances_,tangents_,previousInstances_,previousVertices_;
    bool vertexHistoryReliable_=false;
    uint32_t instanceCapacity_=0;
    uint32_t count_ = 0;
    glm::vec3 boundsMin_,boundsMax_,instanceBoundsMin_{0},instanceBoundsMax_{0};
};
}
