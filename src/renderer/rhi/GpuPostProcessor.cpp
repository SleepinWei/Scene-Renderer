#include "renderer/rhi/GpuPostProcessor.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_inverse.hpp>
namespace render {
namespace {
rhi::ShaderAsset asset(const std::string& root,const char* name){auto p=root+"/"+name;return {p+".glsl",p+".metallib",p+".spv",p+".json","main0"};}
void bounded(float value,float lo,float hi){if(!std::isfinite(value)||value<lo||value>hi)throw std::invalid_argument("Post processing: invalid parameter");}
void matrix(const glm::mat4& m){for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(!std::isfinite(m[c][r]))throw std::invalid_argument("Post processing: nonfinite camera matrix");}
struct alignas(16) SpatialBlock {glm::mat4 inverseVP,view,previousVP,stableVP;glm::vec4 lens,motion;};
struct alignas(16) ToneBlock {glm::vec4 exposure,grading,effects;};
struct alignas(16) FinishBlock {glm::vec4 options,vignette;};
static_assert(sizeof(SpatialBlock)==288&&sizeof(ToneBlock)==48&&sizeof(FinishBlock)==32,"Post processing std140 ABI");
}
void validatePostProcessSettings(const PostProcessSettings& s){
    if(int(s.toneMapper)<0||int(s.toneMapper)>3)throw std::invalid_argument("Post processing: unknown tone mapper");
    bounded(s.exposureEV,-10,10);bounded(s.bloomThreshold,0,64);bounded(s.bloomKnee,0,16);
    bounded(s.bloomStrength,0,4);bounded(s.bloomScatter,0,1);
    bounded(s.focusDistance,.01f,10000);bounded(s.focusRange,.01f,10000);bounded(s.dofRadius,0,32);
    bounded(s.shutter,0,2);bounded(s.motionMaxPixels,0,64);
    bounded(s.saturation,0,2);bounded(s.contrast,0,2);bounded(s.temperature,-1,1);bounded(s.tint,-1,1);
    bounded(s.sharpness,0,1);bounded(s.vignetteStrength,0,1);bounded(s.vignetteRoundness,0,1);
    bounded(s.chromaticPixels,0,8);bounded(s.grainStrength,0,.2f);
}
struct GpuPostProcessor::Targets {
    Resources resources;uint32_t width,height;
    std::vector<rhi::TextureViewHandle> views,ups;
    Targets(std::shared_ptr<rhi::GraphicsDevice> d,uint32_t w,uint32_t h,int kind):resources(d),width(w),height(h){
        using namespace rhi;
        auto add=[&](uint32_t x,uint32_t y,const std::string& label){return resources.view(resources.texture({x,y,Format::RGBA16Float,TextureUsage::ColorAttachment|TextureUsage::Sampled,label}));};
        if(kind==0){
            uint32_t x=w,y=h;
            for(int i=0;i<6;++i){x=std::max(1u,(x+1)/2);y=std::max(1u,(y+1)/2);views.push_back(add(x,y,"Bloom down "+std::to_string(i)));if(x==1&&y==1)break;}
            x=w;y=h;for(size_t i=0;i+1<views.size();++i){x=std::max(1u,(x+1)/2);y=std::max(1u,(y+1)/2);ups.push_back(add(x,y,"Bloom up "+std::to_string(i)));}
        }else {views.push_back(add(w,h,kind==1?"Post spatial A":"Post display intermediate"));if(kind==1)views.push_back(add(w,h,"Post spatial B"));}
    }
};
GpuPostProcessor::GpuPostProcessor(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& dir):resources_(d){
    using namespace rhi;
    sampler_=resources_.sampler({Filter::Linear,AddressMode::ClampToEdge});depthSampler_=resources_.sampler({Filter::Nearest,AddressMode::ClampToEdge});
    const float quad[]={-1,-1,0,1,1,-1,1,1,1,1,1,0,-1,-1,0,1,1,1,1,0,-1,1,0,0};
    quad_=resources_.buffer({sizeof(quad),BufferUsage::Vertex,"Post fullscreen triangle pair"},quad);
    auto layout=[](const char* block,uint32_t bytes,bool second,const char* secondName){
        BindingLayout l{0,{{0,BindingType::UniformBuffer,ShaderStage::Fragment,block,bytes},{1,BindingType::SampledTexture,ShaderStage::Fragment,"sourceColor",0}}};
        if(second)l.entries.push_back({2,BindingType::SampledTexture,ShaderStage::Fragment,secondName,0});return l;
    };
    auto pipeline=[&](const char* shader,const char* label,BindingLayout l,Format format){GraphicsPipelineDesc p;
        p.vertex=asset(dir,"tonemap.vert");p.fragment=asset(dir,shader);p.vertexStride=16;p.attributes={{0,VertexFormat::Float2,0},{1,VertexFormat::Float2,8}};
        p.bindings={l};p.colorFormat=format;p.label=label;return resources_.pipeline(p);
    };
    bloomLayout_=layout("BloomParameters",16,true,"lowColor");down_=pipeline("post-bloom.frag","Post bloom",bloomLayout_,Format::RGBA16Float);up_=down_;
    spatialLayout_=layout("SpatialParameters",288,true,"sceneDepth");spatialPipeline_=pipeline("post-spatial.frag","Post lens",spatialLayout_,Format::RGBA16Float);
    toneLayout_=layout("PostToneParameters",48,true,"bloomColor");toneHdr_=pipeline("post-tone.frag","Post tone/grade intermediate",toneLayout_,Format::RGBA16Float);
    toneOutput_=pipeline("post-tone.frag","Post tone/grade output",toneLayout_,Format::RGBA8UNorm);
    finishLayout_=layout("PostFinishParameters",32,false,"");finish_=pipeline("post-finish.frag","Post display effects",finishLayout_,Format::RGBA8UNorm);
}
GpuPostProcessor::~GpuPostProcessor()=default;
void GpuPostProcessor::reset(){history_=false;pending_=false;}
void GpuPostProcessor::commit(){
    if(!pending_)return;
    previousVP_=pendingView_.unjitteredViewProjection;
    previousProjection_=previousVP_*glm::inverse(pendingView_.view);
    previousCamera_=pendingView_.cameraPosition;historyKey_=pendingView_.historyKey;
    historyWidth_=pendingWidth_;historyHeight_=pendingHeight_;history_=true;pending_=false;
}
void GpuPostProcessor::record(Resources& frame,rhi::CommandList& commands,uint32_t width,uint32_t height,
    rhi::TextureViewHandle hdr,rhi::TextureViewHandle depth,rhi::TextureViewHandle output,const PostProcessSettings& requested,
    const PostProcessView& view,float exposure,float gamma,bool toneMapping){
    pending_=false;
    if(!width||!height||!hdr||!output)throw std::invalid_argument("Post processing: missing targets");
    validatePostProcessSettings(requested);bounded(exposure,0,1e6);bounded(gamma,.1f,8);bounded(view.time,-1e8,1e8);
    matrix(view.view);matrix(view.viewProjection);matrix(view.unjitteredViewProjection);
    PostProcessSettings s=requested;if(!s.enabled)s=PostProcessSettings{};
    auto ensure=[&](std::unique_ptr<Targets>& t,int kind,bool enabled){if(!enabled){t.reset();return;}if(!t||t->width!=width||t->height!=height)t=std::make_unique<Targets>(resources_.device,width,height,kind);};
    const auto projection=view.unjitteredViewProjection*glm::inverse(view.view);matrix(projection);
    float projectionChange=0;for(int c=0;c<4;++c)for(int r=0;r<4;++r)projectionChange=std::max(projectionChange,std::abs(projection[c][r]-previousProjection_[c][r]));
    if(historyWidth_!=width||historyHeight_!=height)history_=false;
    if(!history_||historyKey_!=view.historyKey||projectionChange>1e-4f||glm::length(view.cameraPosition-previousCamera_)>2)history_=false;
    auto draw=[&](const char* label,rhi::PipelineHandle pipeline,rhi::BindingLayout layout,const void* p,size_t bytes,
                  rhi::TextureViewHandle source,rhi::TextureViewHandle second,rhi::TextureViewHandle target,bool depthImage=false){
        using namespace rhi;auto data=frame.buffer({bytes,BufferUsage::Uniform,label},p);
        std::vector<BindingEntry> entries{{0,data,0,bytes,{},{}},{1,{},0,0,source,sampler_}};
        if(layout.entries.size()>2)entries.push_back({2,{},0,0,second,depthImage?depthSampler_:sampler_});
        auto bindings=frame.bindings({layout,entries});commands.setLabel(label);RenderPassDesc pass;pass.color=target;
        commands.beginRenderPass(pass);commands.bindPipeline(pipeline);commands.bindBindingSet(bindings);commands.bindVertexBuffer(quad_);commands.draw(6);commands.endRenderPass();
    };
    const bool lens=depth&&((s.motionBlur&&s.shutter>0&&s.motionMaxPixels>0)||(s.depthOfField&&s.dofRadius>0));
    ensure(spatial_,1,lens);rhi::TextureViewHandle source=hdr;size_t spatialIndex=0;
    if(lens){SpatialBlock p{glm::inverse(view.viewProjection),view.view,history_?previousVP_:view.unjitteredViewProjection,view.unjitteredViewProjection,{s.focusDistance,s.focusRange,s.dofRadius,1},{s.shutter,s.motionMaxPixels,history_?1.f:0.f,0}};matrix(p.inverseVP);
        if(s.motionBlur&&s.shutter>0&&s.motionMaxPixels>0){draw("post-motion-blur",spatialPipeline_,spatialLayout_,&p,sizeof(p),source,depth,spatial_->views[spatialIndex],true);source=spatial_->views[spatialIndex++];}
        if(s.depthOfField&&s.dofRadius>0){p.lens.w=0;draw("post-depth-of-field",spatialPipeline_,spatialLayout_,&p,sizeof(p),source,depth,spatial_->views[spatialIndex],true);source=spatial_->views[spatialIndex];}
    }
    ensure(bloom_,0,s.bloom&&s.bloomStrength>0);auto bloom=source;
    if(bloom_){auto input=source;glm::vec4 p(s.bloomThreshold,s.bloomKnee,1,s.bloomScatter);
        for(size_t i=0;i<bloom_->views.size();++i){p.z=i?0.f:1.f;draw("post-bloom-down",down_,bloomLayout_,&p,sizeof(p),input,input,bloom_->views[i]);input=bloom_->views[i];}
        p.z=-1;for(size_t i=bloom_->views.size()-1;i>0;--i){draw("post-bloom-up",up_,bloomLayout_,&p,sizeof(p),bloom_->views[i-1],input,bloom_->ups[i-1]);input=bloom_->ups[i-1];}bloom=input;
    }
    const bool finishing=s.fxaa||(s.sharpen&&s.sharpness>0)||(s.vignette&&s.vignetteStrength>0)||(s.chromaticAberration&&s.chromaticPixels>0)||(s.filmGrain&&s.grainStrength>0);
    ensure(display_,2,finishing);
    ToneBlock tone{{exposure*std::exp2(s.exposureEV),gamma,toneMapping?0.f:1.f,float(s.toneMapper)},
        {s.saturation,s.contrast,s.temperature,s.tint},{bloom_?s.bloomStrength:0.f,s.colorGrading?1.f:0.f,0,0}};
    draw("post-tone-grade",finishing?toneHdr_:toneOutput_,toneLayout_,&tone,sizeof(tone),source,bloom,finishing?display_->views[0]:output);
    if(finishing){FinishBlock p{{s.fxaa?1.f:0.f,s.sharpen?s.sharpness:0.f,s.chromaticAberration?s.chromaticPixels:0.f,s.filmGrain?s.grainStrength:0.f},
        {s.vignette?s.vignetteStrength:0.f,s.vignetteRoundness,view.time,0}};
        draw("post-fxaa-display",finish_,finishLayout_,&p,sizeof(p),display_->views[0],{},output);
    }
    pendingView_=view;pendingWidth_=width;pendingHeight_=height;pending_=true;
}
}
