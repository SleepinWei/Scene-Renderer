#include "renderer/rhi/GpuInstanceCuller.h"
namespace render {
GpuInstanceCuller::GpuInstanceCuller(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& directory):resources_(d){
    using namespace rhi;
    layouts_={{0,{{0,BindingType::UniformBuffer,ShaderStage::Compute,"CullParameters",112}}},
        {1,{{0,BindingType::StorageRead,ShaderStage::Compute,"InputArgs",20},{1,BindingType::StorageRead,ShaderStage::Compute,"InputPoses",64},{2,BindingType::StorageReadWrite,ShaderStage::Compute,"OutputArgs",20},{3,BindingType::StorageReadWrite,ShaderStage::Compute,"OutputPoses",64}}}};
    auto asset=[&](const char* name){auto p=directory+"/"+name;return ShaderAsset{p+".glsl",p+".metallib",p+".spv",p+".json","main0"};};
    prepare_=resources_.computePipeline({asset("instance-cull-prepare.comp"),layouts_,{1,1,1},"Shadow instance arguments"});
    cull_=resources_.computePipeline({asset("instance-cull.comp"),layouts_,{64,1,1},"Light-view instance compaction"});
}
void GpuInstanceCuller::reset(){cursor_=0;measures_.clear();frameResources_=std::make_unique<Resources>(resources_.device);}
std::vector<CulledInstances> GpuInstanceCuller::record(rhi::CommandList& commands,const FrameData& view,const std::vector<DrawPacket>& packets,bool enabled){
    using namespace rhi;std::vector<CulledInstances> result(packets.size());if(!frameResources_)frameResources_=std::make_unique<Resources>(resources_.device);auto& frame=*frameResources_;
    for(size_t i=0;i<packets.size();i++){
        const auto& p=packets[i];auto mesh=p.mesh;if(!mesh->instances()||!mesh->indirectBuffer())continue;
        if(!enabled){measures_.push_back({mesh->indirectBuffer(),mesh->indirectBuffer(),mesh->indexCount()});continue;}
        if(cursor_==slots_.size())slots_.push_back({});auto& slot=slots_[cursor_++];uint32_t capacity=mesh->instanceCapacity();
        if(slot.capacity<capacity){slot.resources=std::make_unique<Resources>(resources_.device);slot.capacity=capacity;slot.poses=slot.resources->buffer({size_t(capacity)*64,BufferUsage::Storage|BufferUsage::CopySource|BufferUsage::CopyDestination,"Shadow compact instance poses",BufferInitialization::Uninitialized});slot.args=slot.resources->buffer({20,BufferUsage::Storage|BufferUsage::Indirect|BufferUsage::CopySource,"Shadow compact arguments"});}
        struct alignas(16) Parameters {glm::mat4 clip;glm::vec4 minimum,maximum,guard;};
        Parameters params{view.viewProjection*p.model,{mesh->instanceBoundsMin(),float(capacity)},{mesh->instanceBoundsMax(),float(mesh->indexCount())},{2.f/std::max(1u,view.viewportWidth),2.f/std::max(1u,view.viewportHeight),0,0}};
        auto uniform=frame.buffer({sizeof(params),BufferUsage::Uniform,"Light-view local bounds"},&params);
        auto a=frame.bindings({layouts_[0],{{0,uniform,0,sizeof(params),{},{}}}});
        auto b=frame.bindings({layouts_[1],{{0,mesh->indirectBuffer(),0,20,{},{}},{1,mesh->instances(),0,size_t(capacity)*64,{},{}},{2,slot.args,0,20,{},{}},{3,slot.poses,0,size_t(slot.capacity)*64,{},{}}}});
        commands.dispatch(prepare_,{a,b},{1,1,1});commands.dispatch(cull_,{a,b},{(capacity+63)/64,1,1});
        result[i]={slot.poses,slot.args};measures_.push_back({mesh->indirectBuffer(),slot.args,mesh->indexCount()});
    }return result;
}
std::array<uint64_t,4> GpuInstanceCuller::readStats(){std::array<uint64_t,4> stats{};for(const auto& m:measures_){rhi::DrawIndexedIndirectArguments a{},b{};resources_.device->readBuffer(m.input,0,20,&a);resources_.device->readBuffer(m.output,0,20,&b);stats[0]+=a.instanceCount;stats[1]+=b.instanceCount;stats[2]+=uint64_t(a.instanceCount)*(a.indexCount/3);stats[3]+=uint64_t(b.instanceCount)*(b.indexCount/3);}return stats;}
}
