#include "renderer/rhi/GpuTerrain.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "rhi/ShaderAssets.h"
#include <json/json.hpp>
#include <algorithm>
#include <cmath>
namespace render {
GpuTerrain::GpuTerrain(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& dir,uint32_t width,uint32_t height,const std::vector<float>& data,uint32_t capacity):GpuTerrain(d,dir,heightVirtualSource(width,height,data),capacity){}
GpuTerrain::GpuTerrain(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& dir,VirtualTextureSource source,uint32_t capacity):resources_(d),capacity_(capacity),width_(source.extent),height_(source.extent){
    using namespace rhi;if(capacity<25 || capacity>25600 || !source.heightField)throw std::invalid_argument("Terrain leaf budget or height source invalid");
    heightTexture_=std::make_shared<GpuVirtualTexture>(d,std::move(source));const uint32_t patches=capacity*64;
    mesh_=std::make_shared<GpuMesh>(d,patches*4,patches*6,glm::vec3(-1,heightTexture_->minimum(),-1),glm::vec3(1,heightTexture_->maximum(),1));
    descriptors_=resources_.buffer({34126*4,BufferUsage::Storage|BufferUsage::CopyDestination,"Terrain quadtree descriptors"});finalNodes_=resources_.buffer({16+size_t(capacity)*16,BufferUsage::Storage|BufferUsage::Indirect|BufferUsage::CopyDestination|BufferUsage::CopySource,"Terrain leaf queue"});
    lod_=resources_.texture({160,160,Format::RGBA32Float,TextureUsage::Storage|TextureUsage::CopySource,"Terrain neighbor LOD"});lodView_=resources_.view(lod_);
    for(const char* name:{"lod","lodmap","generate"}){std::string path=dir+"/terrain-"+name+".comp";ComputePipelineDesc p;p.shader={path+".glsl",path+".metallib",path+".spv",path+".json","main0"};auto r=nlohmann::json::parse(readShaderText(p.shader.reflectionPath));p.threads=r.at("entryPoints").at(0).at("workgroup_size").get<std::array<uint32_t,3>>();p.bindings={{0,{}},{1,{}}};
        for(const auto& u:r.at("ubos"))p.bindings[0].entries.push_back({u.at("binding"),BindingType::UniformBuffer,ShaderStage::Compute,u.at("name"),u.at("block_size")});
        for(const auto& b:r.at("ssbos"))p.bindings[1].entries.push_back({b.at("binding"),b.value("readonly",false)?BindingType::StorageRead:b.value("writeonly",false)?BindingType::StorageWrite:BindingType::StorageReadWrite,ShaderStage::Compute,b.at("name"),b.at("block_size")});
        for(const auto& b:r.value("images",nlohmann::json::array()))p.bindings[1].entries.push_back({b.at("binding"),b.value("readonly",false)?BindingType::StorageTextureRead:b.value("writeonly",false)?BindingType::StorageTextureWrite:BindingType::StorageTextureReadWrite,ShaderStage::Compute,b.at("name"),0});
        for(const auto& b:r.value("textures",nlohmann::json::array()))p.bindings[b.at("set").get<uint32_t>()].entries.push_back({b.at("binding"),BindingType::SampledTexture,ShaderStage::Compute,b.at("name"),0});
        kernels_[name]={resources_.computePipeline(p),p.bindings};
    }
}
GpuTerrain::~GpuTerrain()=default;
void GpuTerrain::update(const FrameData& f,const glm::mat4& model){
    using namespace rhi;const size_t QueueBytes=16+size_t(capacity_)*8,FinalBytes=16+size_t(capacity_)*16;const uint32_t Patches=capacity_*64;
    for(const auto& matrix:{f.viewProjection,f.view,model})for(int c=0;c<4;c++)for(int r=0;r<4;r++)if(!std::isfinite(matrix[c][r]))throw std::invalid_argument("Terrain nonfinite transform");
    heightTexture_->prepare(f.viewProjection,model,f.viewportWidth,f.viewportHeight);
    struct alignas(16) Params{glm::mat4 vp,view,model;glm::ivec4 counts,dimensions;};static_assert(sizeof(Params)==224,"Terrain parameter ABI");
    const std::array<uint32_t,4> empty{0,1,1,0};resources_.device->writeBuffer(finalNodes_,0,16,empty.data());std::vector<uint32_t> zero(34126);zero.back()=25;resources_.device->writeBuffer(descriptors_,0,zero.size()*4,zero.data());const DrawIndexedIndirectArguments args{0,1,0,0,0};resources_.device->writeBuffer(mesh_->indirectBuffer(),0,sizeof(args),&args);
    Resources frame(resources_.device);std::vector<uint32_t> initial(QueueBytes/4);initial[0]=25;initial[1]=initial[2]=1;for(uint32_t y=0;y<5;++y)for(uint32_t x=0;x<5;++x){initial[4+(y*5+x)*2]=x;initial[5+(y*5+x)*2]=y;}
    auto input=frame.buffer({QueueBytes,BufferUsage::Storage|BufferUsage::Indirect,"Terrain initial nodes"},initial.data());auto c=resources_.device->createCommandList();
    auto record=[&](const char* name,uint32_t level,BufferHandle in,BufferHandle out){
        auto& k=kernels_.at(name);Params p{f.viewProjection,f.view,model,{int(level),int(capacity_),0,0},{int(width_),int(height_),glm::floatBitsToInt(heightTexture_->minimum()),glm::floatBitsToInt(heightTexture_->maximum())}};auto params=frame.buffer({sizeof(p),BufferUsage::Uniform,"Terrain stage parameters"},&p);
        std::vector<BindingEntry> bufferEntries,imageEntries;for(const auto& e:k.layouts[0].entries)if(e.type==BindingType::SampledTexture)bufferEntries.push_back({e.binding,{},0,0,heightTexture_->pageTable(),heightTexture_->sampler()});else bufferEntries.push_back({e.binding,params,0,sizeof(p),{},{}});
        for(const auto& e:k.layouts[1].entries){if(e.type==BindingType::SampledTexture){imageEntries.push_back({e.binding,{},0,0,heightTexture_->atlas(),heightTexture_->sampler()});continue;}if(isStorageTexture(e.type)){imageEntries.push_back({e.binding,{},0,0,lodView_,{}});continue;}BufferHandle handle;
            if(e.name=="NodeDescriptor")handle=descriptors_;else if(e.name=="FinalNodeList")handle=finalNodes_;else if(e.name=="InQueue")handle=in;else if(e.name=="OutQueue")handle=out;else if(e.name=="GeneratedVertices")handle=mesh_->vertexBuffer();else if(e.name=="GeneratedIndices")handle=mesh_->indexBuffer();else if(e.name=="OutIndirect")handle=mesh_->indirectBuffer();else throw std::invalid_argument("Terrain unknown reflected binding");
            size_t bytes=e.name=="NodeDescriptor"?34126*4:e.name=="FinalNodeList"?FinalBytes:e.name=="GeneratedVertices"?size_t(Patches)*4*32:e.name=="GeneratedIndices"?size_t(Patches)*6*4:e.name=="OutIndirect"?20:QueueBytes;imageEntries.push_back({e.binding,handle,0,bytes,{},{}});
        }
        auto a=frame.bindings({k.layouts[0],bufferEntries}),b=frame.bindings({k.layouts[1],imageEntries});if(std::string(name)=="lodmap")c.dispatch(k.pipeline,{a,b},{160,160,1});else c.dispatchIndirect(k.pipeline,{a,b},std::string(name)=="lod"?in:finalNodes_);
    };
    for(uint32_t i=0;i<6;++i){std::vector<uint32_t> queue(QueueBytes/4);queue[1]=queue[2]=1;auto output=frame.buffer({QueueBytes,BufferUsage::Storage|BufferUsage::Indirect,"Terrain subdivision queue"},queue.data());record("lod",i,input,output);input=output;}
    record("lodmap",0,{},{});record("generate",0,{},{});resources_.device->submit(c);
}
std::vector<uint32_t> GpuTerrain::readNodes(){uint32_t n;resources_.device->readBuffer(finalNodes_,0,4,&n);if(n>capacity_)throw std::runtime_error("Terrain leaf overflow");std::vector<uint32_t> nodes(n*4);resources_.device->readBuffer(finalNodes_,16,nodes.size()*4,nodes.data());return nodes;}
rhi::DrawIndexedIndirectArguments GpuTerrain::readArguments(){rhi::DrawIndexedIndirectArguments a;resources_.device->readBuffer(mesh_->indirectBuffer(),0,sizeof(a),&a);return a;}
std::vector<MeshVertex> GpuTerrain::readVertices(uint32_t n){std::vector<MeshVertex> v(n);resources_.device->readBuffer(mesh_->vertexBuffer(),0,size_t(n)*32,v.data());return v;}
std::vector<uint32_t> GpuTerrain::readIndices(uint32_t n){std::vector<uint32_t> v(n);resources_.device->readBuffer(mesh_->indexBuffer(),0,size_t(n)*4,v.data());return v;}
}
