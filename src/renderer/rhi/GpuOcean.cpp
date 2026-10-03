#include "renderer/rhi/GpuOcean.h"
#include "rhi/ShaderAssets.h"
#include <json/json.hpp>
#include <cmath>
#include <stdexcept>
namespace render {
namespace {
void validate(const OceanSettings& s,float time) {
    if(s.size<8 || s.size>2048 || (s.size&(s.size-1)) || !std::isfinite(s.length) || s.length<=0) throw std::invalid_argument("Ocean: invalid FFT grid/domain");
    for(float value:{time,s.amplitude,s.windSpeed,s.choppiness,s.heightScale,s.foamScale,s.foamThreshold,s.windDirection.x,s.windDirection.y})
        if(!std::isfinite(value))throw std::invalid_argument("Ocean: nonfinite simulation parameter");
    if(s.amplitude<0 || s.windSpeed<0 || s.foamScale<0)throw std::invalid_argument("Ocean: negative spectrum/foam parameter");
}
}
GpuOcean::GpuOcean(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory,const OceanSettings& settings) : resources_(std::move(device)),initial_(settings) {
    validate(settings,0);static_assert(sizeof(Parameters)==64,"Ocean parameter ABI");
    if(!resources_.device->computeLimits().maxStorageImages)throw std::invalid_argument("Ocean: storage images unavailable on this backend");
    using namespace rhi;
    const char* labels[]={"GaussianRandomRT","HeightSpectrumRT","DisplaceXSpectrumRT","DisplaceZSpectrumRT","FFT scratch","DisplaceRT","NormalRT","BubblesRT"};
    for(size_t i=0;i<8;++i) { textures_[i]=resources_.texture({settings.size,settings.size,Format::RGBA32Float,TextureUsage::Storage|TextureUsage::Sampled|TextureUsage::CopySource|TextureUsage::CopyDestination,labels[i]});views_[i]=resources_.view(textures_[i]); }
    for(const char* name:{"gaussian","height","displacementspectrum","horizontal","vertical","displacement","normalfoam"}) {
        const auto path=directory+"/ocean-"+name+".comp";ComputePipelineDesc desc;desc.shader={path+".glsl",path+".metallib",path+".spv",path+".json","main0"};desc.threads={8,8,1};desc.label=std::string("Ocean ")+name;
        const auto reflection=nlohmann::json::parse(readShaderText(desc.shader.reflectionPath));
        for(const auto& ubo:reflection.at("ubos")) {
            if(ubo.at("name")!="OceanParameters" || ubo.at("block_size")!=64 || ubo.at("set")!=0 || ubo.at("binding")!=0)throw std::invalid_argument("Ocean: shader parameter layout mismatch");
        }
        for(const auto& ubo:reflection.at("ubos")){const auto& members=reflection.at("types").at(ubo.at("type").get<std::string>()).at("members");for(size_t i=0;i<4;++i)if(members.at(i).at("offset")!=i*16)throw std::invalid_argument("Ocean: parameter member offset mismatch");}
        desc.bindings={{0,{{0,BindingType::UniformBuffer,ShaderStage::Compute,"OceanParameters",64}}},{1,{}}};
        for(const auto& image:reflection.at("images")) {
            const auto type=image.value("readonly",false)?BindingType::StorageTextureRead:image.value("writeonly",false)?BindingType::StorageTextureWrite:BindingType::StorageTextureReadWrite;
            desc.bindings[1].entries.push_back({image.at("binding"),type,ShaderStage::Compute,image.at("name"),0});
        }
        auto pipeline=resources_.computePipeline(desc);kernels_.emplace(name,Kernel{pipeline,desc.bindings});
    }
}
void GpuOcean::record(Resources& frame,rhi::CommandList& list,const char* name,const Parameters& parameters,const std::map<uint32_t,rhi::TextureViewHandle>& images) {
    const auto& kernel=kernels_.at(name);
    auto data=frame.buffer({sizeof(parameters),rhi::BufferUsage::Uniform,"Ocean dispatch parameters"},&parameters);
    auto params=frame.bindings({kernel.layouts[0],{{0,data,0,sizeof(parameters),{},{}}}});
    std::vector<rhi::BindingEntry> entries;for(const auto& image:images)entries.push_back({image.first,{},0,0,image.second,{}});
    auto bindings=frame.bindings({kernel.layouts[1],entries});list.dispatch(kernel.pipeline,{params,bindings},{initial_.size/8,initial_.size/8,1});
}
void GpuOcean::recordFFT(Resources& frame,rhi::CommandList& list,Parameters parameters,rhi::TextureHandle& texture,rhi::TextureViewHandle& view) {
    for(const char* axis:{"horizontal","vertical"})for(uint32_t ns=1;ns<initial_.size;ns*=2) {
        parameters.grid.y=int(ns);record(frame,list,axis,parameters,{{5,view},{6,views_[4]}});
        std::swap(texture,textures_[4]);std::swap(view,views_[4]);
    }
}
void GpuOcean::simulate(float seconds,const OceanSettings& settings) {
    validate(settings,seconds);if(settings.size!=initial_.size || settings.length!=initial_.length)throw std::invalid_argument("Ocean: grid/domain changes require recreation");
    const float windLength=glm::length(settings.windDirection);const auto wind=windLength>1e-6f?settings.windDirection/windLength*settings.windSpeed:glm::vec2(0);
    Parameters p{{int(settings.size),0,0,settings.seed},{seconds,settings.amplitude,settings.length,0},{wind,0,0},{settings.choppiness,settings.heightScale,settings.foamScale,settings.foamThreshold}};
    Resources frame(resources_.device);auto list=resources_.device->createCommandList();
    if(!seeded_ || seed_!=settings.seed)record(frame,list,"gaussian",p,{{1,views_[0]}});
    record(frame,list,"height",p,{{1,views_[0]},{2,views_[1]}});
    record(frame,list,"displacementspectrum",p,{{2,views_[1]},{3,views_[2]},{4,views_[3]}});
    for(size_t i=1;i<=3;++i)recordFFT(frame,list,p,textures_[i],views_[i]);
    record(frame,list,"displacement",p,{{2,views_[1]},{3,views_[2]},{4,views_[3]},{7,views_[5]}});
    record(frame,list,"normalfoam",p,{{7,views_[5]},{5,views_[6]},{6,views_[7]}});
    resources_.device->submit(list);seed_=settings.seed;seeded_=true;
}
std::vector<float> GpuOcean::inverseFFT(const std::vector<float>& input) {
    if(input.size()!=size_t(initial_.size)*initial_.size*4)throw std::invalid_argument("Ocean: invalid FFT input size");
    resources_.device->writeTextureFloat(textures_[1],input.data(),input.size()*sizeof(float));
    Resources frame(resources_.device);auto list=resources_.device->createCommandList();Parameters parameters{};parameters.grid.x=int(initial_.size);
    recordFFT(frame,list,parameters,textures_[1],views_[1]);resources_.device->submit(list);return readHeight();
}
std::vector<float> GpuOcean::readGaussian(){return resources_.device->readTextureFloat(textures_[0]);}
std::vector<float> GpuOcean::readHeight(){return resources_.device->readTextureFloat(textures_[1]);}
std::vector<float> GpuOcean::readDisplacement(){return resources_.device->readTextureFloat(textures_[5]);}
std::vector<float> GpuOcean::readNormal(){return resources_.device->readTextureFloat(textures_[6]);}
std::vector<float> GpuOcean::readFoam(){return resources_.device->readTextureFloat(textures_[7]);}
}
