#include "renderer/rhi/GpuGrass.h"
#include "rhi/ShaderAssets.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <cmath>
namespace render {
namespace {
struct alignas(16) Parameters {
    glm::mat4 model;glm::ivec4 dimensions;glm::vec4 settings,placement,shape,controls,camera,distribution,exclusion;
};
static_assert(sizeof(Parameters)==192,"Vegetation std140 ABI");
}
GpuGrass::GpuGrass(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& dir,
                  std::shared_ptr<GpuTerrain> terrain,const glm::mat4& model,uint32_t cap,
                  VegetationSettings settings,std::shared_ptr<const ImageRGBA8> mask,std::shared_ptr<const ImageRGBA8> exclusionMask)
    :terrain_(std::move(terrain)),resources_(d),settings_(std::move(settings)) {
    using namespace rhi;settings_.capacity=cap;settings_.validate();
    if(!terrain_)throw std::invalid_argument("Grass needs terrain");
    std::vector<MeshVertex> vertices;std::vector<uint32_t> indices;
    // Four slender curved blades form a clump visible from every azimuth.
    for(int blade=0;blade<4;++blade){
        float a=blade*2.3999632f,c=std::cos(a),s=std::sin(a);
        const glm::vec3 points[]={{-.015f,0,0},{.015f,0,0},{-.012f,.08f,.004f},{.012f,.08f,.004f},{-.007f,.17f,.025f},{.007f,.17f,.025f},{0,.25f,.055f}};
        const glm::vec2 uv[]={{0,1},{1,1},{0,.68f},{1,.68f},{0,.32f},{1,.32f},{.5f,0}};
        uint32_t base=uint32_t(vertices.size());
        for(int i=0;i<7;++i){auto p=points[i];p.y*=.85f+.1f*blade;vertices.push_back({{c*p.x+s*p.z,p.y,-s*p.x+c*p.z},{s,0,c},uv[i]});}
        for(uint32_t i:{0u,2u,1u,1u,2u,3u,2u,4u,3u,3u,4u,5u,4u,6u,5u})indices.push_back(base+i);
    }
    mesh_=std::make_shared<GpuMesh>(d,vertices,indices,cap,glm::vec3(-1),glm::vec3(1));
    imageCache_=GpuImageCache::forDevice(d);
    mask_=imageCache_->acquire(mask?mask:std::make_shared<const ImageRGBA8>(ImageRGBA8{1,1,{0,0,0,255}}));
    exclusion_=imageCache_->acquire(exclusionMask?exclusionMask:std::make_shared<const ImageRGBA8>(ImageRGBA8{1,1,{0,0,0,255}}));
    maskSampler_=resources_.sampler({Filter::Linear,AddressMode::ClampToEdge});
    // Mask availability is detached from the file path for direct RHI clients.
    hasMask_=bool(mask);
    ComputePipelineDesc p;auto path=dir+"/grass-generate.comp";
    p.shader={path+".glsl",path+".metallib",path+".spv",path+".json","main0"};p.threads={16,16,1};
    p.bindings={{0,{{0,BindingType::UniformBuffer,ShaderStage::Compute,"GrassParameters",192},
                    {1,BindingType::UniformBuffer,ShaderStage::Compute,"GrassSurface",144}}},
                {1,{{0,BindingType::StorageRead,ShaderStage::Compute,"FinalNodeList",16},
                    {1,BindingType::StorageWrite,ShaderStage::Compute,"OutPose",64},
                    {2,BindingType::StorageReadWrite,ShaderStage::Compute,"GrassIndirect",20},
                    {3,BindingType::StorageRead,ShaderStage::Compute,"GrassCandidates",8},
                    {4,BindingType::StorageTextureRead,ShaderStage::Compute,"lodMap",0},
                    {5,BindingType::SampledTexture,ShaderStage::Compute,"waterMask",0},
                    {6,BindingType::SampledTexture,ShaderStage::Compute,"heightAtlas",0},
                    {7,BindingType::SampledTexture,ShaderStage::Compute,"heightPageTable",0}}}};
    p.bindings.push_back({2,{{0,BindingType::SampledTexture,ShaderStage::Compute,"exclusionMask",0}}});
    layouts_=p.bindings;pipeline_=resources_.computePipeline(p);
    candidates_=resources_.buffer({8,BufferUsage::Storage|BufferUsage::CopyDestination,"Eligible vegetation count"});
    path=dir+"/grass-count.comp";p.shader={path+".glsl",path+".metallib",path+".spv",path+".json","main0"};
    auto& entries=p.bindings[1].entries;
    entries.erase(entries.begin()+1,entries.begin()+3);entries[1].type=BindingType::StorageReadWrite;
    countLayouts_=p.bindings;countPipeline_=resources_.computePipeline(p);
}
void GpuGrass::update(const glm::mat4& model,float seconds,const VegetationSettings& settings){
    settings.validate();
    if(settings.capacity!=mesh_->instanceCapacity())throw std::invalid_argument("Vegetation capacity change requires recreation");
    settings_=settings;update(model,seconds);
}
void GpuGrass::update(const glm::mat4& model,float seconds){
    using namespace rhi;if(!std::isfinite(seconds))throw std::invalid_argument("Grass invalid time");
    glm::vec3 minimum(1e30f),maximum(-1e30f);
    for(int i=0;i<8;i++){auto a=terrain_->mesh()->boundsMin(),b=terrain_->mesh()->boundsMax();
        glm::vec3 p=model*glm::vec4(i&1?b.x:a.x,i&2?b.y:a.y,i&4?b.z:a.z,1);
        minimum=glm::min(minimum,p);maximum=glm::max(maximum,p);}
    float radius=std::max(settings_.heightScale*.4f,settings_.widthScale*.15f);
    mesh_->setBounds(minimum-glm::vec3(radius),maximum+glm::vec3(radius));
    const DrawIndexedIndirectArguments args{60,0,0,0,0};
    resources_.device->writeBuffer(mesh_->indirectBuffer(),0,sizeof(args),&args);
    uint32_t zero[2]={0,0};resources_.device->writeBuffer(candidates_,0,8,zero);Resources frame(resources_.device);
    Parameters p{model,{int(terrain_->width()),int(terrain_->height()),0,0},
        {seconds,float(mesh_->instanceCapacity()),settings_.density,settings_.distance},
        {settings_.fadeStart,settings_.minimumNormalY,settings_.waterLevel,settings_.shoreMargin},
        {settings_.heightScale,settings_.widthScale,settings_.maximumAltitude,hasMask_?1.f:0.f},
        {float(settings_.maxLod),settings_.frustumCull?1.f:0.f,float(settings_.samplesPerCell),0},glm::inverse(terrain_->surfaceView())[3],
        {settings_.nearSpacing,settings_.farSpacing,settings_.denseRadius,settings_.sparseRadius},
        {settings_.exclusionSeaLevel,settings_.exclusionHeightRange,settings_.exclusionSlopeMin,settings_.exclusionSlopeMax}};
    auto data=frame.buffer({sizeof(p),BufferUsage::Uniform,"Vegetation placement parameters"},&p);
    struct alignas(16) Surface {glm::mat4 view,vp;glm::vec4 screen;};
    Surface surface{terrain_->surfaceView(),terrain_->surfaceVP(),terrain_->surfaceScreen()};
    auto surfaceData=frame.buffer({sizeof(surface),BufferUsage::Uniform,"Grass morphed terrain attachment"},&surface);
    auto a=frame.bindings({layouts_[0],{{0,data,0,sizeof(p),{},{}},{1,surfaceData,0,sizeof(surface),{},{}}}});
    std::vector<BindingEntry> entries{{0,terrain_->leafQueue(),0,16+size_t(terrain_->leafCapacity())*16,{},{}},
        {1,mesh_->instances(),0,size_t(mesh_->instanceCapacity())*64,{},{}},{2,mesh_->indirectBuffer(),0,20,{},{}},
        {3,candidates_,0,8,{},{}},{4,{},0,0,terrain_->lodView(),{}},{5,{},0,0,mask_->view(),maskSampler_},
        {6,{},0,0,terrain_->heightTexture()->atlas(),terrain_->heightTexture()->sampler()},
        {7,{},0,0,terrain_->heightTexture()->pageTable(),terrain_->heightTexture()->sampler()}};
    auto b=frame.bindings({layouts_[1],entries});entries.erase(entries.begin()+1,entries.begin()+3);
    auto counter=frame.bindings({countLayouts_[1],entries});
    auto exclusion=frame.bindings({layouts_[2],{{0,{},0,0,exclusion_->view(),maskSampler_}}});
    auto commands=resources_.device->createCommandList();
    commands.dispatchIndirect(countPipeline_,{a,counter,exclusion},terrain_->leafQueue());
    commands.dispatchIndirect(pipeline_,{a,b,exclusion},terrain_->leafQueue());resources_.device->submit(commands);
}
rhi::DrawIndexedIndirectArguments GpuGrass::readArguments(){rhi::DrawIndexedIndirectArguments a;resources_.device->readBuffer(mesh_->indirectBuffer(),0,sizeof(a),&a);return a;}
std::vector<glm::mat4> GpuGrass::readPoses(uint32_t n){if(n>mesh_->instanceCapacity())throw std::invalid_argument("Grass readback exceeds capacity");std::vector<glm::mat4> result(n);resources_.device->readBuffer(mesh_->instances(),0,size_t(n)*64,result.data());return result;}
}
