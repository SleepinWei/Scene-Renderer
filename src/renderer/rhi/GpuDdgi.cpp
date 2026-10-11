#include "renderer/rhi/GpuDdgi.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "renderer/rhi/ShadowRenderer.h"
#include "renderer/rhi/RasterVisibility.h"
#include "PT/GpuPathTracer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
namespace render {
namespace {
glm::mat4 depthCorrection(){glm::mat4 m(1);m[2][2]=.5f;m[3][2]=.5f;return m;}

struct alignas(16) Parameters {glm::vec4 origin;glm::ivec4 counts;glm::vec4 bias,temporal,capture,probe,camera;};
struct alignas(16) DebugParameters {glm::mat4 vp;glm::vec4 origin;glm::ivec4 counts;glm::vec4 right,up,eye,display,jitter;};
static_assert(sizeof(DebugParameters)==176,"DDGI debug std140 ABI");
static_assert(sizeof(Parameters)==112 && offsetof(Parameters,camera)==96,"DDGI std140 ABI");
rhi::ShaderAsset shader(const std::string& dir,const char* name){auto p=dir+"/"+name;return {p+".glsl",p+".metallib",p+".spv",p+".json","main0"};}
Parameters parameters(const FrameData& f){const auto& s=f.ddgi;return {glm::vec4(s.origin,s.spacing),glm::ivec4(s.counts,s.probeCount()),{s.normalBias,s.viewBias,s.visibility?1.f:0.f,s.intensity},{s.hysteresis,s.visibilityPower,s.backfaceThreshold,s.multiBounce?s.bounceGain:0.f},{float(s.captureResolution),s.maxDistance,f.sky?1.f:0.f,f.ambient},{0,0,0,-1},glm::vec4(f.cameraPosition,f.ambient)};}
bool sameVolume(const DdgiSettings& a,const DdgiSettings& b){return a.origin==b.origin && a.counts==b.counts && a.spacing==b.spacing && a.captureResolution==b.captureResolution && a.maxDistance==b.maxDistance && a.resetSerial==b.resetSerial && a.pathTracedCapture==b.pathTracedCapture && a.pathSamples==b.pathSamples && a.pathDepth==b.pathDepth;}
}
struct GpuDdgi::Volume {
    Resources resources;DdgiSettings settings;uint64_t historyKey;uint32_t cursor=0,read=0;
    rhi::BufferHandle fields[2];rhi::TextureHandle capture,depth;rhi::TextureViewHandle captureView,depthView;
    Parameters query{};
    rhi::BufferHandle debugVertices;
    Volume(std::shared_ptr<rhi::GraphicsDevice> device,const FrameData& frame):resources(device),settings(frame.ddgi),historyKey(frame.giSceneKey){
        using namespace rhi;
        for(auto& field:fields)field=resources.buffer({settings.cacheBytes()/2,BufferUsage::Storage|BufferUsage::CopySource|BufferUsage::CopyDestination,"DDGI directional field"});
        const uint32_t n=settings.captureResolution;
        capture=resources.texture({6*n,n,Format::RGBA32Float,TextureUsage::ColorAttachment|TextureUsage::Sampled|TextureUsage::CopySource,"DDGI radiance and signed radial distance"});captureView=resources.view(capture);
        depth=resources.texture({6*n,n,Format::Depth32Float,TextureUsage::DepthAttachment,"DDGI cubemap capture depth"});depthView=resources.view(depth);
    }
};
GpuDdgi::~GpuDdgi()=default;
GpuDdgi::GpuDdgi(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& dir):resources_(device),directory_(dir){
    using namespace rhi;
    if(!device->computeLimits().supported)throw std::invalid_argument("DDGI requires compute and fragment storage buffers");
    captureLayouts_={{0,{{0,BindingType::UniformBuffer,ShaderStage::Vertex,"CameraVertex",64},{1,BindingType::UniformBuffer,ShaderStage::Vertex,"ObjectData",128},{2,BindingType::UniformBuffer,ShaderStage::Fragment,"SceneLighting",1472}}},GpuMaterial::layout(),
        {2,{{0,BindingType::UniformBuffer,ShaderStage::Fragment,"DdgiParameters",sizeof(Parameters)},{1,BindingType::UniformBuffer,ShaderStage::Fragment,"ShadowData",sizeof(ShadowParameters)},{4,BindingType::SampledTexture,ShaderStage::Fragment,"shadowAtlas",0},{5,BindingType::StorageRead,ShaderStage::Fragment,"DdgiField",16}}}};
    GraphicsPipelineDesc p;p.vertex=shader(dir,"forward.vert");p.fragment=shader(dir,"ddgi-capture.frag");p.vertexStride=32;p.attributes={{0,VertexFormat::Float3,0},{1,VertexFormat::Float3,12},{2,VertexFormat::Float2,24}};p.bindings=captureLayouts_;p.colorFormat=Format::RGBA32Float;p.depthAttachment=p.depthTest=p.depthWrite=true;p.cull=CullMode::None;p.label="DDGI Lambertian probe capture";capture_=resources_.pipeline(p);
    p.vertex=shader(dir,"instanced.vert");p.bindings[0].entries.push_back({3,BindingType::StorageRead,ShaderStage::Vertex,"OutPose",64});instanced_=resources_.pipeline(p);
    copyLayout_={0,{{0,BindingType::StorageRead,ShaderStage::Compute,"DdgiField",16},{1,BindingType::StorageReadWrite,ShaderStage::Compute,"DdgiNext",16},{2,BindingType::UniformBuffer,ShaderStage::Compute,"DdgiParameters",sizeof(Parameters)}}};
    copy_=resources_.computePipeline({shader(dir,"ddgi-copy.comp"),{copyLayout_},{64,1,1},"DDGI retain unupdated probes"});
    integrateLayout_={0,{{0,BindingType::UniformBuffer,ShaderStage::Compute,"DdgiParameters",sizeof(Parameters)},{1,BindingType::SampledTexture,ShaderStage::Compute,"probeCapture",0},{2,BindingType::SampledTexture,ShaderStage::Compute,"probeSky",0},{3,BindingType::StorageRead,ShaderStage::Compute,"DdgiField",16},{4,BindingType::StorageReadWrite,ShaderStage::Compute,"DdgiNext",16},{5,BindingType::StorageRead,ShaderStage::Compute,"ProbePixels",80}}};
    emptyProbe_=resources_.buffer({80,BufferUsage::Storage,"DDGI unused PT scratch"});
    integrate_=resources_.computePipeline({shader(dir,"ddgi-integrate.comp"),{integrateLayout_},{256,1,1},"DDGI irradiance and distance moments"});
    surfaceLayout_={0,{{0,BindingType::UniformBuffer,ShaderStage::Fragment,"DdgiParameters",sizeof(Parameters)},{1,BindingType::StorageRead,ShaderStage::Fragment,"DdgiField",16},{2,BindingType::SampledTexture,ShaderStage::Fragment,"positions",0},{3,BindingType::SampledTexture,ShaderStage::Fragment,"normals",0},{4,BindingType::SampledTexture,ShaderStage::Fragment,"albedos",0},{5,BindingType::SampledTexture,ShaderStage::Fragment,"materialEffects",0},{6,BindingType::SampledTexture,ShaderStage::Fragment,"materialAO",0},{7,BindingType::SampledTexture,ShaderStage::Fragment,"screenAO",0}}};
    environmentLayout_={1,{{0,BindingType::UniformBuffer,ShaderStage::Fragment,"SkyData",128},{1,BindingType::SampledTexture,ShaderStage::Fragment,"skyIrradiance",0}}};
    p={};p.vertex=shader(dir,"tonemap.vert");p.fragment=shader(dir,"ddgi-surface.frag");p.vertexStride=16;p.attributes={{0,VertexFormat::Float2,0},{1,VertexFormat::Float2,8}};p.colorFormat=Format::RGBA16Float;p.blend=p.additiveBlend=true;p.bindings={surfaceLayout_,environmentLayout_};p.label="DDGI diffuse ambient replacement";surface_=resources_.pipeline(p);
    sampleLayout_={0,{{0,BindingType::UniformBuffer,ShaderStage::Compute,"DdgiParameters",sizeof(Parameters)},{1,BindingType::StorageRead,ShaderStage::Compute,"DdgiField",16},{2,BindingType::StorageRead,ShaderStage::Compute,"QueryPositions",16},{3,BindingType::StorageRead,ShaderStage::Compute,"QueryNormals",16},{4,BindingType::StorageReadWrite,ShaderStage::Compute,"QueryResults",16}}};
    sample_=resources_.computePipeline({shader(dir,"ddgi-sample.comp"),{sampleLayout_},{64,1,1},"DDGI diagnostic surface queries"});
    nearest_=resources_.sampler({Filter::Nearest,AddressMode::ClampToEdge});linear_=resources_.sampler({Filter::Linear,AddressMode::Repeat});
    const float q[]={-1,-1,0,1,1,-1,1,1,1,1,1,0,-1,-1,0,1,1,1,1,0,-1,1,0,0};quad_=resources_.buffer({sizeof(q),BufferUsage::Vertex,"DDGI fullscreen quad"},q);
}
void GpuDdgi::update(Resources& temp,rhi::CommandList& commands,const FrameData& f,const std::vector<DrawPacket>& packets,rhi::BufferHandle lighting,rhi::BufferHandle shadows,rhi::TextureViewHandle shadowAtlas,rhi::TextureViewHandle sky){
    using namespace rhi;f.ddgi.validate();
    if(f.ddgi.pathTracedCapture&&(!probeTracer_||probeTracer_->resolution()!=uint32_t(f.ddgi.captureResolution)))throw std::logic_error("DDGI PT capture needs a matching frozen PT scene");
    if(!volume_||!sameVolume(volume_->settings,f.ddgi)||volume_->historyKey!=f.giSceneKey){volume_=std::make_unique<Volume>(resources_.device,f);stats_={};}
    auto& v=*volume_;v.settings=f.ddgi;v.query=parameters(f);
    if(f.ddgi.freezeUpdates){stats_.updated=0;return;}
    const size_t bytes=f.ddgi.cacheBytes()/2;const auto read=v.fields[v.read],write=v.fields[1-v.read];
    auto copyParameters=temp.buffer({sizeof(Parameters),BufferUsage::Uniform,"DDGI copy parameters"},&v.query);
    commands.setLabel("ddgi/retain-field");auto retain=temp.bindings({copyLayout_,{{0,read,0,bytes,{},{}},{1,write,0,bytes,{},{}},{2,copyParameters,0,sizeof(Parameters),{},{}}}});commands.dispatch(copy_,{retain},{uint32_t((bytes/16+63)/64),1,1});
    std::vector<BufferHandle> objects;objects.reserve(packets.size());
    for(const auto& packet:packets){const std::array<glm::mat4,2> data{packet.model,glm::transpose(glm::inverse(packet.model))};objects.push_back(temp.buffer({128,BufferUsage::Uniform,"DDGI capture object"},data.data()));}
    const glm::vec3 directions[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    const glm::vec3 up[]={{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}};
    const uint32_t count=f.ddgi.probeCount(),budget=std::min(count,uint32_t(f.ddgi.probesPerFrame)),resolution=f.ddgi.captureResolution;
    for(uint32_t b=0;b<budget;b++){
        const uint32_t index=v.cursor;v.cursor=(v.cursor+1)%count;
        const glm::ivec3 grid{index%f.ddgi.counts.x,(index/f.ddgi.counts.x)%f.ddgi.counts.y,index/(f.ddgi.counts.x*f.ddgi.counts.y)};
        const glm::vec3 origin=f.ddgi.origin+glm::vec3(grid)*f.ddgi.spacing;
        auto params=v.query;params.probe=glm::vec4(origin,float(index));if(f.ddgi.pathTracedCapture)params.capture.z=2.f;auto uniform=temp.buffer({sizeof(params),BufferUsage::Uniform,"DDGI probe parameters"},&params);
        auto probePixels=emptyProbe_;if(f.ddgi.pathTracedCapture)probePixels=probeTracer_->record(temp,commands,origin,uint32_t(stats_.frame),f.ddgi.maxDistance);
        auto environment=temp.bindings({captureLayouts_[2],{{0,uniform,0,sizeof(params),{},{}},{1,shadows,0,sizeof(ShadowParameters),{},{}},{4,{},0,0,shadowAtlas,nearest_},{5,read,0,bytes,{},{}}}});
        if(!f.ddgi.pathTracedCapture)for(uint32_t face=0;face<6;face++){
            auto projection=depthCorrection()*glm::perspective(glm::radians(90.f),1.f,std::max(.001f,f.ddgi.spacing*.001f),f.ddgi.maxDistance);
            auto vp=projection*glm::lookAtRH(origin,origin+directions[face],up[face]);auto camera=temp.buffer({64,BufferUsage::Uniform,"DDGI face camera"},&vp);
            commands.setLabel("ddgi/probe-capture");RenderPassDesc pass;pass.color=v.captureView;pass.depth=v.depthView;pass.clearColor={0,0,0,0};pass.colorLoad=pass.depthLoad=face==0?LoadOp::Clear:LoadOp::Load;pass.viewport={face*resolution,0,resolution,resolution};commands.beginRenderPass(pass);
            auto visibility=f;visibility.viewProjection=vp;visibility.viewportWidth=visibility.viewportHeight=resolution;
            for(size_t i=0;i<packets.size();i++){
                const auto& packet=packets[i];if(!packet.mesh||!packet.material||packet.material->transparent()||!rasterPacketVisible(packet,visibility))continue;
                auto layout=captureLayouts_[0];std::vector<BindingEntry> entries{{0,camera,0,64,{},{}},{1,objects[i],0,128,{},{}},{2,lighting,0,1472,{},{}}};
                if(packet.mesh->instances()){layout.entries.push_back({3,BindingType::StorageRead,ShaderStage::Vertex,"OutPose",64});entries.push_back({3,packet.mesh->instances(),0,size_t(packet.mesh->instanceCapacity())*64,{},{}});commands.bindPipeline(instanced_);}else commands.bindPipeline(capture_);
                commands.bindBindingSet(temp.bindings({layout,entries}));packet.material->bind(commands);commands.bindBindingSet(environment);packet.mesh->draw(commands);
            }commands.endRenderPass();
        }
        commands.setLabel("ddgi/probe-integrate");auto integrate=temp.bindings({integrateLayout_,{{0,uniform,0,sizeof(params),{},{}},{1,{},0,0,v.captureView,nearest_},{2,{},0,0,sky,linear_},{3,read,0,bytes,{},{}},{4,write,0,bytes,{},{}},{5,probePixels,0,f.ddgi.pathTracedCapture?size_t(6*resolution*resolution)*80:80,{},{}}}});commands.dispatch(integrate_,{integrate},{1,1,1});
    }
    v.read=1-v.read;stats_.probes=count;stats_.updated=budget;stats_.sweepFrames=(count+budget-1)/budget;stats_.cursor=v.cursor;stats_.cacheBytes=f.ddgi.cacheBytes();stats_.scratchBytes=6*resolution*resolution*(f.ddgi.pathTracedCapture?100:20);stats_.frame++;
}
void GpuDdgi::compose(Resources& temp,rhi::CommandList& commands,const FrameData& f,rhi::TextureViewHandle hdr,const std::vector<rhi::TextureViewHandle>& g,rhi::TextureViewHandle ao,rhi::BufferHandle skyParams,rhi::TextureViewHandle irradiance){
    if(!volume_||g.size()<6)throw std::logic_error("DDGI requires Scene GBuffer");using namespace rhi;auto& v=*volume_;auto p=parameters(f);p.probe.w=f.rsm&&f.rsmSettings.indirectOnly?1.f:0.f;auto uniform=temp.buffer({sizeof(p),BufferUsage::Uniform,"DDGI surface parameters"},&p);
    auto set=temp.bindings({surfaceLayout_,{{0,uniform,0,sizeof(p),{},{}},{1,v.fields[v.read],0,f.ddgi.cacheBytes()/2,{},{}},{2,{},0,0,g[0],nearest_},{3,{},0,0,g[1],nearest_},{4,{},0,0,g[2],nearest_},{5,{},0,0,g[4],nearest_},{6,{},0,0,g[3],nearest_},{7,{},0,0,ao,nearest_}}});
    auto env=temp.bindings({environmentLayout_,{{0,skyParams,0,128,{},{}},{1,{},0,0,irradiance,linear_}}});
    commands.setLabel("ddgi/surface-query");RenderPassDesc pass;pass.color=hdr;pass.colorLoad=LoadOp::Load;commands.beginRenderPass(pass);commands.bindPipeline(surface_);commands.bindBindingSet(set);commands.bindBindingSet(env);commands.bindVertexBuffer(quad_);commands.draw(6);commands.endRenderPass();
}
void GpuDdgi::visualize(Resources& temp,rhi::CommandList& commands,const FrameData& f,rhi::TextureViewHandle output,rhi::TextureViewHandle positions,float exposure,float gamma){
    if(!volume_||!f.ddgi.debugProbes)return;
    using namespace rhi;auto& v=*volume_;
    if(!debugPipeline_){
        debugVertexLayout_={0,{{0,BindingType::UniformBuffer,ShaderStage::Vertex,"DdgiDebug",sizeof(DebugParameters)}}};
        debugFragmentLayout_={1,{{0,BindingType::UniformBuffer,ShaderStage::Fragment,"DdgiDebug",sizeof(DebugParameters)},
            {1,BindingType::StorageRead,ShaderStage::Fragment,"DdgiField",16},
            {2,BindingType::SampledTexture,ShaderStage::Fragment,"debugPositions",0},
            {3,BindingType::UniformBuffer,ShaderStage::Fragment,"DdgiParameters",sizeof(Parameters)}}};
        GraphicsPipelineDesc p;p.vertex=shader(directory_,"ddgi-debug.vert");p.fragment=shader(directory_,"ddgi-debug.frag");
        p.vertexStride=12;p.attributes={{0,VertexFormat::Float3,0}};p.bindings={debugVertexLayout_,debugFragmentLayout_};
        p.colorFormat=Format::RGBA8UNorm;p.blend=true;p.label="DDGI diagnostic probe overlay";debugPipeline_=resources_.pipeline(p);
    }
    if(!v.debugVertices){
        std::vector<glm::vec3> vertices;vertices.reserve(f.ddgi.probeCount()*6);
        const glm::vec2 corners[]={{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}};
        for(uint32_t i=0;i<f.ddgi.probeCount();++i)for(auto corner:corners)vertices.emplace_back(corner,float(i));
        v.debugVertices=v.resources.buffer({vertices.size()*sizeof(glm::vec3),BufferUsage::Vertex,"DDGI probe marker corners"},vertices.data());
    }
    const auto inverseView=glm::inverse(f.view);
    DebugParameters p{f.viewProjection,glm::vec4(f.ddgi.origin,f.ddgi.spacing),glm::ivec4(f.ddgi.counts,f.ddgi.debugLayer),
        glm::vec4(glm::vec3(inverseView[0]),f.ddgi.debugRadius),inverseView[1],glm::vec4(f.cameraPosition,1),
        {exposure,gamma,float(f.ddgi.debugProbes),f.ddgi.debugXray?1.f:0.f},glm::vec4(f.temporalJitter,f.toneMapping?1.f:0.f,0)};
    auto debug=temp.buffer({sizeof(p),BufferUsage::Uniform,"DDGI probe display"},&p);
    auto query=parameters(f);auto gi=temp.buffer({sizeof(query),BufferUsage::Uniform,"DDGI probe query parameters"},&query);
    auto vertex=temp.bindings({debugVertexLayout_,{{0,debug,0,sizeof(p),{},{}}}});
    auto fragment=temp.bindings({debugFragmentLayout_,{{0,debug,0,sizeof(p),{},{}},
        {1,v.fields[v.read],0,f.ddgi.cacheBytes()/2,{},{}},{2,{},0,0,positions,nearest_},{3,gi,0,sizeof(query),{},{}}}});
    commands.setLabel("ddgi/probe-visualization");RenderPassDesc pass;pass.color=output;pass.colorLoad=LoadOp::Load;
    commands.beginRenderPass(pass);commands.bindPipeline(debugPipeline_);commands.bindBindingSet(vertex);commands.bindBindingSet(fragment);
    commands.bindVertexBuffer(v.debugVertices);commands.draw(f.ddgi.probeCount()*6);commands.endRenderPass();
}
std::vector<glm::vec4> GpuDdgi::readField() const {if(!volume_)return {};std::vector<glm::vec4> data(volume_->settings.cacheBytes()/2/16);resources_.device->readBuffer(volume_->fields[volume_->read],0,data.size()*16,data.data());return data;}
std::vector<float> GpuDdgi::readCapture() const {if(!volume_)return {};if(!volume_->settings.pathTracedCapture)return resources_.device->readTextureFloat(volume_->capture);std::vector<float> out;for(auto p:probeTracer_->readCapture()){out.push_back(p.x);out.push_back(p.y);out.push_back(p.z);out.push_back(p.w);}return out;}
std::vector<glm::vec4> GpuDdgi::sample(const std::vector<glm::vec4>& positions,const std::vector<glm::vec4>& normals){
    if(!volume_||positions.size()!=normals.size()||positions.empty())throw std::invalid_argument("DDGI diagnostic queries invalid");using namespace rhi;Resources temp(resources_.device);auto& v=*volume_;size_t bytes=positions.size()*16;
    auto query=v.query;query.probe.w=float(positions.size());auto params=temp.buffer({sizeof(Parameters),BufferUsage::Uniform,"DDGI diagnostic parameters"},&query),p=temp.buffer({bytes,BufferUsage::Storage,"DDGI query positions"},positions.data()),n=temp.buffer({bytes,BufferUsage::Storage,"DDGI query normals"},normals.data()),result=temp.buffer({bytes,BufferUsage::Storage|BufferUsage::CopySource,"DDGI query results"});
    auto bindings=temp.bindings({sampleLayout_,{{0,params,0,sizeof(Parameters),{},{}},{1,v.fields[v.read],0,v.settings.cacheBytes()/2,{},{}},{2,p,0,bytes,{},{}},{3,n,0,bytes,{},{}},{4,result,0,bytes,{},{}}}});auto commands=resources_.device->createCommandList();commands.dispatch(sample_,{bindings},{uint32_t((positions.size()+63)/64),1,1});resources_.device->submit(commands);std::vector<glm::vec4> out(positions.size());resources_.device->readBuffer(result,0,bytes,out.data());return out;
}
}
