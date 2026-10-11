#include "renderer/rhi/GpuMesh.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
namespace render {
void GpuMesh::enableVertexHistory(){if(!resources_.device->computeLimits().supported)throw std::invalid_argument("Vertex wind history requires compute");if(!previousVertices_)previousVertices_=resources_.buffer({size_t(vertexCount_)*sizeof(MeshVertex),rhi::BufferUsage::Storage|rhi::BufferUsage::CopyDestination|rhi::BufferUsage::CopySource,"Previous deformed vertices"});}
void GpuMesh::padWindBounds(float padding){boundsMin_-=glm::vec3(padding);boundsMax_+=glm::vec3(padding);instanceBoundsMin_-=glm::vec3(padding);instanceBoundsMax_+=glm::vec3(padding);}
void GpuMesh::enableInstanceHistory(){if(!instances_||!instanceCapacity_)throw std::invalid_argument("Instance history requires poses");if(!previousInstances_)previousInstances_=resources_.buffer({size_t(instanceCapacity_)*64,rhi::BufferUsage::Storage|rhi::BufferUsage::CopySource,"Previous instance poses"});}
void GpuMesh::setBounds(glm::vec3 minimum,glm::vec3 maximum){for(int i=0;i<3;i++)if(!std::isfinite(minimum[i]) || !std::isfinite(maximum[i]) || minimum[i]>maximum[i])throw std::invalid_argument("Mesh invalid bounds");boundsMin_=minimum;boundsMax_=maximum;}

static_assert(std::is_standard_layout<MeshVertex>::value && sizeof(MeshVertex) == 32, "GPU vertex ABI changed");
static_assert(offsetof(MeshVertex, normal) == 12 && offsetof(MeshVertex, uv) == 24, "GPU attribute offsets changed");
std::vector<rhi::VertexAttribute> GpuMesh::attributes() { return {{0, rhi::VertexFormat::Float3, 0}, {1, rhi::VertexFormat::Float3, 12}, {2, rhi::VertexFormat::Float2, 24}}; }
GpuMesh::GpuMesh(std::shared_ptr<rhi::GraphicsDevice> device, const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices) : resources_(std::move(device)) {
    if (vertices.empty() || indices.empty() || indices.size() % 3 || indices.size() > uint32_t(std::numeric_limits<int32_t>::max()))
        throw std::invalid_argument("Renderer: mesh must contain indexed triangles");
    for (auto index : indices) if (index >= vertices.size()) throw std::invalid_argument("Renderer: mesh index exceeds vertex data");
    boundsMin_=boundsMax_=vertices.front().position;
    for (const auto& vertex : vertices) {
        boundsMin_=glm::min(boundsMin_,vertex.position);boundsMax_=glm::max(boundsMax_,vertex.position);
        for (int i = 0; i < 3; ++i) if (!std::isfinite(vertex.position[i]) || !std::isfinite(vertex.normal[i])) throw std::invalid_argument("Renderer: nonfinite vertex");
        if (!std::isfinite(vertex.uv.x) || !std::isfinite(vertex.uv.y) || glm::dot(vertex.normal, vertex.normal) < 1e-12f) throw std::invalid_argument("Renderer: invalid vertex normal or UV");
    }
    instanceBoundsMin_=boundsMin_;instanceBoundsMax_=boundsMax_;
    vertexCount_=uint32_t(vertices.size());initializeMaterialTangents();
    const auto storage=resources_.device->computeLimits().supported?rhi::BufferUsage::Storage:rhi::BufferUsage::None;
    vertices_ = resources_.buffer({vertices.size() * sizeof(MeshVertex), rhi::BufferUsage::Vertex | rhi::BufferUsage::CopySource | storage, "PBR vertices"}, vertices.data());
    indices_ = resources_.buffer({indices.size() * sizeof(uint32_t), rhi::BufferUsage::Index | rhi::BufferUsage::CopySource | storage, "PBR indices"}, indices.data());count_ = uint32_t(indices.size());
}
GpuMesh::GpuMesh(std::shared_ptr<rhi::GraphicsDevice> device,uint32_t vertices,uint32_t indices,glm::vec3 minimum,glm::vec3 maximum):resources_(std::move(device)),boundsMin_(minimum),boundsMax_(maximum){
    using namespace rhi;if(!vertices || !indices || !resources_.device->computeLimits().supported)throw std::invalid_argument("Generated mesh requires compute and positive capacities");
    vertexCount_=vertices;initializeMaterialTangents();
    vertices_=resources_.buffer({size_t(vertices)*sizeof(MeshVertex),BufferUsage::Vertex|BufferUsage::Storage|BufferUsage::CopySource|BufferUsage::CopyDestination,"Generated mesh vertices",BufferInitialization::Uninitialized});indices_=resources_.buffer({size_t(indices)*4,BufferUsage::Index|BufferUsage::Storage|BufferUsage::CopySource|BufferUsage::CopyDestination,"Generated mesh indices",BufferInitialization::Uninitialized});
    const DrawIndexedIndirectArguments args{0,1,0,0,0};indirect_=resources_.buffer({sizeof(args),BufferUsage::Storage|BufferUsage::Indirect|BufferUsage::CopySource|BufferUsage::CopyDestination,"Generated indexed arguments"},&args);count_=indices;
}
GpuMesh::GpuMesh(std::shared_ptr<rhi::GraphicsDevice> d,const std::vector<MeshVertex>& v,const std::vector<uint32_t>& i,uint32_t cap,glm::vec3 minimum,glm::vec3 maximum):GpuMesh(d,v,i){
    using namespace rhi;if(!cap || !d->computeLimits().supported)throw std::invalid_argument("Instanced mesh requires compute storage");instanceCapacity_=cap;boundsMin_=minimum;boundsMax_=maximum;instances_=resources_.buffer({size_t(cap)*64,BufferUsage::Storage|BufferUsage::CopySource,"GPU instance poses"});const DrawIndexedIndirectArguments args{count_,0,0,0,0};indirect_=resources_.buffer({sizeof(args),BufferUsage::Storage|BufferUsage::Indirect|BufferUsage::CopySource|BufferUsage::CopyDestination,"GPU indexed instance arguments"},&args);
}
std::shared_ptr<GpuMesh> GpuMesh::beginUpload(std::shared_ptr<rhi::GraphicsDevice> device,
                                           uint32_t vertices, uint32_t indices, bool frames) {
    return std::shared_ptr<GpuMesh>(new GpuMesh(std::move(device), vertices, indices, frames, UploadTag{}));
}
std::shared_ptr<GpuMesh> GpuMesh::withInstances(std::shared_ptr<GpuMesh> base,const std::vector<glm::mat4>& poses) {
    if(!base||!base->ready()||base->indirectBuffer()||poses.empty()||poses.size()>UINT32_MAX)
        throw std::invalid_argument("Ordinary instance batch requires a ready static base and poses");
    return std::shared_ptr<GpuMesh>(new GpuMesh(std::move(base),poses));
}
GpuMesh::GpuMesh(std::shared_ptr<GpuMesh> base,const std::vector<glm::mat4>& poses)
    :base_(std::move(base)),resources_(base_->resources_.device) {
    using namespace rhi;
    if(!resources_.device->computeLimits().supported)throw std::invalid_argument("Instance batches require vertex storage");
    boundsMin_=glm::vec3(std::numeric_limits<float>::max());boundsMax_=-boundsMin_;
    for(const auto& pose:poses){
        for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(!std::isfinite(pose[c][r]))throw std::invalid_argument("Nonfinite instance transform");
        const float det=glm::determinant(pose);
        if(!std::isfinite(det)||std::abs(det)<1e-12f||pose[0].w!=0||pose[1].w!=0||pose[2].w!=0||pose[3].w!=1)
            throw std::invalid_argument("Instance batch requires invertible affine transforms");
        const auto inverse=glm::inverse(pose);
        for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(!std::isfinite(inverse[c][r]))throw std::invalid_argument("Nonfinite instance normal transform");
        for(int corner=0;corner<8;++corner){const auto v=pose*glm::vec4(corner&1?base_->boundsMax_.x:base_->boundsMin_.x,corner&2?base_->boundsMax_.y:base_->boundsMin_.y,corner&4?base_->boundsMax_.z:base_->boundsMin_.z,1);
            for(int c=0;c<3;++c)if(!std::isfinite(v[c]))throw std::invalid_argument("Nonfinite instance bounds");
            boundsMin_=glm::min(boundsMin_,glm::vec3(v));boundsMax_=glm::max(boundsMax_,glm::vec3(v));}
    }
    instanceBoundsMin_=base_->boundsMin_;instanceBoundsMax_=base_->boundsMax_;
    vertexCount_=base_->vertexCount_;tangents_=base_->tangents_;tangentBytes_=base_->tangentBytes_;
    vertices_=base_->vertices_;indices_=base_->indices_;count_=base_->count_;instanceCapacity_=uint32_t(poses.size());
    instances_=resources_.buffer({poses.size()*sizeof(glm::mat4),BufferUsage::Storage|BufferUsage::CopySource,"Ordinary instance poses"},poses.data());
    const DrawIndexedIndirectArguments args{count_,instanceCapacity_,0,0,0};
    indirect_=resources_.buffer({sizeof(args),BufferUsage::Storage|BufferUsage::Indirect|BufferUsage::CopySource,"Ordinary instance arguments"},&args);
}
GpuMesh::GpuMesh(std::shared_ptr<rhi::GraphicsDevice> device, uint32_t vertices, uint32_t indices, bool frames, UploadTag)
    : ready_(false), uploadVertexCapacity_(vertices), uploadIndexCapacity_(indices),
      resources_(std::move(device)) {
    using namespace rhi;
    if(!vertices || !indices || indices % 3 || indices > uint32_t(std::numeric_limits<int32_t>::max()))
        throw std::invalid_argument("Upload mesh must contain indexed triangles");
    vertexCount_=vertices;initializeMaterialTangents();
    if(frames && resources_.device->computeLimits().supported){
        uploadingTangents_=true;tangentBytes_=(size_t(vertices)+1)*16;
        tangents_=resources_.buffer({tangentBytes_,rhi::BufferUsage::Storage|rhi::BufferUsage::CopyDestination,"Streamed material tangents",rhi::BufferInitialization::Uninitialized});
        const glm::vec4 inactive(0);resources_.device->writeBuffer(tangents_,0,16,&inactive);
    }
    const auto storage=resources_.device->computeLimits().supported?BufferUsage::Storage:BufferUsage::None;
    vertices_ = resources_.buffer({size_t(vertices) * sizeof(MeshVertex),
                                  BufferUsage::Vertex | BufferUsage::CopyDestination | BufferUsage::CopySource | storage,
                                  "Streamed PBR vertices", BufferInitialization::Uninitialized});
    indices_ = resources_.buffer({size_t(indices) * 4,
                                 BufferUsage::Index | BufferUsage::CopyDestination | BufferUsage::CopySource | storage,
                                 "Streamed PBR indices", BufferInitialization::Uninitialized});
}
void GpuMesh::finishRange() {
    if(uploadedVertices_ == uploadVertexCapacity_ && uploadedIndices_ == uploadIndexCapacity_ && (!uploadingTangents_ || uploadedTangents_==vertexCount_)) {
        count_ = uploadIndexCapacity_;
        ready_ = true;
    }
}
void GpuMesh::uploadVertices(uint32_t offset, const MeshVertex* vertices, uint32_t count) {
    if(!vertices || !count || offset > uploadedVertices_ || offset > uploadVertexCapacity_ ||
       count > uploadVertexCapacity_ - offset)
        throw std::invalid_argument("Invalid or noncontiguous vertex upload range");
    auto minimum = vertices[0].position, maximum = minimum;
    for(uint32_t i = 0; i < count; ++i) {
        const auto& v = vertices[i];
        for(int c = 0; c < 3; ++c)
            if(!std::isfinite(v.position[c]) || !std::isfinite(v.normal[c]))
                throw std::invalid_argument("Nonfinite upload vertex");
        if(!std::isfinite(v.uv.x) || !std::isfinite(v.uv.y) || glm::dot(v.normal,v.normal) < 1e-12f)
            throw std::invalid_argument("Invalid upload normal or UV");
        minimum = glm::min(minimum,v.position);
        maximum = glm::max(maximum,v.position);
    }
    resources_.device->writeBuffer(vertices_,size_t(offset)*sizeof(MeshVertex),size_t(count)*sizeof(MeshVertex),vertices);
    ++geometryRevision_;
    boundsMin_ = offset ? glm::min(boundsMin_,minimum) : minimum;
    boundsMax_ = offset ? glm::max(boundsMax_,maximum) : maximum;
    uploadedVertices_ = std::max(uploadedVertices_,offset+count);
    finishRange();
}
void GpuMesh::uploadIndices(uint32_t offset, const uint32_t* indices, uint32_t count) {
    if(!indices || !count || offset > uploadedIndices_ || offset > uploadIndexCapacity_ ||
       count > uploadIndexCapacity_ - offset)
        throw std::invalid_argument("Invalid or noncontiguous index upload range");
    for(uint32_t i = 0; i < count; ++i)
        if(indices[i] >= uploadVertexCapacity_)throw std::invalid_argument("Upload index exceeds vertex count");
    resources_.device->writeBuffer(indices_,size_t(offset)*4,size_t(count)*4,indices);
    ++geometryRevision_;
    uploadedIndices_ = std::max(uploadedIndices_,offset+count);
    finishRange();
}
std::shared_ptr<GpuMesh> GpuMesh::withGeneratedPoses(std::shared_ptr<GpuMesh> source,const std::vector<MeshVertex>& vertices,const std::vector<uint32_t>& indices){
    if(!source||!source->instances()||!source->indirectBuffer())throw std::invalid_argument("Generated pose source required");
    auto mesh=std::make_shared<GpuMesh>(source->resources_.device,vertices,indices);
    mesh->base_=source;mesh->instances_=source->instances_;mesh->previousInstances_=source->previousInstances_;mesh->instanceCapacity_=source->instanceCapacity_;
    const rhi::DrawIndexedIndirectArguments args{mesh->count_,0,0,0,0};mesh->indirect_=mesh->resources_.buffer({sizeof(args),rhi::BufferUsage::Storage|rhi::BufferUsage::Indirect|rhi::BufferUsage::CopySource|rhi::BufferUsage::CopyDestination,"Foliage instance arguments"},&args);return mesh;
}
void GpuMesh::syncGeneratedPoses(){
    if(!base_||!indirect_||instances_.value!=base_->instances_.value)throw std::logic_error("Mesh does not share generated poses");
    resources_.device->copyBuffer(base_->indirect_,4,indirect_,4,4);setBounds(base_->boundsMin_,base_->boundsMax_);
}
void GpuMesh::initializeMaterialTangents() {
    if(!resources_.device->computeLimits().supported)return;
    // Metal reflection includes one runtime-array element even when the
    // inactive flag prevents reads. Keep a real header + dummy element.
    const glm::vec4 empty[2]={glm::vec4(0),glm::vec4(0)};tangentBytes_=sizeof(empty);
    tangents_=resources_.buffer({tangentBytes_,rhi::BufferUsage::Storage,"Absent mesh tangent frame"},empty);
}
void GpuMesh::uploadMaterialTangents(uint32_t offset,const glm::vec4* frames,uint32_t count){
    if(!uploadingTangents_ || !frames || !count || offset>uploadedTangents_ || offset>vertexCount_ || count>vertexCount_-offset)
        throw std::invalid_argument("Invalid or noncontiguous material tangent upload");
    for(uint32_t i=0;i<count;++i){
        for(int c=0;c<4;++c)if(!std::isfinite(frames[i][c]))throw std::invalid_argument("Nonfinite material tangent upload");
        if(frames[i].w!=0 && frames[i].w!=1 && frames[i].w!=-1)throw std::invalid_argument("Invalid tangent upload handedness");
    }
    resources_.device->writeBuffer(tangents_,size_t(offset+1)*16,size_t(count)*16,frames);
    uploadedTangents_=std::max(uploadedTangents_,offset+count);
    if(uploadedTangents_==vertexCount_){const glm::vec4 active(1,0,0,0);resources_.device->writeBuffer(tangents_,0,16,&active);}
    finishRange();
}
void GpuMesh::setMaterialTangents(const std::vector<glm::vec4>& frames) {
    if(frames.empty())return;
    if(!ready_ || base_ || frames.size()!=vertexCount_)throw std::invalid_argument("Mesh tangent count / readiness mismatch");
    for(const auto& frame:frames)for(int c=0;c<4;++c)if(!std::isfinite(frame[c]))throw std::invalid_argument("Nonfinite mesh tangent");
    for(const auto& frame:frames)if(frame.w!=0 && frame.w!=1 && frame.w!=-1)throw std::invalid_argument("Invalid mesh tangent handedness");
    if(!resources_.device->computeLimits().supported)return; // GL 4.1 uses UV derivatives.
    std::vector<glm::vec4> data;data.reserve(frames.size()+1);data.emplace_back(1,0,0,0);data.insert(data.end(),frames.begin(),frames.end());
    auto uploaded=resources_.buffer({data.size()*16,rhi::BufferUsage::Storage,"Material corner tangents"},data.data());
    tangents_=uploaded;tangentBytes_=data.size()*16;
}
void GpuMesh::draw(rhi::CommandList& commands) const { if(!ready_)throw std::logic_error("Pending mesh cannot draw");commands.bindVertexBuffer(vertices_);commands.bindIndexBuffer(indices_, rhi::IndexType::UInt32);if(indirect_)commands.drawIndexedIndirect(indirect_);else commands.drawIndexed(count_); }
void GpuMesh::draw(rhi::CommandList& commands,rhi::BufferHandle visibleIndices,rhi::BufferHandle visibleArguments,size_t argumentOffset) const {
    if(!visibleIndices && !visibleArguments){draw(commands);return;}
    if(!ready_ || !visibleIndices || !visibleArguments)throw std::logic_error("Invalid visible mesh draw");
    commands.bindVertexBuffer(vertices_);commands.bindIndexBuffer(visibleIndices,rhi::IndexType::UInt32);
    commands.drawIndexedIndirect(visibleArguments,argumentOffset);
}
}
