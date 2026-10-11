#include "renderer/rhi/GpuOcean.h"
#include "rhi/ShaderAssets.h"
#include "renderer/rhi/OceanSpectrum.h"
#include <json/json.hpp>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <cstring>
namespace render {
namespace {
void validate(const OceanSettings& s,float time) {
    s.seaState.validate();
    if(s.size<8 || s.size>2048 || (s.size&(s.size-1)) || !std::isfinite(s.length) || s.length<=0) throw std::invalid_argument("Ocean: invalid FFT grid/domain");
    for(float value:{time,s.amplitude,s.windSpeed,s.choppiness,s.heightScale,s.foamScale,s.foamThreshold,s.windDirection.x,s.windDirection.y,s.minWavelength,s.maxWavelength,s.targetRmsHeight,s.previousWind.x,s.previousWind.y,s.windBlend})
        if(!std::isfinite(value))throw std::invalid_argument("Ocean: nonfinite simulation parameter");
    if(s.minWavelength<0 || s.maxWavelength<0 || s.targetRmsHeight<0 ||
        ((s.minWavelength>0 || s.maxWavelength>0) && (s.minWavelength<2*s.length/s.size || s.maxWavelength<s.minWavelength*1.6f || s.maxWavelength>s.length)))
        throw std::invalid_argument("Ocean: invalid wavelength band/RMS height");
    if(s.windBlend<0||s.windBlend>1)throw std::invalid_argument("Ocean: invalid wind transition weight");
    if(s.amplitude<0 || s.windSpeed<0 || s.foamScale<0)throw std::invalid_argument("Ocean: negative spectrum/foam parameter");
}
}
GpuOcean::GpuOcean(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory,const OceanSettings& settings) : resources_(std::move(device)),directory_(directory),initial_(settings) {
    validate(settings,0);static_assert(sizeof(Parameters)==128,"Ocean parameter ABI");
    if(!resources_.device->computeLimits().maxStorageImages)throw std::invalid_argument("Ocean: storage images unavailable on this backend");
    const auto limits=resources_.device->computeLimits();sharedFFT_=settings.size<=512 && limits.maxInvocations>=256 && limits.maxThreads[0]>=256 && !std::getenv("SCENERENDERER_FFT_REFERENCE");
    using namespace rhi;
    const char* labels[]={"GaussianRandomRT","HeightSpectrumRT","DisplaceXSpectrumRT","DisplaceZSpectrumRT","FFT scratch","DisplaceRT","NormalRT","BubblesRT","InitialSpectrumRT"};
    for(size_t i=0;i<9;++i) { textures_[i]=resources_.texture({settings.size,settings.size,Format::RGBA32Float,TextureUsage::Storage|TextureUsage::Sampled|TextureUsage::CopySource|TextureUsage::CopyDestination,labels[i]});views_[i]=resources_.view(textures_[i]); }
    for(const char* name:{"gaussian","initial","height","displacementspectrum","horizontal","vertical","displacement","normalfoam"}) {
        const bool sharedAxis=sharedFFT_ && (std::string(name)=="horizontal"||std::string(name)=="vertical");
        const auto path=directory+"/ocean-"+name+(sharedAxis?"-shared":"")+".comp";ComputePipelineDesc desc;desc.shader={path+".glsl",path+".metallib",path+".spv",path+".json","main0"};desc.threads={8,8,1};desc.label=std::string("Ocean ")+name;
        const auto reflection=nlohmann::json::parse(readShaderText(desc.shader.reflectionPath));
        desc.threads=reflection.at("entryPoints").at(0).at("workgroup_size").get<std::array<uint32_t,3>>();
        for(const auto& ubo:reflection.at("ubos")) {
            if(ubo.at("name")!="OceanParameters" || ubo.at("block_size")!=128 || ubo.at("set")!=0 || ubo.at("binding")!=0)throw std::invalid_argument("Ocean: shader parameter layout mismatch");
        }
        for(const auto& ubo:reflection.at("ubos")){const auto& members=reflection.at("types").at(ubo.at("type").get<std::string>()).at("members");for(size_t i=0;i<8;++i)if(members.at(i).at("offset")!=i*16)throw std::invalid_argument("Ocean: parameter member offset mismatch");}
        desc.bindings={{0,{{0,BindingType::UniformBuffer,ShaderStage::Compute,"OceanParameters",128}}},{1,{}}};
        for(const auto& image:reflection.at("images")) {
            const auto type=image.value("readonly",false)?BindingType::StorageTextureRead:image.value("writeonly",false)?BindingType::StorageTextureWrite:BindingType::StorageTextureReadWrite;
            desc.bindings[1].entries.push_back({image.at("binding"),type,ShaderStage::Compute,image.at("name"),0});
        }
        auto pipeline=resources_.computePipeline(desc);kernels_.emplace(name,Kernel{pipeline,desc.bindings});
    }
}
struct GpuOcean::SlopeMoments {
    Resources resources; rhi::TextureHandle texture; rhi::TextureViewHandle all;
    std::vector<std::array<rhi::TextureViewHandle,3>> levels;
    std::array<rhi::ComputePipelineHandle,2> pipelines;
    std::array<rhi::BindingLayout,2> layouts;
    SlopeMoments(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& dir,uint32_t size):resources(device){
        using namespace rhi;uint32_t count=1;for(uint32_t n=size;n>1;n/=2)++count;
        texture=resources.texture({size,size,Format::RGBA32Float,TextureUsage::Storage|TextureUsage::Sampled|TextureUsage::CopySource,"Ocean slope moments",count,3});
        all=resources.view(texture,{0,count,0,3});
        for(uint32_t i=0;i<count;++i)levels.push_back({resources.view(texture,{i,1,0,1}),resources.view(texture,{i,1,1,1}),resources.view(texture,{i,1,2,1})});
        for(size_t i=0;i<2;++i){auto path=dir+(i?"/ocean-slope-reduce.comp":"/ocean-slope-init.comp");ComputePipelineDesc d;
            d.shader={path+".glsl",path+".metallib",path+".spv",path+".json","main0"};d.threads={8,8,1};d.label=i?"Ocean slope moment reduction":"Ocean slope moment conversion";
            layouts[i]={0,{}};
            if(i){for(uint32_t b=0;b<3;++b)layouts[i].entries.push_back({b,BindingType::StorageTextureRead,ShaderStage::Compute,b==0?"slopeInput":b==1?"crossInput":"normalInput",0});}
            else layouts[i].entries.push_back({0,BindingType::StorageTextureRead,ShaderStage::Compute,"sourceNormal",0});
            for(uint32_t b=0;b<3;++b)layouts[i].entries.push_back({uint32_t(i?3:1)+b,BindingType::StorageTextureWrite,ShaderStage::Compute,b==0?"slopeOutput":b==1?"crossOutput":"normalOutput",0});
            d.bindings={layouts[i]};pipelines[i]=resources.computePipeline(d);
        }
    }
};
GpuOcean::~GpuOcean()=default;
rhi::TextureViewHandle GpuOcean::slopeMoments() const {return slopeMoments_?slopeMoments_->all:rhi::TextureViewHandle{};}
rhi::TextureHandle GpuOcean::slopeMomentTexture() const {return slopeMoments_?slopeMoments_->texture:rhi::TextureHandle{};}
uint32_t GpuOcean::slopeMomentLevels() const {return slopeMoments_?uint32_t(slopeMoments_->levels.size()):0;}
void GpuOcean::buildSlopeMoments(){
    if(slopeMomentsValid_)return;
    if(!seeded_)throw std::logic_error("Ocean slope moments require simulated normals");
    if(!slopeMoments_)slopeMoments_=std::make_unique<SlopeMoments>(resources_.device,directory_,initial_.size);
    auto& m=*slopeMoments_;Resources frame(resources_.device);auto c=resources_.device->createCommandList();uint32_t n=initial_.size;
    for(size_t i=0;i<m.levels.size();++i){size_t stage=i?1:0;
        std::vector<rhi::BindingEntry> entries;
        if(i){for(uint32_t b=0;b<3;++b)entries.push_back({b,{},0,0,m.levels[i-1][b],{}});}else entries.push_back({0,{},0,0,views_[6],{}});
        for(uint32_t b=0;b<3;++b)entries.push_back({uint32_t(i?3:1)+b,{},0,0,m.levels[i][b],{}});
        auto set=frame.bindings({m.layouts[stage],entries});
        c.setLabel("fft"+std::to_string(initial_.size)+"/slope-mip"+std::to_string(i));
        c.dispatch(m.pipelines[stage],{set},{(n+7)/8,(n+7)/8,1u});n=std::max(1u,n/2);
    }
    resources_.device->submit(c);slopeMomentsValid_=true;
}
void GpuOcean::record(Resources& frame,rhi::CommandList& list,const char* name,const Parameters& parameters,const std::map<uint32_t,rhi::TextureViewHandle>& images) {
    const auto& kernel=kernels_.at(name);
    auto data=frame.buffer({sizeof(parameters),rhi::BufferUsage::Uniform,"Ocean dispatch parameters"},&parameters);
    auto params=frame.bindings({kernel.layouts[0],{{0,data,0,sizeof(parameters),{},{}}}});
    std::vector<rhi::BindingEntry> entries;for(const auto& image:images)entries.push_back({image.first,{},0,0,image.second,{}});
    auto bindings=frame.bindings({kernel.layouts[1],entries});list.setLabel("fft"+std::to_string(initial_.size)+"/"+name);const bool sharedAxis=sharedFFT_ && (std::string(name)=="horizontal"||std::string(name)=="vertical");
    list.dispatch(kernel.pipeline,{params,bindings},sharedAxis?std::array<uint32_t,3>{1,initial_.size,1}:std::array<uint32_t,3>{initial_.size/8,initial_.size/8,1});
}
void GpuOcean::recordFFT(Resources& frame,rhi::CommandList& list,Parameters parameters,rhi::TextureHandle& texture,rhi::TextureViewHandle& view) {
    if(sharedFFT_) {
        for(const char* axis:{"horizontal","vertical"}){record(frame,list,axis,parameters,{{5,view},{6,views_[4]}});std::swap(texture,textures_[4]);std::swap(view,views_[4]);}
        return;
    }
    for(const char* axis:{"horizontal","vertical"})for(uint32_t ns=1;ns<initial_.size;ns*=2) {
        parameters.grid.y=int(ns);record(frame,list,axis,parameters,{{5,view},{6,views_[4]}});
        std::swap(texture,textures_[4]);std::swap(view,views_[4]);
    }
}
float GpuOcean::seaAmplitude(const OceanSettings& s,size_t component) {
    if(!seaCached_[component]||!oceanSpectrum::same(s,seaSettings_[component])){
        seaAmplitude_[component]=float(oceanSpectrum::normalization(s,component==2));
        seaSettings_[component]=s;seaCached_[component]=true;
    }
    return seaAmplitude_[component];
}
float GpuOcean::spectralAmplitude(const OceanSettings& settings,size_t endpoint) {
    const float windLength=glm::length(settings.windDirection);
    float amplitude=settings.amplitude;
    if(settings.minWavelength>0){
        // Cache discrete spectral normalization. It changes with wind/band/RMS,
        // not with simulation time; no per-frame CPU spectral integration.
        const auto& old=amplitudeSettings_[endpoint];
        if(!amplitudeCached_[endpoint] || old.windSpeed!=settings.windSpeed || old.windDirection!=settings.windDirection ||
           old.minWavelength!=settings.minWavelength || old.maxWavelength!=settings.maxWavelength ||
           old.targetRmsHeight!=settings.targetRmsHeight || old.heightScale!=settings.heightScale){
            constexpr double pi=3.141592653589793;
            const double dk=2*pi/settings.length,L=settings.windSpeed*settings.windSpeed/9.81;
            const double low=2*pi/settings.maxWavelength,high=2*pi/settings.minWavelength;
            auto smooth=[](double a,double b,double x){double t=std::clamp((x-a)/(b-a),0.,1.);return t*t*(3-2*t);};
            double energy=0;
            if(L>0 && windLength>1e-6f)for(uint32_t y=1;y<settings.size;++y)for(uint32_t x=1;x<settings.size;++x){
                const double kx=(int(x)-int(settings.size/2))*dk,kz=(int(y)-int(settings.size/2))*dk,k2=kx*kx+kz*kz;
                if(k2==0)continue;const double k=std::sqrt(k2),alignment=(kx*settings.windDirection.x+kz*settings.windDirection.y)/(k*windLength);
                const double band=smooth(low,low*1.25,k)*(1-smooth(high*.8,high,k));
                energy+=band*std::exp(-1/(k2*L*L))/(k2*k2)*alignment*alignment*std::exp(-k2*L*L*1e-6);
            }
            const double variance=2*dk*dk*settings.heightScale*settings.heightScale*energy;
            normalizedAmplitude_[endpoint]=variance>0?float(settings.targetRmsHeight*settings.targetRmsHeight/variance):0;
            amplitudeSettings_[endpoint]=settings;amplitudeCached_[endpoint]=true;
        }
        amplitude=settings.amplitude>0?normalizedAmplitude_[endpoint]:0;
    }
    return amplitude;
}
void GpuOcean::simulate(float seconds,const OceanSettings& settings) {
    slopeMomentsValid_=false;validate(settings,seconds);if(settings.size!=initial_.size || settings.length!=initial_.length)throw std::invalid_argument("Ocean: grid/domain changes require recreation");
    const bool sea=settings.seaState.enabled;
    const float windLength=glm::length(settings.windDirection);const auto wind=windLength>1e-6f?settings.windDirection/windLength*(sea?1.f:settings.windSpeed):glm::vec2(0);
    const float amplitude=sea?seaAmplitude(settings,0):spectralAmplitude(settings,0);
    auto source=settings;source.windDirection=settings.previousWind;source.windSpeed=glm::length(settings.previousWind);
    const float sourceAmplitude=settings.windTransition?(sea?seaAmplitude(source,1):spectralAmplitude(source,1)):amplitude;
    Parameters p{{int(settings.size),0,0,settings.seed},{seconds,amplitude,settings.length,settings.windTransition?1.f:0.f},
        {wind,settings.minWavelength,settings.maxWavelength},{settings.choppiness,settings.heightScale,settings.foamScale,settings.foamThreshold},
        {settings.previousWind,settings.windBlend,sourceAmplitude},
        {sea?1.f:0.f,settings.seaState.depth,settings.seaState.windPeriod,settings.seaState.windGamma},
        {settings.seaState.windSpread,settings.seaState.swellSpread,settings.seaState.swellPeriod,0},
        {settings.seaState.swellDirection,sea?seaAmplitude(settings,2):0,0}};
    Resources frame(resources_.device);auto list=resources_.device->createCommandList();
    if(!seeded_ || seed_!=settings.seed)record(frame,list,"gaussian",p,{{1,views_[0]}});
    // Cache the two complex h0 coefficients. Time, choppiness and foam only
    // affect later kernels; normalized spectral amplitudes are already in p.
    auto key=p;key.spectrum.x=0;key.displacement=glm::vec4(0);
    const bool rebuild=!spectrumCached_ || std::memcmp(&key,&spectrumKey_,sizeof(key))!=0 || std::getenv("SCENERENDERER_OCEAN_UNCACHED");
    if(rebuild)record(frame,list,"initial",p,{{1,views_[0]},{2,views_[8]}});
    record(frame,list,"height",p,{{1,views_[8]},{2,views_[1]}});
    record(frame,list,"displacementspectrum",p,{{2,views_[1]},{3,views_[2]},{4,views_[3]}});
    for(size_t i=1;i<=3;++i)recordFFT(frame,list,p,textures_[i],views_[i]);
    record(frame,list,"displacement",p,{{2,views_[1]},{3,views_[2]},{4,views_[3]},{7,views_[5]}});
    record(frame,list,"normalfoam",p,{{7,views_[5]},{5,views_[6]},{6,views_[7]}});
    resources_.device->submit(list);seed_=settings.seed;seeded_=true;
    if(rebuild){spectrumKey_=key;spectrumCached_=true;++initialSpectrumBuilds_;}
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
