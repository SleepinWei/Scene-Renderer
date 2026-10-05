#include "renderer/rhi/GpuClouds.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "rhi/ShaderAssets.h"
#include <json/json.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <cstring>
#include <cmath>
#include <algorithm>
namespace render {
namespace {
struct alignas(16) Parameters {
    glm::mat4 inverseVP,previousVP;
    glm::vec4 cameraTime,previousCameraTime,layer,shape,windHistory,sun,sunColor,planet;
    glm::ivec4 grid,quality;
};
static_assert(sizeof(Parameters)==288,"Cloud std140 ABI");
rhi::ShaderAsset asset(const std::string& root,const std::string& name){auto p=root+"/"+name;return {p+".glsl",p+".metallib",p+".spv",p+".json","main0"};}
}
struct GpuClouds::Targets {
    Resources resources;uint32_t width,height,columns,rows;
    rhi::BufferHandle dispatch,tiles;
    rhi::TextureHandle raw,meta;std::array<rhi::TextureHandle,2> color,depth;
    rhi::TextureViewHandle rawView,metaView;std::array<rhi::TextureViewHandle,2> colorViews,depthViews;
    Targets(std::shared_ptr<rhi::GraphicsDevice> d,uint32_t w,uint32_t h):resources(d),width(w),height(h),columns((w+7)/8),rows((h+7)/8){
        using namespace rhi;
        dispatch=resources.buffer({12,BufferUsage::Storage|BufferUsage::Indirect|BufferUsage::CopySource,"Cloud GPU dispatch arguments"});
        tiles=resources.buffer({size_t(columns)*rows*4,BufferUsage::Storage,"Cloud compact tile queue"});
        const auto usage=TextureUsage::Storage|TextureUsage::Sampled|TextureUsage::CopySource;
        raw=resources.texture({w,h,Format::RGBA16Float,usage,"Cloud raw radiance and transmittance"});rawView=resources.view(raw);
        meta=resources.texture({w,h,Format::RGBA32Float,usage,"Cloud depth and march count"});metaView=resources.view(meta);
        for(uint32_t i=0;i<2;i++){
            color[i]=resources.texture({w,h,Format::RGBA16Float,usage,"Cloud temporal radiance"});colorViews[i]=resources.view(color[i]);
            depth[i]=resources.texture({w,h,Format::RGBA32Float,usage,"Cloud temporal depth"});depthViews[i]=resources.view(depth[i]);
        }
    }
};
GpuClouds::GpuClouds(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory):resources_(device){
    using namespace rhi;
    linear_=resources_.sampler({Filter::Linear,AddressMode::Repeat});nearest_=resources_.sampler({Filter::Nearest,AddressMode::ClampToEdge});
    auto usage=TextureUsage::Storage|TextureUsage::Sampled|TextureUsage::CopySource;
    noise_=resources_.texture({528,528,Format::RGBA16Float,usage,"Cloud periodic 64-cubed noise atlas"});noiseView_=resources_.view(noise_);
    weather_=resources_.texture({256,256,Format::RGBA16Float,usage,"Cloud weather map"});weatherView_=resources_.view(weather_);
    for(const char* name:{"noise","weather","reset","classify","march","resolve"}) {
        ComputePipelineDesc p;p.shader=asset(directory,std::string("cloud-")+name+".comp");p.threads={8,8,1};
        auto reflect=nlohmann::json::parse(readShaderText(p.shader.reflectionPath));p.bindings={{0,{}},{1,{}}};
        for(const auto& u:reflect.at("ubos")){
            if(u.at("block_size")!=sizeof(Parameters))throw std::invalid_argument("Cloud reflected uniform ABI differs");
            p.bindings[0].entries.push_back({u.at("binding"),BindingType::UniformBuffer,ShaderStage::Compute,u.at("name"),u.at("block_size")});
        }
        for(const auto& b:reflect.value("ssbos",nlohmann::json::array())) {
            auto type=b.value("readonly",false)?BindingType::StorageRead:b.value("writeonly",false)?BindingType::StorageWrite:BindingType::StorageReadWrite;
            p.bindings[1].entries.push_back({b.at("binding"),type,ShaderStage::Compute,b.at("name"),b.value("block_size",0u)});
        }
        for(const auto& t:reflect.value("textures",nlohmann::json::array()))p.bindings[1].entries.push_back({t.at("binding"),BindingType::SampledTexture,ShaderStage::Compute,t.at("name"),0});
        for(const auto& t:reflect.value("images",nlohmann::json::array()))p.bindings[1].entries.push_back({t.at("binding"),BindingType::StorageTextureWrite,ShaderStage::Compute,t.at("name"),0,t.at("format")=="rgba16f"?Format::RGBA16Float:Format::RGBA32Float});
        p.label=std::string("Cloud ")+name;kernels_[name]={resources_.computePipeline(p),p.bindings};
    }
    GraphicsPipelineDesc p;p.vertex=asset(directory,"tonemap.vert");p.fragment=asset(directory,"cloud-composite.frag");
    p.vertexStride=16;p.attributes={{0,VertexFormat::Float2,0},{1,VertexFormat::Float2,8}};
    p.colorFormat=Format::RGBA16Float;p.additionalColorFormats={Format::RGBA16Float};p.attachmentBlend={true,false};p.label="Depth-aware cloud composite";
    p.bindings={{0,{{0,BindingType::UniformBuffer,ShaderStage::Fragment,"CloudParameters",sizeof(Parameters)}}},
        {1,{{0,BindingType::SampledTexture,ShaderStage::Fragment,"cloudResolved",0},{1,BindingType::SampledTexture,ShaderStage::Fragment,"cloudResolvedDepth",0},{2,BindingType::SampledTexture,ShaderStage::Fragment,"sceneDepth",0}}}};
    compositeLayouts_=p.bindings;composite_=resources_.pipeline(p);
    const float quad[]={-1,-1,0,1,1,-1,1,1,1,1,1,0,-1,-1,0,1,1,1,1,0,-1,1,0,0};
    quad_=resources_.buffer({sizeof(quad),BufferUsage::Vertex,"Cloud composite quad"},quad);
}
GpuClouds::~GpuClouds()=default;
void GpuClouds::record(Resources& transient,rhi::CommandList& commands,const FrameData& f,const SunState& sun,
    rhi::TextureViewHandle sceneDepth,rhi::TextureViewHandle sky,rhi::TextureViewHandle hdr,rhi::TextureViewHandle motion){
    using namespace rhi;f.clouds.validate();const auto& s=f.clouds;
    const uint32_t width=(f.viewportWidth+s.downsample-1)/s.downsample,height=(f.viewportHeight+s.downsample-1)/s.downsample;
    if(!width || !height)throw std::invalid_argument("Cloud viewport empty");
    if(uint64_t((width+7)/8)*((height+7)/8)>resources_.device->computeLimits().maxGroups[0])
        throw std::invalid_argument("Cloud tile queue exceeds indirect X limit; increase downsample");
    if(!targets_ || targets_->width!=width || targets_->height!=height){targets_=std::make_unique<Targets>(resources_.device,width,height);valid_=false;index_=samples_=0;}
    if(previousKey_!=f.historyKey || !(s==previousSettings_) || glm::length(previousCamera_-f.cameraPosition)>100 ||
       f.timeSeconds<previousTime_ || f.timeSeconds-previousTime_>.5f)valid_=false;
    SunState cloudSun=sun;cloudSun.observerHeightKm=(s.baseHeight+s.thickness*.5f)*.001f;
    Parameters p{glm::inverse(f.viewProjection),valid_?previousVP_:f.viewProjection,
        glm::vec4(f.cameraPosition,f.timeSeconds),glm::vec4(previousCamera_,previousTime_),
        {s.baseHeight,s.thickness,s.coverage,s.density},{s.shapeScale,s.weatherScale,s.erosion,s.maxDistance},
        {s.wind.x,s.wind.y,valid_ && s.temporal?1.f:0.f,.85f},glm::vec4(sun.direction,0),
        glm::vec4(sun.irradiance*solarTransmittance(f.atmosphere,cloudSun),0),{f.atmosphere.radii.w*1000,f.seaLevelMeters,0,0},
        {int(width),int(height),int(targets_->columns),int(samples_%1024)},
        {int(s.steps),int(s.lightSteps),int(s.seed),int(s.downsample)}};
    auto uniform=transient.buffer({sizeof(p),BufferUsage::Uniform,"Cloud frame parameters"},&p);
    auto bind=[&](const Kernel& kernel,const std::map<uint32_t,BindingEntry>& entries){
        auto a=transient.bindings({kernel.layouts[0],{{0,uniform,0,sizeof(p),{},{}}}});
        std::vector<BindingEntry> values;for(const auto& e:kernel.layouts[1].entries)values.push_back(entries.at(e.binding));
        return std::vector<BindingSetHandle>{a,transient.bindings({kernel.layouts[1],values})};
    };
    auto image=[&](uint32_t i,TextureViewHandle view,bool sampled=false,bool repeat=false){return BindingEntry{i,{},0,0,view,sampled?(repeat?linear_:nearest_):SamplerHandle{}};};
    auto buffer=[&](uint32_t i,BufferHandle h,size_t bytes){return BindingEntry{i,h,0,bytes,{},{}};};
    const std::array<uint32_t,3> groups{targets_->columns,targets_->rows,1};
    if(!baked_ || seed_!=s.seed){
        commands.setLabel("Cloud noise bake");commands.dispatch(kernels_.at("noise").pipeline,bind(kernels_.at("noise"),{{0,image(0,noiseView_)}}),{66,66,1});
        commands.dispatch(kernels_.at("weather").pipeline,bind(kernels_.at("weather"),{{0,image(0,weatherView_)}}),{32,32,1});
    }
    commands.setLabel("Cloud clear");auto& reset=kernels_.at("reset");
    commands.dispatch(reset.pipeline,bind(reset,{{0,buffer(0,targets_->dispatch,12)},{1,image(1,targets_->rawView)},{2,image(2,targets_->metaView)}}),groups);
    commands.setLabel("Cloud GPU tile classification");auto& classify=kernels_.at("classify");
    commands.dispatch(classify.pipeline,bind(classify,{{0,buffer(0,targets_->dispatch,12)},{1,buffer(1,targets_->tiles,size_t(totalTiles())*4)},{2,image(2,sceneDepth,true)}}),groups);
    commands.setLabel("Cloud indirect raymarch");auto& march=kernels_.at("march");
    commands.dispatchIndirect(march.pipeline,bind(march,{{0,buffer(0,targets_->tiles,size_t(totalTiles())*4)},
        {1,image(1,sceneDepth,true)},{2,image(2,noiseView_,true,true)},{3,image(3,weatherView_,true,true)},
        {4,image(4,sky,true,true)},{5,image(5,targets_->rawView)},{6,image(6,targets_->metaView)}}),targets_->dispatch);
    commands.setLabel("Cloud temporal resolve");auto& resolve=kernels_.at("resolve");
    commands.dispatch(resolve.pipeline,bind(resolve,{{0,image(0,targets_->colorViews[index_])},{1,image(1,targets_->depthViews[index_])},
        {2,image(2,targets_->rawView,true)},{3,image(3,targets_->metaView,true)},
        {4,image(4,targets_->colorViews[1-index_],true)},{5,image(5,targets_->depthViews[1-index_],true)}}),groups);
    commands.setLabel("Cloud bilateral composite");RenderPassDesc pass;pass.color=hdr;pass.colorLoad=LoadOp::Load;pass.additionalColors={{motion,LoadOp::Load}};
    commands.beginRenderPass(pass);commands.bindPipeline(composite_);
    commands.bindBindingSet(transient.bindings({compositeLayouts_[0],{{0,uniform,0,sizeof(p),{},{}}}}));
    commands.bindBindingSet(transient.bindings({compositeLayouts_[1],{image(0,targets_->colorViews[index_],true),image(1,targets_->depthViews[index_],true),image(2,sceneDepth,true)}}));
    commands.bindVertexBuffer(quad_);commands.draw(6);commands.endRenderPass();
}
void GpuClouds::commit(const FrameData& f){previousVP_=f.viewProjection;previousCamera_=f.cameraPosition;previousTime_=f.timeSeconds;previousKey_=f.historyKey;previousSettings_=f.clouds;seed_=f.clouds.seed;baked_=valid_=true;index_=1-index_;++samples_;}
std::vector<float> GpuClouds::read(){if(!targets_ || !valid_)throw std::logic_error("Clouds have no completed frame");return resources_.device->readTextureFloat(targets_->color[1-index_]);}
std::vector<float> GpuClouds::readMetadata(){if(!targets_ || !valid_)throw std::logic_error("Clouds have no completed frame");return resources_.device->readTextureFloat(targets_->depth[1-index_]);}
std::array<uint32_t,3> GpuClouds::readDispatch(){if(!targets_)throw std::logic_error("Clouds not initialized");std::array<uint32_t,3> result;resources_.device->readBuffer(targets_->dispatch,0,12,result.data());return result;}
uint32_t GpuClouds::totalTiles() const{return targets_?targets_->columns*targets_->rows:0;}
}
