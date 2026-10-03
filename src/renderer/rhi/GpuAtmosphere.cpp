#include "renderer/rhi/GpuAtmosphere.h"
#include "rhi/ShaderAssets.h"
#include <json/json.hpp>
#include <cmath>
#include <cstring>
namespace render {
GpuAtmosphere::GpuAtmosphere(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& directory):resources_(std::move(d)){
    using namespace rhi;static_assert(sizeof(AtmosphereSettings)==96,"Atmosphere std140 ABI");
    const uint32_t widths[]={256,200,32,200},heights[]={64,100,32,100};
    for(uint32_t i=0;i<4;++i){textures_[i]=resources_.texture({widths[i],heights[i],Format::RGBA32Float,TextureUsage::Storage|TextureUsage::Sampled|TextureUsage::CopySource,"Atmosphere LUT"});views_[i]=resources_.view(textures_[i]);}
    parameters_=resources_.buffer({96,BufferUsage::Uniform|BufferUsage::CopyDestination,"Atmosphere parameters"});sampler_=resources_.sampler({Filter::Linear,AddressMode::ClampToEdge});
    for(const char* name:{"transmittance","skyview","multi","convolution"}){
        std::string path=directory+"/atmosphere-"+name+".comp";ComputePipelineDesc p;p.shader={path+".glsl",path+".metallib",path+".spv",path+".json","main0"};
        auto r=nlohmann::json::parse(readShaderText(p.shader.reflectionPath));p.threads=r.at("entryPoints").at(0).at("workgroup_size").get<std::array<uint32_t,3>>();p.bindings={{0,{}},{1,{}}};
        for(const auto& u:r.value("ubos",nlohmann::json::array())){p.bindings[0].entries.push_back({u.at("binding"),BindingType::UniformBuffer,ShaderStage::Compute,u.at("name"),u.at("block_size")});if(u.at("name")=="ATMOS" && u.at("block_size")!=96)throw std::invalid_argument("Atmosphere reflected ABI differs");}
        for(const auto& t:r.value("textures",nlohmann::json::array()))p.bindings[1].entries.push_back({t.at("binding"),BindingType::SampledTexture,ShaderStage::Compute,t.at("name"),0});
        for(const auto& t:r.at("images"))p.bindings[1].entries.push_back({t.at("binding"),BindingType::StorageTextureWrite,ShaderStage::Compute,t.at("name"),0});
        kernels_[name]={resources_.computePipeline(p),p.bindings,p.threads};
    }
}
void GpuAtmosphere::update(const AtmosphereSettings& settings,float sun){
    const auto* values=reinterpret_cast<const float*>(&settings);for(size_t i=0;i<24;++i)if(!std::isfinite(values[i]))throw std::invalid_argument("Atmosphere nonfinite parameter");
    if(!std::isfinite(sun)||settings.radii.z<=settings.radii.w||settings.radii.w<=0||settings.densities.x<=0||settings.densities.y<=0||settings.absorption.w<=0||std::abs(settings.densities.w)>=1)throw std::invalid_argument("Atmosphere invalid domain");
    if(initialized_ && sun==previousSun_ && !std::memcmp(&settings,&previous_,sizeof(settings)))return;
    resources_.device->writeBuffer(parameters_,0,96,&settings);Resources frame(resources_.device);auto commands=resources_.device->createCommandList();
    const char* names[]={"transmittance","skyview","multi","convolution"};const int widths[]={256,200,32,200},heights[]={64,100,32,100};
    for(uint32_t i=0;i<4;++i){
        const auto& kernel=kernels_.at(names[i]);struct alignas(16) Params{glm::ivec4 dims;glm::vec4 sun;};Params p{{widths[i],heights[i],256,64},{sun,0,0,0}};
        auto data=frame.buffer({sizeof(p),rhi::BufferUsage::Uniform,"Atmosphere LUT dimensions"},&p);std::vector<rhi::BindingEntry> buffers,images;
        for(const auto& e:kernel.layouts[0].entries)buffers.push_back({e.binding,e.binding==0?parameters_:data,0,e.minimumSize,{},{}});
        for(const auto& e:kernel.layouts[1].entries)images.push_back({e.binding,{},0,0,e.type==rhi::BindingType::SampledTexture?views_[i==3?1:0]:views_[i],e.type==rhi::BindingType::SampledTexture?sampler_:rhi::SamplerHandle{}});
        auto a=frame.bindings({kernel.layouts[0],buffers}),b=frame.bindings({kernel.layouts[1],images});
        commands.dispatch(kernel.pipeline,{a,b},{uint32_t(widths[i])/kernel.threads[0],uint32_t(heights[i])/kernel.threads[1],1});
    }
    resources_.device->submit(commands);initialized_=true;previous_=settings;previousSun_=sun;
}
}
