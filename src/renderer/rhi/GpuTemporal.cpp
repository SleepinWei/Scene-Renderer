#include "renderer/rhi/GpuTemporal.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "rhi/ShaderAssets.h"
namespace render {
struct GpuTemporal::Targets {Resources resources;uint32_t width,height;std::array<rhi::TextureHandle,2> color,depth;std::array<rhi::TextureViewHandle,2> colorViews,depthViews;
    Targets(std::shared_ptr<rhi::GraphicsDevice> d,uint32_t w,uint32_t h):resources(d),width(w),height(h){using namespace rhi;for(uint32_t i=0;i<2;i++){color[i]=resources.texture({w,h,Format::RGBA16Float,TextureUsage::Storage|TextureUsage::Sampled|TextureUsage::CopySource,"Temporal HDR history"});depth[i]=resources.texture({w,h,Format::RGBA32Float,TextureUsage::Storage|TextureUsage::Sampled|TextureUsage::CopySource,"Temporal linear depth history"});colorViews[i]=resources.view(color[i]);depthViews[i]=resources.view(depth[i]);}}
};
GpuTemporal::GpuTemporal(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& dir):resources_(d){
    using namespace rhi;ComputePipelineDesc p;auto path=dir+"/temporal.comp";p.shader={path+".glsl",path+".metallib",path+".spv",path+".json","main0"};p.threads={8,8,1};p.bindings={{0,{{0,BindingType::UniformBuffer,ShaderStage::Compute,"TemporalParameters",288}}},{1,{{0,BindingType::StorageTextureWrite,ShaderStage::Compute,"resolvedColor",0,Format::RGBA16Float},{1,BindingType::StorageTextureWrite,ShaderStage::Compute,"resolvedDepth",0,Format::RGBA32Float}}}};
    const char* names[]={"currentColor","currentDepth","historyColor","historyDepth","surfaceMotion"};for(uint32_t i=0;i<5;++i)p.bindings[1].entries.push_back({i+2,BindingType::SampledTexture,ShaderStage::Compute,names[i],0});layouts_=p.bindings;pipeline_=resources_.computePipeline(p);sampler_=resources_.sampler({Filter::Linear,AddressMode::ClampToEdge});
}
GpuTemporal::~GpuTemporal()=default;
void GpuTemporal::resize(uint32_t w,uint32_t h){if(targets_ && targets_->width==w && targets_->height==h)return;auto t=std::make_unique<Targets>(resources_.device,w,h);targets_.swap(t);reset();index_=0;}
glm::vec2 GpuTemporal::jitter(uint32_t frame){auto halton=[](uint32_t n,uint32_t base){float result=0,scale=1;while(n){scale/=base;result+=scale*(n%base);n/=base;}return result;};const uint32_t n=frame%16+1;return {halton(n,2)-.5f,halton(n,3)-.5f};}
rhi::TextureViewHandle GpuTemporal::record(Resources& frame,rhi::CommandList& c,const FrameData& f,rhi::TextureViewHandle color,rhi::TextureViewHandle depth,rhi::TextureViewHandle motion){
    using namespace rhi;if(!targets_)throw std::invalid_argument("Temporal targets not initialized");struct alignas(16) Parameters {glm::mat4 inverseVP,currentView,previousVP,previousView;glm::vec4 camera,settings;};static_assert(sizeof(Parameters)==288,"Temporal ABI");Parameters p{glm::inverse(f.viewProjection),f.view,valid_?previousVP_:f.viewProjection,valid_?previousView_:f.view,glm::vec4(f.cameraPosition,0),{.9f,valid_?1.f:0.f,0,0}};
    auto data=frame.buffer({sizeof(p),BufferUsage::Uniform,"Temporal resolve parameters"},&p);auto a=frame.bindings({layouts_[0],{{0,data,0,sizeof(p),{},{}}}});const TextureViewHandle images[]={targets_->colorViews[index_],targets_->depthViews[index_],color,depth,targets_->colorViews[1-index_],targets_->depthViews[1-index_],motion};std::vector<BindingEntry> entries;for(uint32_t i=0;i<7;++i)entries.push_back({i,{},0,0,images[i],i<2?SamplerHandle{}:sampler_});auto b=frame.bindings({layouts_[1],entries});c.dispatch(pipeline_,{a,b},{(targets_->width+7)/8,(targets_->height+7)/8,1});return targets_->colorViews[index_];
}
void GpuTemporal::commit(const FrameData& f){previousVP_=f.viewProjection;previousView_=f.view;valid_=true;++samples_;index_=1-index_;}
std::vector<float> GpuTemporal::read(){if(!valid_)throw std::invalid_argument("Temporal history has not been submitted");return resources_.device->readTextureFloat(targets_->color[1-index_]);}
}
