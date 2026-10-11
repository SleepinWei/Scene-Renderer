#include "renderer/rhi/GpuTreeWind.h"
#include <cstring>
namespace render {
GpuTreeWind::GpuTreeWind(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& dir,std::shared_ptr<GpuMesh> mesh,std::shared_ptr<const MeshPayload> source):mesh_(std::move(mesh)),resources_(d){
    using namespace rhi;
    if(!d->computeLimits().supported||source->treeWind.size()!=source->vertices.size()||source->vertices.size()!=mesh_->vertexCount())throw std::invalid_argument("Tree wind requires matching vertex metadata and compute");
    static_assert(sizeof(TreeWindVertex)==48,"Tree wind GPU ABI");
    const size_t bytes=source->vertices.size()*sizeof(MeshVertex);
    rest_=resources_.buffer({bytes,BufferUsage::Storage,"Tree rest vertices"},source->vertices.data());
    data_=resources_.buffer({source->treeWind.size()*48,BufferUsage::Storage,"Tree branch and leaf pivots"},source->treeWind.data());
    BindingLayout layout{0,{{0,BindingType::UniformBuffer,ShaderStage::Compute,"TreeClock",48},{1,BindingType::StorageRead,ShaderStage::Compute,"TreeRest",32},{2,BindingType::StorageRead,ShaderStage::Compute,"TreePivots",48},{3,BindingType::StorageReadWrite,ShaderStage::Compute,"TreeDeformed",32},{4,BindingType::StorageWrite,ShaderStage::Compute,"TreePrevious",32}}};
    auto path=dir+"/tree-wind.comp";pipeline_=resources_.computePipeline({{path+".glsl",path+".metallib",path+".spv",path+".json","main0"},{layout},{64,1,1},"Tree branch and leaf wind"});
    layout_=layout;
    float padding=0,maxY=0;for(size_t i=0;i<source->vertices.size();++i){auto p=source->vertices[i].position;auto v=source->treeWind[i];maxY=std::max(maxY,p.y);padding=std::max(padding,.24f*v.branch.w*glm::length(p-glm::vec3(v.branch))+.40f*v.twig.w*glm::length(p-glm::vec3(v.twig))+1.1f*v.leaf.w*glm::length(p-glm::vec3(v.leaf)));}
    padding+=.07f*(maxY+padding)*(maxY+padding);
    mesh_->enableVertexHistory();mesh_->padWindBounds(padding);
}
void GpuTreeWind::record(rhi::CommandList& commands,float seconds,const TreeWindSettings& s){
    s.validate();if(!std::isfinite(seconds))throw std::invalid_argument("Nonfinite tree clock");
    const size_t bytes=mesh_->vertexCount()*sizeof(MeshVertex);
    const bool changed=!history_||seconds!=seconds_||s.velocity!=settings_.velocity||s.phase!=settings_.phase||s.branches!=settings_.branches||s.flutter!=settings_.flutter||s.enabled!=settings_.enabled;
    const bool reliable=history_&&seconds>=seconds_&&seconds-seconds_<=.25f&&s.enabled==settings_.enabled;
    {
        const std::array<glm::vec4,3> p={glm::vec4(seconds,s.phase,s.enabled?s.branches:0,s.enabled?s.flutter:0),glm::vec4(s.enabled?s.velocity:glm::vec2(0),0,0),glm::vec4(float(mesh_->vertexCount()),reliable?1.f:0.f,0,0)};
        frame_=std::make_unique<Resources>(resources_.device);auto parameters=frame_->buffer({48,rhi::BufferUsage::Uniform,"Frozen tree wind clock"},p.data());
        auto bindings=frame_->bindings({layout_,{{0,parameters,0,48,{},{}},{1,rest_,0,bytes,{},{}},{2,data_,0,size_t(mesh_->vertexCount())*48,{},{}},{3,mesh_->vertexBuffer(),0,bytes,{},{}},{4,mesh_->previousVertices(),0,bytes,{},{}}}});
        commands.dispatch(pipeline_,{bindings},{(mesh_->vertexCount()+63)/64,1,1});
        if(changed)mesh_->markGeometryChanged();
    }
    mesh_->setVertexHistoryReliable(reliable);history_=true;seconds_=seconds;settings_=s;
}
std::shared_ptr<const MeshPayload> freezeTreeWind(const MeshPayload& source,float seconds,const TreeWindSettings& s){
    s.validate();if(source.treeWind.size()!=source.vertices.size())throw std::invalid_argument("Frozen tree wind metadata mismatch");
    auto result=std::make_shared<MeshPayload>(source);for(size_t i=0;i<result->vertices.size();++i){auto& v=result->vertices[i];treeWindDeform(v.position,v.normal,source.treeWind[i],s,seconds);}result->treeWind.clear();return result;
}
}
