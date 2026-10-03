#include "renderer/rhi/OceanSurface.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "rhi/ShaderAssets.h"
#include <json/json.hpp>
#include <cmath>
namespace render {
namespace {
rhi::ShaderAsset asset(const std::string& dir,const char* name){std::string p=dir+"/"+name;return {p+".glsl",p+".metallib",p+".spv",p+".json","main0"};}
struct alignas(16) Vertex {glm::mat4 vp,model,previousVP,previousView,previousModel;glm::ivec4 flags;glm::vec4 settings;};
struct alignas(16) Fragment {glm::mat4 vp,view;glm::vec4 camera,direction,diffuse,specular,shallow,deep,foamColor,specularColor,ambientColor,optics,volume,absorb,scatter,surface,flags;};
static_assert(sizeof(Vertex)==352 && sizeof(Fragment)==368,"Water block ABI");
}
struct OceanSurface::Simulation {
    GpuOcean large,detail;Resources views;std::array<rhi::TextureViewHandle,6> images;
    Simulation(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& dir,const OceanSurfaceSettings& s):large(d,dir,s.spectrum),detail(d,dir,detailSettings(s)),views(d){
        auto i=0;for(auto* ocean:{&large,&detail})for(auto t:{ocean->displacement(),ocean->normal(),ocean->foam()})images[i++]=views.view(t);
    }
    static OceanSettings detailSettings(const OceanSurfaceSettings& s){auto d=s.spectrum;d.size=256;d.length=32;d.amplitude*=.06f*s.detailStrength;d.seed+=71;d.windSpeed*=.6f;return d;}
};
OceanSurface::OceanSurface(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& dir,const OceanSurfaceSettings& s):simulation_(std::make_unique<Simulation>(d,dir,s)),resources_(d),initial_(s){
    using namespace rhi;if(s.meshSize<2 || s.meshSize>1025)throw std::invalid_argument("Ocean surface mesh size invalid");
    GraphicsPipelineDesc p;p.vertex=asset(dir,"ocean-surface.vert");p.fragment=asset(dir,"ocean-surface.frag");p.vertexStride=20;p.attributes={{0,VertexFormat::Float3,0},{1,VertexFormat::Float2,12}};
    p.colorFormat=Format::RGBA16Float;p.additionalColorFormats={Format::RGBA16Float};p.depthAttachment=p.depthTest=p.depthWrite=true;
    p.bindings={{0,{{0,BindingType::UniformBuffer,ShaderStage::Vertex,"WaterVertex",352},{1,BindingType::UniformBuffer,ShaderStage::Fragment,"WaterFragment",368}}},{1,{}}};
    const char* vertexNames[]={"DisplaceRT","detailDisplace","previousDisplace","previousDetailDisplace"};for(uint32_t i=0;i<4;++i)p.bindings[0].entries.push_back({i+2,BindingType::SampledTexture,ShaderStage::Vertex,vertexNames[i],0});
    const char* fragmentNames[]={"NormalRT","BubblesRT","skyview","detailNormal","detailFoam","opaqueScene","scenePosition","sceneNormal"};for(uint32_t i=0;i<8;++i)p.bindings[1].entries.push_back({i,BindingType::SampledTexture,ShaderStage::Fragment,fragmentNames[i],0});
    for(const auto& stage:{p.vertex,p.fragment}){auto r=nlohmann::json::parse(readShaderText(stage.reflectionPath));for(const auto& u:r.at("ubos"))if(u.at("block_size")!=(u.at("name")=="WaterVertex"?352:368))throw std::invalid_argument("Ocean surface reflected block ABI differs");}
    pipeline_=resources_.pipeline(p);repeat_=resources_.sampler({Filter::Linear,AddressMode::Repeat});clamp_=resources_.sampler({Filter::Linear,AddressMode::ClampToEdge});
    std::vector<float> v;std::vector<uint32_t> indices;
    for(uint32_t y=0;y<s.meshSize;++y)for(uint32_t x=0;x<s.meshSize;++x){const float u=float(x)/(s.meshSize-1),w=float(y)/(s.meshSize-1);v.insert(v.end(),{(u-.5f)*s.spectrum.length,0,(w-.5f)*s.spectrum.length,u,w});}
    for(uint32_t y=0;y+1<s.meshSize;++y)for(uint32_t x=0;x+1<s.meshSize;++x){uint32_t a=y*s.meshSize+x,b=a+1,c=a+s.meshSize,d=c+1;indices.insert(indices.end(),{a,c,b,b,c,d});}
    vertices_=resources_.buffer({v.size()*sizeof(float),BufferUsage::Vertex,"Ocean grid vertices"},v.data());indices_=resources_.buffer({indices.size()*4,BufferUsage::Index,"Ocean grid indices"},indices.data());indexCount_=uint32_t(indices.size());
    for(uint32_t i=0;i<2;++i){const uint32_t size=i?256:s.spectrum.size;previous_[i]=resources_.texture({size,size,Format::RGBA32Float,TextureUsage::CopyDestination|TextureUsage::Sampled,"Previous ocean displacement"});previousViews_[i]=resources_.view(previous_[i]);}
}
OceanSurface::~OceanSurface()=default;
bool OceanSurface::compatible(const OceanSurfaceSettings& s)const{return s.spectrum.size==initial_.spectrum.size && s.spectrum.length==initial_.spectrum.length && s.meshSize==initial_.meshSize;}
void OceanSurface::simulate(float seconds,const OceanSurfaceSettings& s){
    if(!compatible(s))throw std::invalid_argument("Ocean surface requires recreation after grid change");
    if(history_){auto copy=resources_.device->createCommandList();copy.copyTexture(simulation_->large.displacement(),previous_[0]);copy.copyTexture(simulation_->detail.displacement(),previous_[1]);resources_.device->submit(copy);}
    simulation_->large.simulate(s.animate?seconds*s.timeScale:0,s.spectrum);simulation_->detail.simulate(s.animate?seconds*s.timeScale:0,Simulation::detailSettings(s));
    if(!history_){auto copy=resources_.device->createCommandList();copy.copyTexture(simulation_->large.displacement(),previous_[0]);copy.copyTexture(simulation_->detail.displacement(),previous_[1]);resources_.device->submit(copy);}
}
void OceanSurface::record(Resources& frame,rhi::CommandList& c,const FrameData& f,const OceanSurfaceSettings& s,rhi::TextureViewHandle sky,rhi::TextureViewHandle opaque,rhi::TextureViewHandle position,rhi::TextureViewHandle normal){
    using namespace rhi;glm::mat4 model(1);model[3].y=s.seaLevel;
    Vertex v{f.viewProjection,model,history_?previousVP_:f.viewProjection,history_?previousView_:f.view,history_?previousModel_:model,{s.detailWaves?1:0,history_?1:0,0,0},{32,0,0,0}};
    LightData sun{{0,0,0,0},{0,0,0,0},{0,-1,-.1f,0}};for(const auto& l:f.lights)if(l.positionType.w==0){sun=l;break;}
    Fragment p{f.viewProjection,f.view,glm::vec4(f.cameraPosition,0),sun.directionOuter,sun.colorInner,sun.colorInner,glm::vec4(s.shallow,0),glm::vec4(s.deep,0),glm::vec4(s.foamColor,0),glm::vec4(s.specular,0),glm::vec4(s.ambient,0),{s.fresnel,s.gloss,s.refractionStrength,s.deepWaterDistance},{s.subsurfaceStrength,s.anisotropy,0,0},glm::vec4(s.absorption,0),glm::vec4(s.scattering,0),{s.seaLevel,std::max(s.spectrum.heightScale,.01f),0,0},{f.sky?1.f:0.f,s.detailWaves?1.f:0.f,s.refraction?1.f:0.f,history_?1.f:0.f}};
    auto vd=frame.buffer({sizeof(v),BufferUsage::Uniform,"Water vertex parameters"},&v),fd=frame.buffer({sizeof(p),BufferUsage::Uniform,"Water fragment parameters"},&p);
    BindingLayout vertex{0,{{0,BindingType::UniformBuffer,ShaderStage::Vertex,"WaterVertex",352},{1,BindingType::UniformBuffer,ShaderStage::Fragment,"WaterFragment",368}}},fragment{1,{}};
    std::vector<BindingEntry> ve{{0,vd,0,sizeof(v),{},{}},{1,fd,0,sizeof(p),{},{}}},fe;
    const char* vn[]={"DisplaceRT","detailDisplace","previousDisplace","previousDetailDisplace"};const TextureViewHandle vi[]={simulation_->images[0],simulation_->images[3],previousViews_[0],previousViews_[1]};
    for(uint32_t i=0;i<4;++i){vertex.entries.push_back({i+2,BindingType::SampledTexture,ShaderStage::Vertex,vn[i],0});ve.push_back({i+2,{},0,0,vi[i],repeat_});}
    const char* fn[]={"NormalRT","BubblesRT","skyview","detailNormal","detailFoam","opaqueScene","scenePosition","sceneNormal"};const TextureViewHandle fi[]={simulation_->images[1],simulation_->images[2],sky,simulation_->images[4],simulation_->images[5],opaque,position,normal};
    for(uint32_t i=0;i<8;++i){fragment.entries.push_back({i,BindingType::SampledTexture,ShaderStage::Fragment,fn[i],0});fe.push_back({i,{},0,0,fi[i],i>=5?clamp_:repeat_});}
    c.bindPipeline(pipeline_);c.bindBindingSet(frame.bindings({vertex,ve}));c.bindBindingSet(frame.bindings({fragment,fe}));c.bindVertexBuffer(vertices_);c.bindIndexBuffer(indices_);c.drawIndexed(indexCount_);
    previousVP_=f.viewProjection;previousView_=f.view;previousModel_=model;history_=true;
}
}
