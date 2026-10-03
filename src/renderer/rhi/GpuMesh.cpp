#include "renderer/rhi/GpuMesh.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
namespace render {
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
    vertices_ = resources_.buffer({vertices.size() * sizeof(MeshVertex), rhi::BufferUsage::Vertex, "PBR vertices"}, vertices.data());
    indices_ = resources_.buffer({indices.size() * sizeof(uint32_t), rhi::BufferUsage::Index, "PBR indices"}, indices.data());count_ = uint32_t(indices.size());
}
GpuMesh::GpuMesh(std::shared_ptr<rhi::GraphicsDevice> device,uint32_t vertices,uint32_t indices,glm::vec3 minimum,glm::vec3 maximum):resources_(std::move(device)),boundsMin_(minimum),boundsMax_(maximum){
    using namespace rhi;if(!vertices || !indices || !resources_.device->computeLimits().supported)throw std::invalid_argument("Generated mesh requires compute and positive capacities");
    vertices_=resources_.buffer({size_t(vertices)*sizeof(MeshVertex),BufferUsage::Vertex|BufferUsage::Storage|BufferUsage::CopySource,"Generated mesh vertices"});indices_=resources_.buffer({size_t(indices)*4,BufferUsage::Index|BufferUsage::Storage|BufferUsage::CopySource,"Generated mesh indices"});
    const DrawIndexedIndirectArguments args{0,1,0,0,0};indirect_=resources_.buffer({sizeof(args),BufferUsage::Storage|BufferUsage::Indirect|BufferUsage::CopySource|BufferUsage::CopyDestination,"Generated indexed arguments"},&args);count_=indices;
}
GpuMesh::GpuMesh(std::shared_ptr<rhi::GraphicsDevice> d,const std::vector<MeshVertex>& v,const std::vector<uint32_t>& i,uint32_t cap,glm::vec3 minimum,glm::vec3 maximum):GpuMesh(d,v,i){
    using namespace rhi;if(!cap || !d->computeLimits().supported)throw std::invalid_argument("Instanced mesh requires compute storage");instanceCapacity_=cap;boundsMin_=minimum;boundsMax_=maximum;instances_=resources_.buffer({size_t(cap)*64,BufferUsage::Storage|BufferUsage::CopySource,"GPU instance poses"});const DrawIndexedIndirectArguments args{count_,0,0,0,0};indirect_=resources_.buffer({sizeof(args),BufferUsage::Storage|BufferUsage::Indirect|BufferUsage::CopySource|BufferUsage::CopyDestination,"GPU indexed instance arguments"},&args);
}
void GpuMesh::draw(rhi::CommandList& commands) const { commands.bindVertexBuffer(vertices_);commands.bindIndexBuffer(indices_, rhi::IndexType::UInt32);if(indirect_)commands.drawIndexedIndirect(indirect_);else commands.drawIndexed(count_); }
}
