#include "renderer/rhi/GpuWaveParticles.h"
#include <algorithm>
#include <array>
namespace render {
GpuWaveParticles::GpuWaveParticles(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& dir,uint32_t n):resources_(d),size_(n){
    using namespace rhi;if(n<32||n>512||n%8)throw std::invalid_argument("Wave particle grid resolution");
    texture_=resources_.texture({n,n,Format::RGBA32Float,TextureUsage::Storage|TextureUsage::Sampled|TextureUsage::CopySource,"Combined shore / wave particle height"});view_=resources_.view(texture_);
    previous_=resources_.texture({n,n,Format::RGBA32Float,TextureUsage::Sampled|TextureUsage::CopyDestination|TextureUsage::CopySource,"Previous wave particle height"});previousView_=resources_.view(previous_);
    sampler_=resources_.sampler({Filter::Nearest,AddressMode::ClampToEdge});
    layouts_={{0,{{0,BindingType::UniformBuffer,ShaderStage::Compute,"WaveParameters",32},{1,BindingType::SampledTexture,ShaderStage::Compute,"shore",0}}},
        {1,{{0,BindingType::StorageRead,ShaderStage::Compute,"Particles",16},{1,BindingType::StorageRead,ShaderStage::Compute,"Bins",8},{2,BindingType::StorageRead,ShaderStage::Compute,"References",4},{3,BindingType::StorageTextureWrite,ShaderStage::Compute,"result",0}}}};
    std::string p=dir+"/water-particles.comp";pipeline_=resources_.computePipeline({{p+".glsl",p+".metallib",p+".spv",p+".json","main0"},layouts_,{8,8,1},"Wave particle compact kernel reconstruction"});
}
void GpuWaveParticles::simulate(double seconds,const WaveParticleSettings& settings,glm::vec4 patch,rhi::TextureViewHandle shore,bool hasShore){
    using namespace rhi;settings.validate();if(!std::isfinite(patch.x)||!std::isfinite(patch.y)||!std::isfinite(patch.z)||!std::isfinite(patch.w)||patch.z<=0||patch.w<=0||std::abs(patch.z-patch.w*size_)>patch.z*.001f)throw std::invalid_argument("Wave particle patch");
    bool reset=!initialized_||seconds<time_||seconds-time_>settings.lifetime;simulation_.advance(seconds,settings);
    double tick=std::floor(seconds*60+1e-7);uint32_t particleCount=uint32_t(simulation_.particles().size());
    if(!reset&&!hasShore&&tick==tick_&&patch==patch_&&particleCount==particleCount_){
        previousPatch_=patch_;time_=seconds;auto c=resources_.device->createCommandList();c.copyTexture(texture_,previous_);resources_.device->submit(c);return;
    }
    tick_=tick;particleCount_=particleCount;
    previousPatch_=reset?patch:patch_;patch_=patch;time_=seconds;
    uint32_t tiles=(size_+15)/16;std::vector<glm::vec4> particles;std::vector<glm::uvec2> headers(tiles*tiles,glm::uvec2(0));
    std::vector<std::array<int,4>> ranges;
    for(auto& p:simulation_.particles()){
        auto low=glm::ivec2(glm::floor((p.position-glm::vec2(p.radius)-glm::vec2(patch_))/(patch_.w*16)));
        auto high=glm::ivec2(glm::floor((p.position+glm::vec2(p.radius)-glm::vec2(patch_))/(patch_.w*16)));
        if(high.x<0||high.y<0||low.x>=int(tiles)||low.y>=int(tiles))continue;
        low=glm::clamp(low,glm::ivec2(0),glm::ivec2(tiles-1));high=glm::clamp(high,glm::ivec2(0),glm::ivec2(tiles-1));
        ranges.push_back({low.x,low.y,high.x,high.y});particles.push_back({p.position,p.amplitude,p.radius});
        for(int y=low.y;y<=high.y;y++)for(int x=low.x;x<=high.x;x++)++headers[y*tiles+x].y;
    }
    uint32_t count=0;for(auto& h:headers){h.x=count;count+=h.y;}references_=count;
    std::vector<uint32_t> refs(std::max(count,1u)),cursor(headers.size());
    for(uint32_t i=0;i<ranges.size();i++){auto r=ranges[i];for(int y=r[1];y<=r[3];y++)for(int x=r[0];x<=r[2];x++){auto b=y*tiles+x;refs[headers[b].x+cursor[b]++]=i;}}
    if(particles.empty())particles.push_back(glm::vec4(0));
    Resources frame(resources_.device);struct alignas(16) Parameters{glm::vec4 patch;glm::uvec4 mode;};Parameters p{patch_,{size_,tiles,hasShore?1u:0u,0}};
    auto data=frame.buffer({sizeof(p),BufferUsage::Uniform,"Wave reconstruction parameters"},&p);
    auto points=frame.buffer({particles.size()*16,BufferUsage::Storage,"Visible wave particles"},particles.data());
    auto bins=frame.buffer({headers.size()*8,BufferUsage::Storage,"Wave support tile ranges"},headers.data());
    auto indices=frame.buffer({refs.size()*4,BufferUsage::Storage,"Wave support tile references"},refs.data());
    auto a=frame.bindings({layouts_[0],{{0,data,0,sizeof(p),{},{}},{1,{},0,0,shore,sampler_}}});
    auto b=frame.bindings({layouts_[1],{{0,points,0,particles.size()*16,{},{}},{1,bins,0,headers.size()*8,{},{}},{2,indices,0,refs.size()*4,{},{}},{3,{},0,0,view_,{}}}});
    auto c=resources_.device->createCommandList();if(!reset)c.copyTexture(texture_,previous_);c.setLabel("water/wave-particles");c.dispatch(pipeline_,{a,b},{size_/8,size_/8,1});if(reset)c.copyTexture(texture_,previous_);resources_.device->submit(c);initialized_=true;
}
}
