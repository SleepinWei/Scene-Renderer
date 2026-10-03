#include "renderer/rhi/GpuGrass.h"
#include "rhi/ShaderAssets.h"
#include <cmath>
namespace render {
GpuGrass::GpuGrass(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& dir,std::shared_ptr<GpuTerrain> terrain,const glm::mat4& model,uint32_t cap):terrain_(std::move(terrain)),resources_(d){
    using namespace rhi;if(!terrain_)throw std::invalid_argument("Grass needs terrain");glm::vec3 minimum(1e30f),maximum(-1e30f);for(int i=0;i<8;i++){auto a=terrain_->mesh()->boundsMin(),b=terrain_->mesh()->boundsMax();glm::vec3 p=model*glm::vec4(i&1?b.x:a.x,i&2?b.y:a.y,i&4?b.z:a.z,1);minimum=glm::min(minimum,p);maximum=glm::max(maximum,p);}maximum.y+=.3f;minimum-=glm::vec3(.1f);maximum+=glm::vec3(.1f);
    mesh_=std::make_shared<GpuMesh>(d,std::vector<MeshVertex>{{{-.05f,0,0},{0,0,1},{0,1}},{{.05f,0,0},{0,0,1},{1,1}},{{0,.25f,0},{0,0,1},{.5f,0}}},std::vector<uint32_t>{0,1,2},cap,minimum,maximum);
    ComputePipelineDesc p;auto path=dir+"/grass-generate.comp";p.shader={path+".glsl",path+".metallib",path+".spv",path+".json","main0"};p.threads={16,16,1};p.bindings={{0,{{0,BindingType::UniformBuffer,ShaderStage::Compute,"GrassParameters",96}}},{1,{{0,BindingType::StorageRead,ShaderStage::Compute,"FinalNodeList",16},{1,BindingType::StorageWrite,ShaderStage::Compute,"OutPose",64},{2,BindingType::StorageReadWrite,ShaderStage::Compute,"GrassIndirect",20},{6,BindingType::StorageRead,ShaderStage::Compute,"HeightField",4}}}};layouts_=p.bindings;pipeline_=resources_.computePipeline(p);
}
void GpuGrass::update(const glm::mat4& model,float seconds){
    using namespace rhi;if(!std::isfinite(seconds))throw std::invalid_argument("Grass invalid time");const DrawIndexedIndirectArguments args{3,0,0,0,0};resources_.device->writeBuffer(mesh_->indirectBuffer(),0,sizeof(args),&args);Resources frame(resources_.device);struct alignas(16) Params{glm::mat4 model;glm::ivec4 dimensions;glm::vec4 settings;};Params p{model,{int(terrain_->width()),int(terrain_->height()),0,0},{seconds,float(mesh_->instanceCapacity()),0,0}};auto data=frame.buffer({96,BufferUsage::Uniform,"Grass parameters"},&p);
    auto a=frame.bindings({layouts_[0],{{0,data,0,96,{},{}}}}),b=frame.bindings({layouts_[1],{{0,terrain_->leafQueue(),0,16+25600*16,{},{}},{1,mesh_->instances(),0,size_t(mesh_->instanceCapacity())*64,{},{}},{2,mesh_->indirectBuffer(),0,20,{},{}},{6,terrain_->heightBuffer(),0,size_t(terrain_->width())*terrain_->height()*4,{},{}}}});auto commands=resources_.device->createCommandList();commands.dispatchIndirect(pipeline_,{a,b},terrain_->leafQueue());resources_.device->submit(commands);
}
rhi::DrawIndexedIndirectArguments GpuGrass::readArguments(){rhi::DrawIndexedIndirectArguments a;resources_.device->readBuffer(mesh_->indirectBuffer(),0,sizeof(a),&a);return a;}
std::vector<glm::mat4> GpuGrass::readPoses(uint32_t n){std::vector<glm::mat4> result(n);resources_.device->readBuffer(mesh_->instances(),0,size_t(n)*64,result.data());return result;}
}
