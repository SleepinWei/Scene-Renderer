#include "renderer/rhi/GpuAtmosphere.h"
#include "rhi/ShaderAssets.h"
#include <json/json.hpp>
#include <cmath>
#include <cstring>
#include <algorithm>
namespace render {
float atmosphereHorizon(const AtmosphereSettings& a,float h) {
    double ratio=double(a.radii.w)/(double(a.radii.w)+h);return float(-std::sqrt(std::max(0.0,1-ratio*ratio)));
}
glm::vec3 solarTransmittance(const AtmosphereSettings& a,const SunState& sun,bool includeDiskVisibility) {
    const double bottom=a.radii.w,r=bottom+sun.observerHeightKm;
    const double horizon=atmosphereHorizon(a,sun.observerHeightKm),mu=glm::normalize(sun.direction).y;
    const double width=bottom/r*a.radii.y,q=std::clamp((mu-horizon+width)/(2*width),0.0,1.0),visibility=q*q*(3-2*q);
    if(includeDiskVisibility && visibility==0)return glm::vec3(0);
    const double m=std::max(mu,horizon),length=-r*m+std::sqrt(std::max(0.0,r*r*(m*m-1)+double(a.radii.z)*a.radii.z));
    glm::dvec3 optical(0);constexpr int steps=256;
    for(int i=0;i<steps;++i) {
        double x=double(i)/steps,y=double(i+1)/steps,start=x*x*length,end=y*y*length,t=(start+end)*.5;
        double h=std::sqrt(r*r+2*r*m*t+t*t)-bottom;
        optical+=(glm::dvec3(a.rayleigh)*std::exp(-h/a.densities.x)+glm::dvec3(a.extinction)*std::exp(-h/a.densities.y)+glm::dvec3(a.absorption)*std::max(0.0,1-std::abs(h-a.densities.z)/a.absorption.w))*(end-start);
    }
    return glm::vec3(glm::exp(-optical)*(includeDiskVisibility?visibility:1.0));
}
GpuAtmosphere::GpuAtmosphere(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& directory):resources_(std::move(d)){
    using namespace rhi;static_assert(sizeof(AtmosphereSettings)==96,"Atmosphere std140 ABI");
    const uint32_t widths[]={256,200,32,200},heights[]={64,100,32,100};
    for(uint32_t i=0;i<4;++i){textures_[i]=resources_.texture({widths[i],heights[i],Format::RGBA32Float,TextureUsage::Storage|TextureUsage::Sampled|TextureUsage::CopySource,"Atmosphere LUT"});views_[i]=resources_.view(textures_[i]);}
    parameters_=resources_.buffer({96,BufferUsage::Uniform|BufferUsage::CopyDestination,"Atmosphere parameters"});sampler_=resources_.sampler({Filter::Linear,AddressMode::ClampToEdge});skySampler_=resources_.sampler({Filter::Linear,AddressMode::Repeat});
    for(const char* name:{"transmittance","skyview","multi","convolution"}){
        std::string path=directory+"/atmosphere-"+name+".comp";ComputePipelineDesc p;p.shader={path+".glsl",path+".metallib",path+".spv",path+".json","main0"};
        auto r=nlohmann::json::parse(readShaderText(p.shader.reflectionPath));p.threads=r.at("entryPoints").at(0).at("workgroup_size").get<std::array<uint32_t,3>>();p.bindings={{0,{}},{1,{}}};
        for(const auto& u:r.value("ubos",nlohmann::json::array())){p.bindings[0].entries.push_back({u.at("binding"),BindingType::UniformBuffer,ShaderStage::Compute,u.at("name"),u.at("block_size")});if(u.at("name")=="ATMOS" && u.at("block_size")!=96)throw std::invalid_argument("Atmosphere reflected ABI differs");if(u.at("name")=="LutParameters" && u.at("block_size")!=64)throw std::invalid_argument("Atmosphere LUT parameters ABI differs");}
        for(const auto& t:r.value("textures",nlohmann::json::array()))p.bindings[1].entries.push_back({t.at("binding"),BindingType::SampledTexture,ShaderStage::Compute,t.at("name"),0});
        for(const auto& t:r.at("images"))p.bindings[1].entries.push_back({t.at("binding"),BindingType::StorageTextureWrite,ShaderStage::Compute,t.at("name"),0});
        kernels_[name]={resources_.computePipeline(p),p.bindings,p.threads};
    }
}
void GpuAtmosphere::update(const AtmosphereSettings& settings,float angle) {
    SunState sun;float radians=glm::radians(angle);sun.direction={0,std::sin(radians),-std::cos(radians)};sun.irradiance=glm::vec3(settings.radii.x);update(settings,sun);
}
void GpuAtmosphere::update(const AtmosphereSettings& settings,const SunState& input){
    SunState sun=input;const auto* values=reinterpret_cast<const float*>(&settings);for(size_t i=0;i<24;++i)if(!std::isfinite(values[i]))throw std::invalid_argument("Atmosphere nonfinite parameter");
    for(float x:{sun.direction.x,sun.direction.y,sun.direction.z,sun.observerHeightKm,sun.irradiance.x,sun.irradiance.y,sun.irradiance.z,sun.multipleScattering,sun.groundAlbedo})if(!std::isfinite(x))throw std::invalid_argument("Sun nonfinite parameter");
    if(settings.radii.x<0||settings.radii.y<=0||settings.radii.y>=.1f||settings.radii.z<=settings.radii.w||settings.radii.w<=0||settings.densities.x<=0||settings.densities.y<=0||settings.absorption.w<=0||std::abs(settings.densities.w)>=1||glm::dot(sun.direction,sun.direction)<1e-10f||sun.observerHeightKm<.001f||sun.observerHeightKm>=settings.radii.z-settings.radii.w||sun.multipleScattering<0||sun.groundAlbedo<0||sun.groundAlbedo>1)throw std::invalid_argument("Atmosphere invalid domain");
    for(int i=0;i<3;++i)if(settings.rayleigh[i]<0||settings.mie[i]<0||settings.extinction[i]<settings.mie[i]||settings.absorption[i]<0||sun.irradiance[i]<0)throw std::invalid_argument("Atmosphere negative coefficient or extinction below scattering");
    sun.direction=glm::normalize(sun.direction);
    const bool physical=!initialized_||std::memcmp(&settings,&previous_,sizeof(settings));
    const bool multi=physical||sun.groundAlbedo!=previousSun_.groundAlbedo;
    const bool sky=multi||sun.direction!=previousSun_.direction||sun.observerHeightKm!=previousSun_.observerHeightKm||sun.irradiance!=previousSun_.irradiance||sun.multipleScattering!=previousSun_.multipleScattering;
    if(!sky)return;
    resources_.device->writeBuffer(parameters_,0,96,&settings);Resources frame(resources_.device);auto commands=resources_.device->createCommandList();
    const char* names[]={"transmittance","skyview","multi","convolution"};const int widths[]={256,200,32,200},heights[]={64,100,32,100};
    for(uint32_t i:{0u,2u,1u,3u}){
        if((i==0 && !physical)||(i==2 && !multi))continue;
        const auto& kernel=kernels_.at(names[i]);struct alignas(16) Params{glm::ivec4 dims;glm::vec4 directionHeight,colorMultiple,features;};static_assert(sizeof(Params)==64,"LUT parameter ABI");
        Params p{{widths[i],heights[i],256,64},glm::vec4(sun.direction,sun.observerHeightKm),glm::vec4(sun.irradiance,sun.multipleScattering),{sun.groundAlbedo,0,0,0}};
        auto data=frame.buffer({sizeof(p),rhi::BufferUsage::Uniform,"Atmosphere LUT dimensions"},&p);std::vector<rhi::BindingEntry> buffers,images;
        for(const auto& e:kernel.layouts[0].entries)buffers.push_back({e.binding,e.binding==0?parameters_:data,0,e.minimumSize,{},{}});
        for(const auto& e:kernel.layouts[1].entries){const bool sampled=e.type==rhi::BindingType::SampledTexture;const uint32_t source=e.binding==2?2:(i==3?1:0);images.push_back({e.binding,{},0,0,sampled?views_[source]:views_[i],sampled?(i==3?skySampler_:sampler_):rhi::SamplerHandle{}});}
        auto a=frame.bindings({kernel.layouts[0],buffers}),b=frame.bindings({kernel.layouts[1],images});
        commands.dispatch(kernel.pipeline,{a,b},{(uint32_t(widths[i])+kernel.threads[0]-1)/kernel.threads[0],(uint32_t(heights[i])+kernel.threads[1]-1)/kernel.threads[1],1});
        if(i==0)++counts_.transmittance;if(i==2)++counts_.multiple;if(i==1)++counts_.sky;
    }
    resources_.device->submit(commands);initialized_=true;previous_=settings;previousSun_=sun;
}
}
