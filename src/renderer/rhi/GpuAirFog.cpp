#include "renderer/rhi/GpuAirFog.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "rhi/ShaderAssets.h"
#include "renderer/rhi/ShadowRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
namespace render {
namespace {
struct alignas(16) Parameters {glm::mat4 inverseVP;glm::vec4 camera,settings,colour,aerial;};
rhi::ShaderAsset asset(const std::string& d,const char* n){auto p=d+"/"+n;return {p+".glsl",p+".metallib",p+".spv",p+".json","main0"};}
}
AerialParameters aerialParameters(const FrameData& f,const SunState& sun){
    static_assert(sizeof(AerialParameters)==160 && offsetof(AerialParameters,visibility)==144,"Aerial perspective std140 ABI");
    auto sunLight=std::find_if(f.lights.begin(),f.lights.end(),[](const auto& l){return l.positionType.w==0;});
    const float sunIndex=sunLight==f.lights.end()?-1.f:float(sunLight-f.lights.begin());
    return {f.atmosphere,glm::vec4(sun.direction,sun.multipleScattering),glm::vec4(sun.irradiance,f.sky&&f.weather.enabled&&f.weather.aerialPerspective?1.f:0.f),
        {f.seaLevelMeters,f.weather.aerialStartDistance,f.weather.aerialMaxDistance,f.weather.aerialDistanceScale},{sunIndex,f.weather.aerialShadows&&f.directionalEnabled?1.f:0.f,0,0}};
}
struct GpuAirFog::Targets {
    Resources resources;uint32_t width,height;rhi::TextureHandle source,radiance,transmission,solarLoss;rhi::TextureViewHandle sourceView,radianceView,transmissionView,solarLossView;
    Targets(std::shared_ptr<rhi::GraphicsDevice> d,uint32_t w,uint32_t h):resources(d),width(w),height(h){
        using namespace rhi;
        source=resources.texture({w,h,Format::RGBA16Float,TextureUsage::CopyDestination|TextureUsage::Sampled,"Air composition HDR snapshot"});sourceView=resources.view(source);
        const auto usage=TextureUsage::Storage|TextureUsage::Sampled|TextureUsage::CopySource;
        radiance=resources.texture({32,1024,Format::RGBA16Float,usage,"Aerial perspective scattered radiance 32 cubed"});radianceView=resources.view(radiance);
        transmission=resources.texture({32,1024,Format::RGBA16Float,usage,"Aerial perspective RGB transmission 32 cubed"});transmissionView=resources.view(transmission);
        solarLoss=resources.texture({32,1024,Format::RGBA16Float,usage,"Aerial locally occluded solar radiance"});solarLossView=resources.view(solarLoss);
    }
};
GpuAirFog::~GpuAirFog()=default;
GpuAirFog::GpuAirFog(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& dir):resources_(d){
    using namespace rhi;GraphicsPipelineDesc p;p.vertex=asset(dir,"tonemap.vert");p.fragment=asset(dir,"air-fog.frag");p.vertexStride=16;p.attributes={{0,VertexFormat::Float2,0},{1,VertexFormat::Float2,8}};p.colorFormat=Format::RGBA16Float;p.label="Atmospheric aerial perspective and height fog";
    layout_={0,{{0,BindingType::UniformBuffer,ShaderStage::Fragment,"AirFogParameters",sizeof(Parameters)},{1,BindingType::SampledTexture,ShaderStage::Fragment,"airSceneDepth",0},
        {2,BindingType::SampledTexture,ShaderStage::Fragment,"airSceneColour",0},{3,BindingType::SampledTexture,ShaderStage::Fragment,"aerialRadiance",0},{4,BindingType::SampledTexture,ShaderStage::Fragment,"aerialTransmission",0},{5,BindingType::SampledTexture,ShaderStage::Fragment,"aerialSolarLoss",0},{6,BindingType::SampledTexture,ShaderStage::Fragment,"airSceneMotion",0}}};
    p.bindings={layout_};pipeline_=resources_.pipeline(p);sampler_=resources_.sampler({Filter::Nearest,AddressMode::ClampToEdge});linear_=resources_.sampler({Filter::Linear,AddressMode::ClampToEdge});
    aerialLayouts_={{0,{{0,BindingType::UniformBuffer,ShaderStage::Compute,"AerialParameters",sizeof(AerialParameters)},{1,BindingType::UniformBuffer,ShaderStage::Compute,"AirFogParameters",sizeof(Parameters)},{2,BindingType::UniformBuffer,ShaderStage::Compute,"ShadowData",sizeof(ShadowParameters)},{3,BindingType::SampledTexture,ShaderStage::Compute,"aerialShadowAtlas",0}}},
        {1,{{0,BindingType::StorageTextureWrite,ShaderStage::Compute,"aerialRadiance",0,Format::RGBA16Float},{1,BindingType::StorageTextureWrite,ShaderStage::Compute,"aerialTransmission",0,Format::RGBA16Float},
        {2,BindingType::SampledTexture,ShaderStage::Compute,"aerialTransmittance",0},{3,BindingType::SampledTexture,ShaderStage::Compute,"aerialMultiple",0},{4,BindingType::StorageTextureWrite,ShaderStage::Compute,"aerialSolarLoss",0,Format::RGBA16Float}}}};
    aerialPipeline_=resources_.computePipeline({asset(dir,"aerial-perspective.comp"),aerialLayouts_,{8,8,1},"Aerial perspective camera froxel bake"});
    const float q[]={-1,-1,0,1,1,-1,1,1,1,1,1,0,-1,-1,0,1,1,1,1,0,-1,1,0,0};quad_=resources_.buffer({sizeof(q),BufferUsage::Vertex,"Air fog fullscreen triangle pair"},q);
}
void GpuAirFog::record(Resources& t,rhi::CommandList& c,const FrameData& f,rhi::TextureHandle hdr,rhi::TextureViewHandle hdrView,rhi::TextureViewHandle depth,rhi::TextureViewHandle motion,const SunState& sun,rhi::TextureViewHandle transmittance,rhi::TextureViewHandle multiple,rhi::BufferHandle shadows,rhi::TextureViewHandle shadowAtlas){
    using namespace rhi;f.weather.validate();const bool aerial=f.sky&&f.weather.aerialPerspective&&f.weather.aerialDistanceScale>0;
    if(!f.weather.enabled||(!f.weather.fog&&!aerial))return;
    // Air seen through the underwater interface is handled in the air capture.
    for(const auto& water:f.oceans){
        const float domain=water.surfaceLength>0?water.surfaceLength:water.spectrum.length;
        if(!water.underwaterView||f.cameraPosition.y>=water.seaLevel||std::abs(f.cameraPosition.x)>=domain*.5f||std::abs(f.cameraPosition.z)>=domain*.5f)continue;
        bool wet=true;
        if(water.waterMask){const auto& mask=*water.waterMask;const float u=f.cameraPosition.x/domain+.5f,v=.5f-f.cameraPosition.z/domain;
            if(mask.width&&mask.height&&mask.pixels.size()==size_t(mask.width)*mask.height*4){const auto x=std::min(mask.width-1,uint32_t(u*mask.width)),y=std::min(mask.height-1,uint32_t(v*mask.height));wet=mask.pixels[(size_t(y)*mask.width+x)*4]>=128;}}
        if(wet)return;
    }
    if(!targets_||targets_->width!=f.viewportWidth||targets_->height!=f.viewportHeight)targets_=std::make_unique<Targets>(resources_.device,f.viewportWidth,f.viewportHeight);
    glm::dmat4 translation(1);translation[3]=glm::dvec4(f.cameraPosition,1);
    const float extinction=f.weather.fog?f.weather.dryExtinction+f.weather.rainExtinction*std::clamp(f.rainMetersPerSecond*3600000/50.f,0.f,1.f):0.f;
    Parameters p{glm::mat4(glm::inverse(glm::dmat4(f.viewProjection)*translation)),glm::vec4(f.cameraPosition,f.farPlane),{extinction,f.weather.height,f.oceans.empty()?f.seaLevelMeters:f.oceans.front().seaLevel,-1e30f},glm::vec4(f.weather.fogColour,1),{aerial?f.weather.aerialMaxDistance:0.f,f.weather.aerialStartDistance,f.weather.aerialShadows?1.f:0.f,0}};
    auto b=t.buffer({sizeof(p),BufferUsage::Uniform,"Air fog frame parameters"},&p);
    if(aerial){
        const auto params=aerialParameters(f,sun);auto a=t.buffer({sizeof(params),BufferUsage::Uniform,"Aerial atmosphere parameters"},&params);
        auto uniforms=t.bindings({aerialLayouts_[0],{{0,a,0,sizeof(params),{},{}},{1,b,0,sizeof(p),{},{}},{2,shadows,0,sizeof(ShadowParameters),{},{}},{3,{},0,0,shadowAtlas,sampler_}}});
        auto images=t.bindings({aerialLayouts_[1],{{0,{},0,0,targets_->radianceView,{}},{1,{},0,0,targets_->transmissionView,{}},{2,{},0,0,transmittance,linear_},{3,{},0,0,multiple,linear_},{4,{},0,0,targets_->solarLossView,{}}}});
        c.setLabel("weather/aerial-perspective-lut");c.dispatch(aerialPipeline_,{uniforms,images},{4,128,1});
    }
    c.setLabel("weather/air-hdr-copy");c.copyTexture(hdr,targets_->source);
    auto set=t.bindings({layout_,{{0,b,0,sizeof(p),{},{}},{1,{},0,0,depth,sampler_},{2,{},0,0,targets_->sourceView,sampler_},
        {3,{},0,0,aerial?targets_->radianceView:targets_->sourceView,linear_},{4,{},0,0,aerial?targets_->transmissionView:targets_->sourceView,linear_},{5,{},0,0,aerial?targets_->solarLossView:targets_->sourceView,linear_},{6,{},0,0,motion,sampler_}}});
    c.setLabel("weather/air-fog");RenderPassDesc pass;pass.color=hdrView;pass.colorLoad=LoadOp::Load;c.beginRenderPass(pass);c.bindPipeline(pipeline_);c.bindBindingSet(set);c.bindVertexBuffer(quad_);c.draw(6);c.endRenderPass();
}
std::vector<float> GpuAirFog::readAerial(bool transmission) const {
    if(!targets_)throw std::logic_error("Aerial perspective has not rendered");
    return resources_.device->readTextureFloat(transmission?targets_->transmission:targets_->radiance);
}
}
