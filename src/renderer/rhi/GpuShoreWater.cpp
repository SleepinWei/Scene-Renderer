#include "renderer/rhi/GpuShoreWater.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace render {
GpuShoreWater::GpuShoreWater(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& dir,const OceanSurfaceSettings& s,rhi::TextureViewHandle bed)
    :resources_(d),initial_(s),bed_(bed) {
    using namespace rhi;
    const auto n=s.shore.resolution;
    for(float v:{s.shore.length,s.shore.maxDepth,s.shore.swellHeight,s.shore.swellPeriod,s.shore.swellDirection.x,s.shore.swellDirection.y,
                 s.shore.foamStrength,s.shore.foamLifetime,s.shore.dryingTime,s.seaLevel})
        if(!std::isfinite(v))throw std::invalid_argument("Nonfinite shallow water configuration");
    if(n<32||n>512||n%8||s.shore.length/n<.125f||s.shore.maxDepth<=0||s.shore.maxDepth>32)throw std::invalid_argument("Invalid shallow water patch");
    if(s.shore.swellHeight<0||s.shore.swellHeight>2||s.shore.swellPeriod<1||s.shore.foamStrength<0||s.shore.foamLifetime<=0||s.shore.dryingTime<=0)
        throw std::invalid_argument("Invalid shallow water forcing or history");
    static_assert(sizeof(Parameters)==256,"Shore solver std140 ABI");
    sampler_=resources_.sampler({Filter::Linear,AddressMode::ClampToEdge});repeat_=resources_.sampler({Filter::Linear,AddressMode::Repeat});
    for(uint32_t i=0;i<2;++i) {
        states_[i]=resources_.texture({n,n,Format::RGBA32Float,TextureUsage::Storage|TextureUsage::Sampled|TextureUsage::CopySource|TextureUsage::CopyDestination,"Shore H/Hu/Hv/bed"});stateViews_[i]=resources_.view(states_[i]);
        foams_[i]=resources_.texture({n,n,Format::RGBA32Float,TextureUsage::Storage|TextureUsage::Sampled|TextureUsage::CopySource,"Shore foam/wetness/depth/bed"});foamViews_[i]=resources_.view(foams_[i]);
    }
    previous_=resources_.texture({n,n,Format::RGBA32Float,TextureUsage::Sampled|TextureUsage::CopyDestination,"Previous shore state"});previousView_=resources_.view(previous_);
    const std::string p=dir+"/water-shore.comp";
    layouts_={{0,{{0,BindingType::UniformBuffer,ShaderStage::Compute,"ShoreParameters",256},
        {1,BindingType::SampledTexture,ShaderStage::Compute,"shoreBed",0},{2,BindingType::SampledTexture,ShaderStage::Compute,"shoreFFT",0},{3,BindingType::SampledTexture,ShaderStage::Compute,"oldFFT",0}}},
        {1,{{0,BindingType::StorageTextureRead,ShaderStage::Compute,"oldState",0},{1,BindingType::StorageTextureWrite,ShaderStage::Compute,"newState",0},
             {2,BindingType::SampledTexture,ShaderStage::Compute,"oldFoam",0},{3,BindingType::StorageTextureWrite,ShaderStage::Compute,"newFoam",0}}}};
    ComputePipelineDesc desc{{p+".glsl",p+".metallib",p+".spv",p+".json","main0"},layouts_,{8,8,1},"Shallow water finite volume / foam"};pipeline_=resources_.computePipeline(desc);
}
bool GpuShoreWater::compatible(const OceanSurfaceSettings& s) const {
    return s.shore.resolution==initial_.shore.resolution && s.shore.length==initial_.shore.length && s.shore.maxDepth==initial_.shore.maxDepth && s.seaLevel==initial_.seaLevel && s.bathymetryModel==initial_.bathymetryModel;
}
void GpuShoreWater::dispatch(Resources& frame,rhi::CommandList& c,const Parameters& p,uint32_t input,uint32_t output,rhi::TextureViewHandle fft,rhi::TextureViewHandle oldFFT) {
    using namespace rhi;auto data=frame.buffer({sizeof(p),BufferUsage::Uniform,"Shore step parameters"},&p);
    auto a=frame.bindings({layouts_[0],{{0,data,0,sizeof(p),{},{}},{1,{},0,0,bed_,sampler_},{2,{},0,0,fft,repeat_},{3,{},0,0,oldFFT,repeat_}}});
    auto b=frame.bindings({layouts_[1],{{0,{},0,0,stateViews_[input],{}},{1,{},0,0,stateViews_[output],{}},{2,{},0,0,foamViews_[input],sampler_},{3,{},0,0,foamViews_[output],{}}}});
    c.setLabel(p.mode.x==0?"shore/init":p.mode.x==1?"shore/scroll":"shore/step");
    c.dispatch(pipeline_,{a,b},{initial_.shore.resolution/8,initial_.shore.resolution/8,1});
}
void GpuShoreWater::simulate(float seconds,const OceanSurfaceSettings& s,glm::vec3 camera,rhi::TextureViewHandle fft,rhi::TextureViewHandle oldFFT) {
    if(!compatible(s)||!std::isfinite(seconds))throw std::invalid_argument("Shore water settings changed");
    const float dx=s.shore.length/s.shore.resolution;
    previousPatch_=patch_;
    glm::vec2 centre=glm::floor(glm::vec2(camera.x,camera.z)/dx)*dx;
    patch_={centre.x-s.shore.length*.5f,centre.y-s.shore.length*.5f,s.shore.length,dx};
    // CFL bound for the supported shallow coast presets: +/-6 m/s velocity,
    // capped still-water bed depth and 3 m of wave-height headroom. This is not
    // an adaptive bound for arbitrary dam-break/compression configurations.
    const float dt=.3f*dx/(2.f*(6.f+std::sqrt(9.81f*(s.shore.maxDepth+3.f))));
    const bool restart=!initialized_||seconds<previousTarget_||seconds-previousTarget_>2.f;
    auto direction=glm::length(s.shore.swellDirection)>.001f?glm::normalize(s.shore.swellDirection):glm::vec2(0,1);
    auto fftDirection=glm::length(s.spectrum.windDirection)>.001f?glm::normalize(s.spectrum.windDirection):direction;
    Parameters p{glm::inverse(s.bathymetryModel),s.bathymetryModel,patch_,previousPatch_,{0,0,0,0},
        {s.seaLevel,s.spectrum.length,s.shore.maxDepth,6},{direction,s.shore.swellHeight,s.shore.swellPeriod},
        {s.shore.foam? s.shore.foamStrength:0,s.shore.foamLifetime,s.shore.dryingTime,s.shore.wetSand?1.f:0.f},{s.shore.boundaryForcing?1.f:0.f,0,fftDirection},{0,0,0,0}};
    Resources frame(resources_.device);auto c=resources_.device->createCommandList();lastSteps_=0;
    if(initialized_)c.copyTexture(states_[current_],previous_);
    if(restart) {
        time_=std::max(0.,double(seconds)-2.);previousTarget_=time_;p.clock={float(time_),0,0,0};p.mode.x=0;
        dispatch(frame,c,p,current_,1-current_,fft,oldFFT);current_=1-current_;previousPatch_=patch_;
    } else if(patch_.x!=previousPatch_.x||patch_.y!=previousPatch_.y) {
        p.clock={float(time_),0,0,0};p.mode.x=1;dispatch(frame,c,p,current_,1-current_,fft,oldFFT);current_=1-current_;
    }
    while(time_+1e-6f<seconds) {
        const double step=std::min(double(dt),double(seconds)-time_);p.clock={float(time_+step),float(step),
            float(std::clamp((time_+step-previousTarget_)/std::max(double(seconds)-previousTarget_,1e-6),0.,1.)),0};p.mode.x=2;
        dispatch(frame,c,p,current_,1-current_,fft,oldFFT);current_=1-current_;time_+=step;++lastSteps_;
        if(lastSteps_>50000)throw std::runtime_error("Shore solver time interval too large");
    }
    if(restart)c.copyTexture(states_[current_],previous_);
    resources_.device->submit(c);
    initialized_=true;previousTarget_=seconds;
}
std::vector<float> GpuShoreWater::read(bool foam) const {return resources_.device->readTextureFloat(foam?foams_[current_]:states_[current_]);}
}
