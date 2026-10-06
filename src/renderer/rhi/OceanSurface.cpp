#include "renderer/rhi/OceanSurface.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "rhi/ShaderAssets.h"
#include "renderer/rhi/GpuImageCache.h"
#include <json/json.hpp>
#include <cmath>
#include <algorithm>
#include "renderer/rhi/ShadowRenderer.h"
#include "renderer/rhi/GpuShoreWater.h"
#include "renderer/rhi/WaterTransport.h"
#include <glm/gtc/matrix_inverse.hpp>
namespace render {
namespace {
rhi::ShaderAsset asset(const std::string& dir,const char* name){std::string p=dir+"/"+name;return {p+".glsl",p+".metallib",p+".spv",p+".json","main0"};}
struct alignas(16) Advanced {glm::mat4 inverseBed,bedModel;glm::vec4 patch,previousPatch,features,bedInfo;};
struct alignas(16) Vertex {glm::mat4 vp,model,previousVP,previousView,previousModel;glm::ivec4 flags;glm::vec4 settings,grid,previousGrid;Advanced advanced;};
struct alignas(16) Fragment {glm::mat4 vp,view;glm::vec4 camera,direction,diffuse,specular,shallow,deep,foamColor,specularColor,ambientColor,optics,volume,absorb,scatter,surface,flags;Advanced advanced;glm::vec4 meshBoundary,underwaterControls;};
struct alignas(16) Eye {glm::mat4 inverseVP;Advanced advanced;glm::vec4 camera,waves,absorb,scatter,direction,diffuse,controls;};
static_assert(sizeof(Advanced)==192 && sizeof(Vertex)==576 && sizeof(Fragment)==592 && sizeof(Eye)==368,"Water block ABI");
rhi::BindingLayout eyeImages(){using namespace rhi;BindingLayout l{1,{}};const char* names[]={"eyeScene","eyePosition","eyeNormal","eyeDisplace","eyeShore","eyeMask","eyeSky","eyeOriginal"};
    for(uint32_t i=0;i<8;++i)l.entries.push_back({i,BindingType::SampledTexture,ShaderStage::Fragment,names[i],0});return l;}
}
struct OceanSurface::WaterTargets {
    Resources resources;uint32_t width,height;
    rhi::TextureViewHandle color,position,depth;
    rhi::TextureHandle colorTexture,positionTexture;
    rhi::TextureHandle intervals; rhi::TextureViewHandle chain;
    std::vector<rhi::TextureViewHandle> intervalViews;
    WaterTargets(std::shared_ptr<rhi::GraphicsDevice> d,uint32_t w,uint32_t h):resources(d),width(w),height(h){
        using namespace rhi;
        // Two columns: underwater on the left, air on the right.
        // All retain full viewport resolution and share
        // sampler bindings to stay within the portable Metal stage limit.
        colorTexture=resources.texture({w*2,h,Format::RGBA16Float,TextureUsage::ColorAttachment|TextureUsage::Sampled|TextureUsage::CopySource,"Underwater HDR"});color=resources.view(colorTexture);
        positionTexture=resources.texture({w*2,h,Format::RGBA32Float,TextureUsage::ColorAttachment|TextureUsage::Sampled|TextureUsage::CopySource,"Underwater position/validity"});position=resources.view(positionTexture);
        depth=resources.view(resources.texture({w*2,h,Format::Depth32Float,TextureUsage::DepthAttachment,"Water capture depth"}));
    }
    void ensureHierarchy(){
        if(chain)return;using namespace rhi;auto d=resources.device;uint32_t w=width*2,h=height;
        uint32_t levels=1;if(d->supportsTextureSubresources())for(uint32_t n=std::max(w,h);n>1;n>>=1)++levels;
        intervals=resources.texture({w,h,Format::RGBA32Float,TextureUsage::Storage|TextureUsage::Sampled|TextureUsage::CopySource,"Water min/max depth intervals",levels});
        chain=resources.view(intervals,{0,levels,0,1});for(uint32_t i=0;i<levels;++i)intervalViews.push_back(resources.view(intervals,{i,1,0,1}));
    }
};
struct OceanSurface::Coastal {
    Resources resources;rhi::TextureViewHandle bed,empty;std::unique_ptr<GpuShoreWater> shore;
    rhi::ComputePipelineHandle depthPipeline;std::vector<rhi::BindingLayout> depthLayouts;
    Coastal(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& dir,const OceanSurfaceSettings& s):resources(d) {
        using namespace rhi;
        const uint32_t n=s.bathymetry?s.bathymetry->size:1;
        if(!n||(s.bathymetry&&s.bathymetry->heightColor.size()!=size_t(n)*n))throw std::invalid_argument("Invalid water bathymetry raster");
        for(int col=0;col<4;++col)for(int row=0;row<4;++row)
            if(!std::isfinite(s.bathymetryModel[col][row]))throw std::invalid_argument("Nonfinite water bathymetry transform");
        if(s.bathymetry && (std::abs(glm::determinant(s.bathymetryModel))<1e-8f ||
            std::abs(s.bathymetryModel[1].x)>1e-6f || std::abs(s.bathymetryModel[1].z)>1e-6f ||
            std::abs(s.bathymetryModel[0].w)>1e-6f || std::abs(s.bathymetryModel[1].w)>1e-6f ||
            std::abs(s.bathymetryModel[2].w)>1e-6f || std::abs(s.bathymetryModel[3].w-1)>1e-6f))
            throw std::invalid_argument("Water bathymetry requires an invertible heightfield transform with a vertical height axis");
        if(s.bathymetry)for(const auto& value:s.bathymetry->heightColor)for(int channel=0;channel<4;++channel)
            if(!std::isfinite(value[channel]))throw std::invalid_argument("Nonfinite water bathymetry data");
        auto texture=resources.texture({n,n,Format::RGBA32Float,TextureUsage::Sampled|TextureUsage::CopyDestination,"Stable water bathymetry and base colour"});bed=resources.view(texture);
        const glm::vec4 fallback(s.seaLevel-s.deepWaterDistance,.6f,.5f,.3f);
        d->writeTextureFloat(texture,s.bathymetry?reinterpret_cast<const float*>(s.bathymetry->heightColor.data()):&fallback.x,size_t(n)*n*16);
        auto zero=resources.texture({1,1,Format::RGBA32Float,TextureUsage::Sampled|TextureUsage::CopyDestination,"Inactive shore state"});empty=resources.view(zero);const glm::vec4 black(0);d->writeTextureFloat(zero,&black.x,16);
        const auto p=dir+"/water-depth.comp";
        depthLayouts={{0,{{0,BindingType::UniformBuffer,ShaderStage::Compute,"WaterDepthParameters",80},{1,BindingType::SampledTexture,ShaderStage::Compute,"depthSource",0}}},
            {1,{{0,BindingType::StorageTextureWrite,ShaderStage::Compute,"depthDestination",0}}}};
        depthPipeline=resources.computePipeline({{p+".glsl",p+".metallib",p+".spv",p+".json","main0"},depthLayouts,{8,8,1},"Water conservative min/max depth"});
    }
};
struct OceanSurface::Transport {
    Resources resources;rhi::TextureViewHandle view;
    Transport(std::shared_ptr<rhi::GraphicsDevice> d):resources(d){using namespace rhi;
        auto t=resources.texture({WaterTauSamples,WaterAlbedoSamples*WaterGSamples*WaterAngleSamples,Format::RGBA32Float,TextureUsage::Sampled|TextureUsage::CopyDestination,"Water 2+ scattering slab response"});view=resources.view(t);
        const auto& data=waterTransportLut();d->writeTextureFloat(t,reinterpret_cast<const float*>(data.data()),data.size()*16);
    }
};
struct OceanSurface::Simulation {
    GpuOcean large,detail;Resources views;std::array<rhi::TextureViewHandle,6> images;
    Simulation(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& dir,const OceanSurfaceSettings& s):large(d,dir,s.spectrum),detail(d,dir,detailSettings(s)),views(d){
        auto i=0;for(auto* ocean:{&large,&detail})for(auto t:{ocean->displacement(),ocean->normal(),ocean->foam()})images[i++]=views.view(t);
    }
    static OceanSettings detailSettings(const OceanSurfaceSettings& s){
        auto d=s.spectrum;d.size=256;d.length=32;d.amplitude*=.06f*s.detailStrength;d.seed+=71;d.windSpeed*=.6f;
        if(s.shortWaveRipples){
            // Reuse the detail FFT rather than adding a third simulation. Four
            // samples at the shortest wavelength keep its normals resolved.
            d.minWavelength=.5f;d.maxWavelength=2.f;
            d.targetRmsHeight=s.rippleRmsHeight*s.detailStrength;
            d.foamScale=0; // Centimetre wind ripples are not breaking whitecaps.
        }
        return d;
    }
};
OceanSurface::OceanSurface(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& dir,const OceanSurfaceSettings& s):simulation_(std::make_unique<Simulation>(d,dir,s)),coastal_(std::make_unique<Coastal>(d,dir,s)),directory_(dir),resources_(d),initial_(s){
    using namespace rhi;if(s.meshSize<2 || s.meshSize>1025)throw std::invalid_argument("Ocean surface mesh size invalid");
    GraphicsPipelineDesc p;p.vertex=asset(dir,"ocean-surface.vert");p.fragment=asset(dir,"ocean-surface.frag");p.vertexStride=20;p.attributes={{0,VertexFormat::Float3,0},{1,VertexFormat::Float2,12}};
    p.colorFormat=Format::RGBA16Float;p.additionalColorFormats={Format::RGBA16Float};p.depthAttachment=p.depthTest=p.depthWrite=true;
    p.bindings={{0,{{0,BindingType::UniformBuffer,ShaderStage::Vertex,"WaterVertex",sizeof(Vertex)},{1,BindingType::UniformBuffer,ShaderStage::Fragment,"WaterFragment",sizeof(Fragment)}}},{1,{}}};
    const char* vertexNames[]={"DisplaceRT","detailDisplace","previousDisplace","previousDetailDisplace"};for(uint32_t i=0;i<4;++i)p.bindings[0].entries.push_back({i+2,BindingType::SampledTexture,ShaderStage::Vertex,vertexNames[i],0});
    p.bindings[0].entries.push_back({6,BindingType::SampledTexture,ShaderStage::Vertex,"vertexShoreState",0});p.bindings[0].entries.push_back({7,BindingType::SampledTexture,ShaderStage::Vertex,"previousShoreState",0});
    const char* fragmentNames[]={"NormalRT","BubblesRT","skyview","detailNormal","detailFoam","opaqueScene","scenePosition","sceneNormal"};for(uint32_t i=0;i<8;++i)p.bindings[1].entries.push_back({i,BindingType::SampledTexture,ShaderStage::Fragment,fragmentNames[i],0});
    for(const auto& stage:{p.vertex,p.fragment}){
        auto reflection=nlohmann::json::parse(readShaderText(stage.reflectionPath));
        for(const auto& u:reflection.at("ubos")){
            const auto name=u.at("name").get<std::string>();const size_t bytes=name=="WaterVertex"?sizeof(Vertex):name=="WaterFragment"?sizeof(Fragment):name=="ShadowData"?sizeof(ShadowParameters):0;
            if(!bytes||u.at("block_size")!=bytes)throw std::invalid_argument("Ocean surface reflected block ABI differs");
        }
    }
    p.bindings.push_back({2,{{0,BindingType::SampledTexture,ShaderStage::Fragment,"waterMask",0},{1,BindingType::SampledTexture,ShaderStage::Fragment,"volumeDisplace",0},{2,BindingType::SampledTexture,ShaderStage::Fragment,"bathymetryMap",0},{3,BindingType::UniformBuffer,ShaderStage::Fragment,"ShadowData",sizeof(ShadowParameters)},{4,BindingType::SampledTexture,ShaderStage::Fragment,"waterShadowAtlas",0},
        {5,BindingType::SampledTexture,ShaderStage::Fragment,"fragmentShoreState",0},{6,BindingType::SampledTexture,ShaderStage::Fragment,"shoreFoam",0},{7,BindingType::SampledTexture,ShaderStage::Fragment,"multipleScatterLut",0}}});
    imageCache_=GpuImageCache::forDevice(d);
    mask_=imageCache_->acquire(s.waterMask ? s.waterMask : std::make_shared<const ImageRGBA8>(ImageRGBA8{1,1,{255,255,255,255}}));
    pipeline_=resources_.pipeline(p);repeat_=resources_.sampler({Filter::Linear,AddressMode::Repeat});clamp_=resources_.sampler({Filter::Linear,AddressMode::ClampToEdge});positionSampler_=resources_.sampler({Filter::Nearest,AddressMode::ClampToEdge});
    const float surfaceLength=s.surfaceLength>0?s.surfaceLength:s.spectrum.length;
    std::vector<float> v;std::vector<uint32_t> indices;
    for(uint32_t y=0;y<s.meshSize;++y)for(uint32_t x=0;x<s.meshSize;++x){const float u=float(x)/(s.meshSize-1),w=float(y)/(s.meshSize-1);v.insert(v.end(),{(u-.5f)*surfaceLength,0,(w-.5f)*surfaceLength,u,w});}
    // Conservatively omit wholly dry cells before uploading indices. The fragment
    // mask still resolves the exact shoreline; this saves vertex work on large lakes.
    auto wetCell=[&](uint32_t x,uint32_t y){
        if(s.shore.enabled||s.cameraGrid||!s.waterMask)return true;
        const auto& mask=*s.waterMask;const float cells=float(s.meshSize-1);
        int x0=std::max(0,int(std::floor(float(x)/cells*mask.width-.5f)));
        int x1=std::min(int(mask.width)-1,int(std::ceil(float(x+1)/cells*mask.width-.5f)));
        int y0=std::max(0,int(std::floor((1-float(y+1)/cells)*mask.height-.5f)));
        int y1=std::min(int(mask.height)-1,int(std::ceil((1-float(y)/cells)*mask.height-.5f)));
        for(int row=y0;row<=y1;++row)for(int col=x0;col<=x1;++col)
            if(mask.pixels[(size_t(row)*mask.width+col)*4]>127)return true;
        return false;
    };
    for(uint32_t y=0;y+1<s.meshSize;++y)for(uint32_t x=0;x+1<s.meshSize;++x){if(!wetCell(x,y))continue;uint32_t a=y*s.meshSize+x,b=a+1,c=a+s.meshSize,d=c+1;indices.insert(indices.end(),{a,c,b,b,c,d});}
    vertices_=resources_.buffer({v.size()*sizeof(float),BufferUsage::Vertex,"Ocean grid vertices"},v.data());indexCount_=uint32_t(indices.size());uint32_t dummy=0;indices_=resources_.buffer({std::max(size_t(1),indices.size())*4,BufferUsage::Index,"Ocean wet-cell indices"},indices.empty()?&dummy:indices.data());
    for(uint32_t i=0;i<2;++i){const uint32_t size=i?256:s.spectrum.size;previous_[i]=resources_.texture({size,size,Format::RGBA32Float,TextureUsage::CopyDestination|TextureUsage::Sampled,"Previous ocean displacement"});previousViews_[i]=resources_.view(previous_[i]);}
    captureLayout_={2,{{0,BindingType::UniformBuffer,ShaderStage::Fragment,"SkyData",128},
        {1,BindingType::UniformBuffer,ShaderStage::Fragment,"ShadowData",sizeof(ShadowParameters)},
        {2,BindingType::SampledTexture,ShaderStage::Fragment,"skyRadianceLut",0},
        {3,BindingType::SampledTexture,ShaderStage::Fragment,"skyIrradianceLut",0},
        {4,BindingType::SampledTexture,ShaderStage::Fragment,"shadowAtlas",0},
        {5,BindingType::UniformBuffer,ShaderStage::Fragment,"WaterCapture",240},
        {6,BindingType::SampledTexture,ShaderStage::Fragment,"captureDisplace",0},
        {7,BindingType::SampledTexture,ShaderStage::Fragment,"captureDetailDisplace",0}}};
    p={};p.vertex=asset(dir,"forward.vert");p.fragment=asset(dir,"water-capture.frag");p.vertexStride=sizeof(MeshVertex);p.attributes=GpuMesh::attributes();
    p.bindings={ForwardPbrRenderer::frameLayout(),GpuMaterial::layout(),captureLayout_};p.colorFormat=Format::RGBA16Float;p.additionalColorFormats={Format::RGBA32Float};
    p.bindings[0].entries.push_back({7,BindingType::SampledTexture,ShaderStage::Fragment,"captureShoreState",0});
    p.bindings[0].entries.push_back({4,BindingType::UniformBuffer,ShaderStage::Fragment,"WaterWetParameters",32});p.bindings[0].entries.push_back({5,BindingType::SampledTexture,ShaderStage::Fragment,"waterWetHistory",0});
    p.depthAttachment=p.depthTest=p.depthWrite=true;p.label="Clipped underwater color/depth";
    capturePipeline_=resources_.pipeline(p);
    if(d->computeLimits().supported){p.vertex=asset(dir,"instanced.vert");p.bindings[0].entries.push_back({3,BindingType::StorageRead,ShaderStage::Vertex,"OutPose",64});captureInstanced_=resources_.pipeline(p);}

    p={};p.vertex=asset(dir,"tonemap.vert");p.fragment=asset(dir,"water-underwater.frag");p.vertexStride=16;
    p.attributes={{0,VertexFormat::Float2,0},{1,VertexFormat::Float2,8}};p.colorFormat=Format::RGBA16Float;
    p.bindings={{0,{{0,BindingType::UniformBuffer,ShaderStage::Fragment,"UnderwaterParameters",sizeof(Eye)}}},eyeImages(),
        {2,{{3,BindingType::UniformBuffer,ShaderStage::Fragment,"ShadowData",sizeof(ShadowParameters)},{4,BindingType::SampledTexture,ShaderStage::Fragment,"waterShadowAtlas",0}}}};
    p.label="Underwater view segment transport";fogPipeline_=resources_.pipeline(p);
    const float quad[]={-1,-1,0,1,1,-1,1,1,1,1,1,0,-1,-1,0,1,1,1,1,0,-1,1,0,0};
    fogQuad_=resources_.buffer({sizeof(quad),BufferUsage::Vertex,"Underwater fullscreen"},quad);

}
OceanSurface::~OceanSurface()=default;
std::vector<float> OceanSurface::readCapture(bool positions,bool aboveWater) const {
    if(!underwater_)throw std::logic_error("Water capture has not been rendered");
    const auto atlas=resources_.device->readTextureFloat(positions?underwater_->positionTexture:underwater_->colorTexture);
    std::vector<float> left(size_t(underwater_->width)*underwater_->height*4);
    for(uint32_t y=0;y<underwater_->height;++y)std::copy_n(atlas.data()+size_t(y)*underwater_->width*8+(aboveWater?underwater_->width*4:0),underwater_->width*4,left.data()+size_t(y)*underwater_->width*4);
    return left;
}
bool OceanSurface::compatible(const OceanSurfaceSettings& s)const{return s.spectrum.size==initial_.spectrum.size && s.spectrum.length==initial_.spectrum.length && s.meshSize==initial_.meshSize && s.surfaceLength==initial_.surfaceLength && s.waterMask==initial_.waterMask && s.cameraGrid==initial_.cameraGrid && s.shore.enabled==initial_.shore.enabled && s.bathymetry==initial_.bathymetry && s.bathymetryModel==initial_.bathymetryModel;}
void OceanSurface::simulate(float seconds,const OceanSurfaceSettings& s,glm::vec3 cameraPosition){
    if(!compatible(s))throw std::invalid_argument("Ocean surface requires recreation after grid change");
    if(history_){auto copy=resources_.device->createCommandList();copy.copyTexture(simulation_->large.displacement(),previous_[0]);copy.copyTexture(simulation_->detail.displacement(),previous_[1]);resources_.device->submit(copy);}
    simulation_->large.simulate(s.animate?seconds*s.timeScale:0,s.spectrum);simulation_->detail.simulate(s.animate?seconds*s.timeScale:0,Simulation::detailSettings(s));
    if(!history_){auto copy=resources_.device->createCommandList();copy.copyTexture(simulation_->large.displacement(),previous_[0]);copy.copyTexture(simulation_->detail.displacement(),previous_[1]);resources_.device->submit(copy);}
    if(s.multipleScattering&&!transport_)transport_=std::make_unique<Transport>(resources_.device);
    if(s.shore.enabled&&s.bathymetry){
        if(!coastal_->shore||!coastal_->shore->compatible(s))coastal_->shore=std::make_unique<GpuShoreWater>(resources_.device,directory_,s,coastal_->bed);
        coastal_->shore->simulate(s.animate?seconds*s.timeScale:0,s,cameraPosition,simulation_->images[0],previousViews_[0]);
    }else coastal_->shore.reset();
}
rhi::TextureViewHandle OceanSurface::wetnessView() const {return coastal_->shore?coastal_->shore->foam():coastal_->empty;}
glm::vec4 OceanSurface::shorePatch() const {return coastal_->shore?coastal_->shore->patch():glm::vec4(0,0,1,1);}
std::vector<float> OceanSurface::readShore(bool foam) const {if(!coastal_->shore)throw std::logic_error("Shallow water is disabled or has no bathymetry");return coastal_->shore->read(foam);}
uint32_t OceanSurface::shoreSubsteps() const {return coastal_->shore?coastal_->shore->lastSteps():0;}
void OceanSurface::captureUnderwater(Resources& frame,rhi::CommandList& c,const FrameData& f,const OceanSurfaceSettings& s,
                                    const std::vector<DrawPacket>& packets,rhi::BufferHandle camera,rhi::BufferHandle lighting,
                                    const std::vector<rhi::BindingEntry>& environment,uint32_t width,uint32_t height){
    captured_=false;
    opaqueGeometry_=std::any_of(packets.begin(),packets.end(),[](const auto& packet){return !packet.material->transparent();});
    if(!s.underwaterCapture||!s.refraction||!opaqueGeometry_)return;
    using namespace rhi;
    if(!underwater_||underwater_->width!=width||underwater_->height!=height)underwater_=std::make_unique<WaterTargets>(resources_.device,width,height);
    const bool shore=bool(coastal_->shore);
    struct alignas(16) Capture {glm::vec4 waves,extinction;Advanced advanced;glm::vec4 eye;};
    Capture parameters{{s.seaLevel,s.spectrum.length,32,s.detailWaves?1.f:0.f},glm::vec4(s.absorption+s.scattering,0),
        {glm::inverse(s.bathymetryModel),s.bathymetryModel,shorePatch(),shore?coastal_->shore->previousPatch():shorePatch(),
        {s.robustRefraction?1.f:0.f,s.multipleScattering?1.f:0.f,shore?1.f:0.f,s.shore.foam?1.f:0.f},{s.bathymetry?1.f:0.f,s.shore.wetSand?1.f:0.f,0,0}},glm::vec4(f.cameraPosition,s.underwaterView?1.f:0.f)};
    static_assert(sizeof(Capture)==240,"Water capture ABI");
    auto captureLayer=[&](WaterTargets& targets,float mode) {
        parameters.eye.w=mode;
        auto buffer=frame.buffer({sizeof(parameters),BufferUsage::Uniform,"Water capture parameters"},&parameters);
        auto entries=environment;entries.insert(entries.end(),{{5,buffer,0,sizeof(parameters),{},{}},{6,{},0,0,simulation_->images[0],repeat_},{7,{},0,0,simulation_->images[3],repeat_}});
        auto water=frame.bindings({captureLayout_,entries});
        const std::array<glm::vec4,2> wetData={shorePatch(),glm::vec4(shore&&s.shore.wetSand?1.f:0.f,s.shore.foam?1.f:0.f,0,0)};
        auto wetBuffer=frame.buffer({32,BufferUsage::Uniform,"Captured underwater wet material"},wetData.data());
        RenderPassDesc pass;pass.color=targets.color;pass.clearColor={0,0,0,0};pass.additionalColors={{targets.position}};pass.depth=targets.depth;
        pass.viewport={mode>1.5f?width:0,0,width,height};
        if(mode>1.5f){pass.colorLoad=LoadOp::Load;pass.depthLoad=LoadOp::Load;pass.additionalColors[0].load=LoadOp::Load;}
        c.setLabel(mode>1.5f?"water/capture-air":"water/capture");c.beginRenderPass(pass);
        for(const auto& packet:packets){
            if(packet.material->transparent())continue;
            std::array<glm::mat4,2> object={packet.model,glm::transpose(glm::inverse(packet.model))};
            auto data=frame.buffer({sizeof(object),BufferUsage::Uniform,"Underwater object"},object.data());
            auto layout=ForwardPbrRenderer::frameLayout();std::vector<BindingEntry> bindings={{0,camera,0,64,{},{}},{1,data,0,128,{},{}},{2,lighting,0,1472,{},{}}};
            layout.entries.push_back({7,BindingType::SampledTexture,ShaderStage::Fragment,"captureShoreState",0});bindings.push_back({7,{},0,0,shore?coastal_->shore->state():coastal_->empty,clamp_});
            layout.entries.push_back({4,BindingType::UniformBuffer,ShaderStage::Fragment,"WaterWetParameters",32});bindings.push_back({4,wetBuffer,0,32,{},{}});
            layout.entries.push_back({5,BindingType::SampledTexture,ShaderStage::Fragment,"waterWetHistory",0});bindings.push_back({5,{},0,0,wetnessView(),clamp_});
            if(packet.mesh->instances()){
                layout.entries.push_back({3,BindingType::StorageRead,ShaderStage::Vertex,"OutPose",64});
                bindings.push_back({3,packet.mesh->instances(),0,size_t(packet.mesh->instanceCapacity())*64,{},{}});c.bindPipeline(captureInstanced_);
            }else c.bindPipeline(capturePipeline_);
            c.bindBindingSet(frame.bindings({layout,bindings}));packet.material->bind(c);c.bindBindingSet(water);packet.mesh->draw(c);
        }
        c.endRenderPass();
    };
    captureLayer(*underwater_,0.f);
    if(s.underwaterView) {
        captureLayer(*underwater_,2.f);
    }
    captured_=true;
    if(s.robustRefraction) {
        underwater_->ensureHierarchy();
        struct alignas(16) Depth {glm::mat4 view;glm::ivec4 mode;};
        for(uint32_t mip=0;mip<underwater_->intervalViews.size();++mip) {
            Depth p{f.view,{int(mip),0,0,0}};auto data=frame.buffer({80,BufferUsage::Uniform,"Water depth mip parameters"},&p);
            auto input=mip?underwater_->intervalViews[mip-1]:underwater_->position;
            auto a=frame.bindings({coastal_->depthLayouts[0],{{0,data,0,80,{},{}},{1,{},0,0,input,positionSampler_}}});
            auto b=frame.bindings({coastal_->depthLayouts[1],{{0,{},0,0,underwater_->intervalViews[mip],{}}}});
            c.setLabel("water/depth-chain");c.dispatch(coastal_->depthPipeline,{a,b},{(std::max(1u,(width*2)>>mip)+7)/8,(std::max(1u,height>>mip)+7)/8,1});
        }
    }
}
void OceanSurface::record(Resources& frame,rhi::CommandList& c,const FrameData& f,const OceanSurfaceSettings& s,rhi::TextureViewHandle sky,rhi::TextureViewHandle opaque,rhi::TextureViewHandle position,rhi::TextureViewHandle normal,rhi::BufferHandle shadowParameters,rhi::TextureViewHandle shadowAtlas){
    using namespace rhi;glm::mat4 model(1);model[3].y=s.seaLevel;
    const float domain=s.surfaceLength>0?s.surfaceLength:s.spectrum.length;
    const glm::vec4 grid(f.cameraPosition.x,f.cameraPosition.z,domain,s.cameraGrid?s.gridFocus:0);
    const bool captured=s.underwaterCapture&&s.refraction&&captured_;
    if(captured){opaque=underwater_->color;position=underwater_->position;}
    const bool shore=bool(coastal_->shore);
    if(captured&&s.robustRefraction)normal=underwater_->chain;
    Advanced advanced{glm::inverse(s.bathymetryModel),s.bathymetryModel,shorePatch(),shore?coastal_->shore->previousPatch():shorePatch(),
        {s.robustRefraction?1.f:0.f,s.multipleScattering&&s.volumeIntegration?1.f:0.f,shore?1.f:0.f,s.shore.foam?1.f:0.f},
        {s.bathymetry?1.f:0.f,s.shore.wetSand?1.f:0.f,0,captured&&s.robustRefraction?1.f:0.f}};
    const auto projection=f.viewProjection*glm::inverse(f.view);
    advanced.bedInfo.z=std::max(std::abs(projection[0][0])*float(f.viewportWidth),std::abs(projection[1][1])*float(f.viewportHeight));
    Vertex v{f.viewProjection,model,history_?previousVP_:f.viewProjection,history_?previousView_:f.view,history_?previousModel_:model,{s.detailWaves?1:0,history_?1:0,0,0},{32,s.spectrum.length,float(s.meshSize),0},grid,history_?previousGrid_:grid,advanced};
    LightData sun{{0,0,0,0},{0,0,0,0},{0,-1,-.1f,0}};int sunIndex=-1;for(size_t i=0;i<f.lights.size();++i)if(f.lights[i].positionType.w==0){sun=f.lights[i];sunIndex=int(i);break;}
    Fragment p{f.viewProjection,f.view,glm::vec4(f.cameraPosition,opaqueGeometry_?1.f:0.f),sun.directionOuter,sun.colorInner,sun.colorInner,glm::vec4(s.shallow,0),glm::vec4(s.deep,0),glm::vec4(s.foamColor,0),glm::vec4(s.specular,0),glm::vec4(s.ambient,0),{s.fresnel,s.gloss,s.refractionStrength,s.deepWaterDistance},{s.subsurfaceStrength,s.anisotropy,s.volumeIntegration?4.f:0.f,captured?1.f:0.f},glm::vec4(s.absorption,float(s.opticalDebug)),glm::vec4(s.scattering,float(sunIndex)),{s.seaLevel,std::max(s.spectrum.heightScale,.01f),s.spectrum.length,s.detailWaves?1.f:0.f},{f.sky?1.f:0.f,s.detailWaves?1.f:0.f,s.refraction&&(opaqueGeometry_||s.bathymetry)?1.f:0.f,history_?1.f:0.f},advanced,{domain*.5f,0,0,0},{s.underwaterView?1.f:0.f,s.underwaterFog?1.f:0.f,s.refraction?1.f:0.f,f.farPlane}};
    auto vd=frame.buffer({sizeof(v),BufferUsage::Uniform,"Water vertex parameters"},&v),fd=frame.buffer({sizeof(p),BufferUsage::Uniform,"Water fragment parameters"},&p);
    BindingLayout vertex{0,{{0,BindingType::UniformBuffer,ShaderStage::Vertex,"WaterVertex",sizeof(Vertex)},{1,BindingType::UniformBuffer,ShaderStage::Fragment,"WaterFragment",sizeof(Fragment)}}},fragment{1,{}};
    std::vector<BindingEntry> ve{{0,vd,0,sizeof(v),{},{}},{1,fd,0,sizeof(p),{},{}}},fe;
    const char* vn[]={"DisplaceRT","detailDisplace","previousDisplace","previousDetailDisplace"};const TextureViewHandle vi[]={simulation_->images[0],simulation_->images[3],previousViews_[0],previousViews_[1]};
    for(uint32_t i=0;i<4;++i){vertex.entries.push_back({i+2,BindingType::SampledTexture,ShaderStage::Vertex,vn[i],0});ve.push_back({i+2,{},0,0,vi[i],repeat_});}
    vertex.entries.push_back({6,BindingType::SampledTexture,ShaderStage::Vertex,"vertexShoreState",0});ve.push_back({6,{},0,0,shore?coastal_->shore->state():coastal_->empty,clamp_});
    vertex.entries.push_back({7,BindingType::SampledTexture,ShaderStage::Vertex,"previousShoreState",0});ve.push_back({7,{},0,0,shore?coastal_->shore->previous():coastal_->empty,clamp_});
    const char* fn[]={"NormalRT","BubblesRT","skyview","detailNormal","detailFoam","opaqueScene","scenePosition","sceneNormal"};const TextureViewHandle fi[]={simulation_->images[1],simulation_->images[2],sky,simulation_->images[4],simulation_->images[5],opaque,position,normal};
    for(uint32_t i=0;i<8;++i){fragment.entries.push_back({i,BindingType::SampledTexture,ShaderStage::Fragment,fn[i],0});fe.push_back({i,{},0,0,fi[i],i>=6||(i==5&&captured)?positionSampler_:i==5?clamp_:repeat_});}
    BindingLayout advancedLayout{2,{{0,BindingType::SampledTexture,ShaderStage::Fragment,"waterMask",0},{1,BindingType::SampledTexture,ShaderStage::Fragment,"volumeDisplace",0},{2,BindingType::SampledTexture,ShaderStage::Fragment,"bathymetryMap",0},{3,BindingType::UniformBuffer,ShaderStage::Fragment,"ShadowData",sizeof(ShadowParameters)},{4,BindingType::SampledTexture,ShaderStage::Fragment,"waterShadowAtlas",0},
        {5,BindingType::SampledTexture,ShaderStage::Fragment,"fragmentShoreState",0},{6,BindingType::SampledTexture,ShaderStage::Fragment,"shoreFoam",0},{7,BindingType::SampledTexture,ShaderStage::Fragment,"multipleScatterLut",0}}};
    auto advancedBindings=frame.bindings({advancedLayout,{{0,{},0,0,mask_->view(),clamp_},{1,{},0,0,simulation_->images[0],repeat_},{2,{},0,0,coastal_->bed,clamp_},{3,shadowParameters,0,sizeof(ShadowParameters),{},{}},{4,{},0,0,shadowAtlas,positionSampler_},
        {5,{},0,0,shore?coastal_->shore->state():coastal_->empty,clamp_},{6,{},0,0,wetnessView(),clamp_},{7,{},0,0,transport_?transport_->view:coastal_->empty,clamp_}}});
    c.bindPipeline(pipeline_);c.bindBindingSet(frame.bindings({vertex,ve}));c.bindBindingSet(frame.bindings({fragment,fe}));c.bindBindingSet(advancedBindings);c.bindVertexBuffer(vertices_);c.bindIndexBuffer(indices_);if(indexCount_)c.drawIndexed(indexCount_);
    previousVP_=f.viewProjection;previousView_=f.view;previousModel_=model;previousGrid_=grid;history_=true;
}
void OceanSurface::recordUnderwaterFog(Resources& frame,rhi::CommandList& c,const FrameData& f,const OceanSurfaceSettings& s,
                                     rhi::TextureViewHandle target,rhi::TextureViewHandle opaque,rhi::TextureViewHandle position,rhi::TextureViewHandle normal,rhi::TextureViewHandle sky,
                                     rhi::BufferHandle shadowParameters,rhi::TextureViewHandle shadowAtlas){
    if(!s.underwaterView||!s.underwaterFog||s.opticalDebug)return;
    if(glm::dot(s.absorption+s.scattering,s.absorption+s.scattering)<1e-12f)return;
    using namespace rhi;const bool shore=bool(coastal_->shore);
    LightData sun{{0,0,0,0},{0,0,0,0},{0,-1,-.1f,0}};int sunIndex=-1;
    for(size_t i=0;i<f.lights.size();++i)if(f.lights[i].positionType.w==0){sun=f.lights[i];sunIndex=int(i);break;}
    Eye p{glm::inverse(f.viewProjection),{glm::inverse(s.bathymetryModel),s.bathymetryModel,shorePatch(),shorePatch(),
        {0,0,shore?1.f:0.f,0},{s.bathymetry?1.f:0.f,0,0,0}},glm::vec4(f.cameraPosition,captured_?1.f:0.f),
        {s.seaLevel,s.spectrum.length,32,0},glm::vec4(s.absorption,s.deepWaterDistance),glm::vec4(s.scattering,s.anisotropy),sun.directionOuter,sun.colorInner,
        {(s.surfaceLength>0?s.surfaceLength:s.spectrum.length)*.5f,f.sky?1.f:0.f,s.volumeIntegration?s.subsurfaceStrength:0.f,float(sunIndex)}};
    auto data=frame.buffer({sizeof(p),BufferUsage::Uniform,"Underwater eye parameters"},&p);
    auto a=frame.bindings({{0,{{0,BindingType::UniformBuffer,ShaderStage::Fragment,"UnderwaterParameters",sizeof(Eye)}}},{{0,data,0,sizeof(p),{},{}}}});
    const TextureViewHandle images[]={captured_?underwater_->color:opaque,position,normal,simulation_->images[0],shore?coastal_->shore->state():coastal_->empty,mask_->view(),sky,opaque};
    std::vector<BindingEntry> entries;for(uint32_t i=0;i<8;++i)entries.push_back({i,{},0,0,images[i],i==1||i==2?positionSampler_:i==3?repeat_:clamp_});
    auto b=frame.bindings({eyeImages(),entries});
    auto shadow=frame.bindings({{2,{{3,BindingType::UniformBuffer,ShaderStage::Fragment,"ShadowData",sizeof(ShadowParameters)},{4,BindingType::SampledTexture,ShaderStage::Fragment,"waterShadowAtlas",0}}},
        {{3,shadowParameters,0,sizeof(ShadowParameters),{},{}},{4,{},0,0,shadowAtlas,positionSampler_}}});
    RenderPassDesc pass;pass.color=target;pass.colorLoad=LoadOp::Load;
    c.setLabel("water/underwater-fog");c.beginRenderPass(pass);c.bindPipeline(fogPipeline_);c.bindBindingSet(a);c.bindBindingSet(b);c.bindBindingSet(shadow);c.bindVertexBuffer(fogQuad_);c.draw(6);c.endRenderPass();
}
}
