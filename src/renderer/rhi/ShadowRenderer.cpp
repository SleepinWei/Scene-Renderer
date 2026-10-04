#include "renderer/rhi/ShadowRenderer.h"
#include "rhi/ShaderAssets.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>
#include <stdexcept>
namespace render {
static_assert(sizeof(ShadowParameters)==16048 && offsetof(ShadowParameters,settings)==15424,"Shadow std140 ABI");
namespace {
rhi::ShaderAsset asset(const std::string& directory,const char* name){auto p=directory+"/"+name;return {p+".glsl",p+".metallib",p+".spv",p+".json","main0"};}
glm::mat4 depthCorrection(){glm::mat4 m(1);m[2][2]=.5f;m[3][2]=.5f;return m;}
glm::vec3 safeUp(glm::vec3 direction){return std::abs(direction.y)>.95f?glm::vec3(0,0,1):glm::vec3(0,1,0);}
}
rhi::BindingLayout ShadowRenderer::objectLayout(){using namespace rhi;return {0,{{0,BindingType::UniformBuffer,ShaderStage::Vertex,"CameraVertex",64},{1,BindingType::UniformBuffer,ShaderStage::Vertex,"ObjectData",128}}};}
ShadowRenderer::ShadowRenderer(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory,uint32_t pixels,rhi::TextureViewHandle sky):resources_(std::move(device)),tilePixels_(pixels),extent_(pixels*14) {
    using namespace rhi;
    skyIrradiance_=sky;skySampler_=resources_.sampler({Filter::Linear,AddressMode::Repeat});if(!sky){auto black=resources_.texture({1,1,Format::RGBA8UNorm,TextureUsage::Sampled|TextureUsage::CopyDestination,"RSM sky fallback"});const uint8_t bytes[]={0,0,0,255};resources_.device->writeTexture(black,bytes,4);skyIrradiance_=resources_.view(black);}
    if(!pixels || pixels>resources_.device->graphicsLimits().maxTextureDimension2D/14 || extent_>resources_.device->graphicsLimits().maxTextureDimension2D)throw std::invalid_argument("Shadow atlas exceeds device dimension limit");
    atlas_=resources_.texture({extent_,extent_,Format::Depth32Float,TextureUsage::DepthAttachment|TextureUsage::Sampled|TextureUsage::CopySource,"Directional/point/spot shadow atlas"});view_=resources_.view(atlas_);
    for(uint32_t i=0;i<3;++i){rsm_[i]=resources_.texture({extent_,extent_,Format::RGBA16Float,TextureUsage::ColorAttachment|TextureUsage::Sampled|TextureUsage::CopySource,"RSM atlas"});rsmViews_[i]=resources_.view(rsm_[i]);}
    sourceExtent_=std::min(sourceExtent_,resources_.device->graphicsLimits().maxTextureDimension2D);
    for(uint32_t i=0;i<3;++i){sourceRsm_[i]=resources_.texture({sourceExtent_,sourceExtent_,Format::RGBA16Float,TextureUsage::ColorAttachment|TextureUsage::Sampled|TextureUsage::CopySource,"Sun/sky/spot RSM"});sourceRsmViews_[i]=resources_.view(sourceRsm_[i]);}
    sourceDepth_=resources_.texture({sourceExtent_,sourceExtent_,Format::Depth32Float,TextureUsage::DepthAttachment,"RSM source depth"});sourceDepthView_=resources_.view(sourceDepth_);
    GraphicsPipelineDesc p;p.vertex=asset(directory,"forward.vert");p.fragment=asset(directory,"shadow.frag");p.vertexStride=sizeof(MeshVertex);p.attributes=GpuMesh::attributes();auto geometry=objectLayout();geometry.entries.push_back({2,BindingType::UniformBuffer,ShaderStage::Fragment,"RsmLight",64});geometry.entries.push_back({4,BindingType::SampledTexture,ShaderStage::Fragment,"skyIrradiance",0});p.bindings={geometry,GpuMaterial::rsmLayout()};p.colorFormat=Format::RGBA16Float;p.additionalColorFormats={Format::RGBA16Float,Format::RGBA16Float};p.depthAttachment=p.depthTest=p.depthWrite=true;p.label="Alpha-aware shadow depth";pipeline_=resources_.pipeline(p);if(resources_.device->computeLimits().supported){p.vertex=asset(directory,"instanced.vert");p.bindings[0].entries.push_back({3,BindingType::StorageRead,ShaderStage::Vertex,"OutPose",64});instanced_=resources_.pipeline(p);}
    parameters_=resources_.buffer({sizeof(ShadowParameters),BufferUsage::Uniform|BufferUsage::CopyDestination,"Shadow atlas parameters"});
}
void ShadowRenderer::render(const FrameData& frame,const std::vector<DrawPacket>& packets) {
    if(frame.lights.size()>30 || !std::isfinite(frame.nearPlane) || !std::isfinite(frame.farPlane) || frame.nearPlane<=0 || frame.farPlane<=frame.nearPlane)throw std::invalid_argument("Shadow: invalid camera/light parameters");
    const auto& settings=frame.shadowSettings;
    for(float x:{settings.distance,settings.cascadeBlend,settings.depthBias,settings.sunAngularRadius,settings.localLightRadius,settings.maxFilterTexels})
        if(!std::isfinite(x))throw std::invalid_argument("Shadow: nonfinite setting");
    if(settings.distance<=frame.nearPlane || settings.cascadeBlend<0 || settings.cascadeBlend>.3f || settings.depthBias<0 ||
       settings.sunAngularRadius<0 || settings.sunAngularRadius>.1f || settings.localLightRadius<0 || settings.maxFilterTexels<1 || settings.maxFilterTexels>64)
        throw std::invalid_argument("Shadow: setting outside domain");
    uint32_t required=0;for(const auto& light:frame.lights)required+=light.positionType.w==0?5:light.positionType.w==1?6:1;
    const uint32_t columns=std::max(1u,uint32_t(std::ceil(std::sqrt(float(required))))),tileSize=extent_/columns;
    if(tileSize<8)throw std::invalid_argument("Shadow tiles too small for filtering");
    glm::vec3 minimum(1e30f),maximum(-1e30f);
    for(const auto& packet:packets) {
        if(!packet.mesh || !packet.material || packet.mesh->owner()!=resources_.device.get() || packet.material->owner()!=resources_.device.get())throw std::invalid_argument("Shadow: foreign draw packet");
        for(int i=0;i<8;++i){const auto a=packet.mesh->boundsMin(),b=packet.mesh->boundsMax();glm::vec3 p=(packet.model*glm::vec4(i&1?b.x:a.x,i&2?b.y:a.y,i&4?b.z:a.z,1));minimum=glm::min(minimum,p);maximum=glm::max(maximum,p);}
    }
    if(packets.empty()){minimum=glm::vec3(-1);maximum=glm::vec3(1);}
    const auto center=(minimum+maximum)*.5f;const float radius=std::max(glm::length(maximum-minimum)*.5f,1.f);
    const float far=std::min(frame.farPlane,settings.distance);
    std::array<float,6> splits;splits[0]=frame.nearPlane;for(int i=1;i<=5;++i){const float t=float(i)/5;splits[i]=.7f*frame.nearPlane*std::pow(far/frame.nearPlane,t)+.3f*(frame.nearPlane+(far-frame.nearPlane)*t);}
    const auto inverse=glm::inverse(frame.viewProjection),inverseView=glm::inverse(frame.view);
    const auto projection=frame.viewProjection*inverseView;const glm::vec3 eye=inverseView[3];
    std::array<glm::vec3,8> corners;
    for(int i=0;i<4;++i){
        auto p=inverse*glm::vec4(i&1?1.f:-1.f,i&2?1.f:-1.f,0,1);
        if(!std::isfinite(p.w) || std::abs(p.w)<1e-12f)throw std::invalid_argument("Shadow: singular camera projection");
        corners[i]=glm::vec3(p)/p.w;
        if(std::abs(projection[3][3])<.5f){
            float depth=-(frame.view*glm::vec4(corners[i],1)).z;
            corners[i+4]=eye+(corners[i]-eye)*(frame.farPlane/depth);
        }else corners[i+4]=corners[i]+glm::vec3(inverseView*glm::vec4(0,0,-(frame.farPlane-frame.nearPlane),0));
    }
    data_={};data_.cameraView=frame.view;data_.settings={settings.depthBias,frame.shadows?1.f:0.f,frame.rsm?frame.rsmSettings.intensity:0.f,float(sourceExtent_)};
    data_.filter={settings.pcss?1.f:0.f,frame.sky?frame.atmosphere.radii.y:settings.sunAngularRadius,settings.localLightRadius,std::min(settings.maxFilterTexels,std::max(1.f,tileSize*.25f-1.f))};
    data_.cascades={frame.nearPlane,far,settings.cascadeBlend,.1f};
    uint32_t tiles=0;
    const glm::vec3 faces[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}},ups[]={{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}};
    for(size_t light=0;light<frame.lights.size();++light) {
        const auto& l=frame.lights[light];const int type=int(l.positionType.w);if(type<0 || type>2)throw std::invalid_argument("Shadow: invalid light type");
        const auto direction=glm::vec3(l.directionOuter);if(type!=1 && glm::dot(direction,direction)<1e-10f)throw std::invalid_argument("Shadow: zero light direction");
        const uint32_t count=type==0?5:type==1?6:1;data_.lights[light]={int(tiles),int(count),type,0};data_.splits[light]={splits[1],splits[2],splits[3],splits[4]};
        for(uint32_t face=0;face<count;++face) {
            auto matrix=glm::mat4(1);
            if(type==0) {
                glm::vec3 cascadeCenter(0);std::array<glm::vec3,8> part;
                const float sliceNear=face?splits[face]-settings.cascadeBlend*(splits[face]-splits[face-1]):splits[0];
                const float low=(sliceNear-frame.nearPlane)/(frame.farPlane-frame.nearPlane), high=(splits[face+1]-frame.nearPlane)/(frame.farPlane-frame.nearPlane);
                for(int i=0;i<4;++i){part[i]=glm::mix(corners[i],corners[i+4],low);part[i+4]=glm::mix(corners[i],corners[i+4],high);cascadeCenter+=part[i]+part[i+4];}cascadeCenter/=8.f;
                float r=.1f;for(const auto& p:part)r=std::max(r,glm::length(p-cascadeCenter));r=std::ceil(r*16)/16;const uint32_t guard=settings.pcss?uint32_t(std::ceil(data_.filter.w))+1:2;
                r*=float(tileSize)/float(tileSize-2*guard);
                const auto axis=glm::normalize(direction),right=glm::normalize(glm::cross(axis,safeUp(axis))),up=glm::cross(right,axis);
                const float texel=2*r/tileSize;
                cascadeCenter+=right*(std::round(glm::dot(cascadeCenter,right)/texel)*texel-glm::dot(cascadeCenter,right))+
                               up*(std::round(glm::dot(cascadeCenter,up)/texel)*texel-glm::dot(cascadeCenter,up));
                const auto orientation=glm::lookAt(glm::vec3(0),axis,safeUp(axis));
                float zMin=1e30f,zMax=-1e30f;
                auto include=[&](glm::vec3 p){float z=(orientation*glm::vec4(p,1)).z;zMin=std::min(zMin,z);zMax=std::max(zMax,z);};
                for(int i=0;i<8;++i)include({i&1?maximum.x:minimum.x,i&2?maximum.y:minimum.y,i&4?maximum.z:minimum.z});
                for(int i=0;i<4;++i){include(corners[i]);include(glm::mix(corners[i],corners[i+4],(far-frame.nearPlane)/(frame.farPlane-frame.nearPlane)));}
                const float padding=1.f,zCenter=(orientation*glm::vec4(cascadeCenter,1)).z;
                const auto view=glm::lookAt(cascadeCenter-axis*(zMax-zCenter+padding),cascadeCenter,safeUp(axis));
                const float depthRange=std::max(zMax-zMin+2*padding,.2f);
                matrix=depthCorrection()*glm::ortho(-r,r,-r,r,0.f,depthRange)*view;
                data_.lightDepth[light]={0,depthRange,1,0};
            } else {
                const auto position=glm::vec3(l.positionType);const float range=std::max(glm::length(center-position)+radius*2,.2f);
                const auto axis=type==1?faces[face]:glm::normalize(direction);const auto up=type==1?ups[face]:safeUp(axis);
                const float fov=type==1?glm::radians(90.f):std::clamp(2*std::acos(std::clamp(l.directionOuter.w,-.999f,.999f)),.02f,3.12f);
                matrix=depthCorrection()*glm::perspective(fov,1.f,.05f,range)*glm::lookAt(position,position+axis,up);
                data_.lightDepth[light]={.05f,range,0,std::tan(fov*.5f)};
            }
            data_.matrices[tiles]=matrix;data_.rects[tiles]={float(tiles%columns*tileSize)/extent_,float(tiles/columns*tileSize)/extent_,float(tileSize)/extent_,float(tileSize)/extent_};++tiles;
        }
    }
    const auto& rsm=frame.rsmSettings;
    if(!std::isfinite(rsm.worldRadius)||!std::isfinite(rsm.intensity)||!std::isfinite(rsm.sampleRadius)||!std::isfinite(rsm.minDistance)||rsm.worldRadius<1 || rsm.intensity<0 || rsm.sampleRadius<=0 || rsm.sampleRadius>1 || rsm.minDistance<=0 || rsm.sampleCount<1 || rsm.sampleCount>256)throw std::invalid_argument("RSM: invalid settings");
    data_.rsmSettings={rsm.sampleRadius,float(rsm.sampleCount),rsm.minDistance,rsm.indirectOnly&&frame.rsm?1.f:0.f};
    struct alignas(16) Capture {LightData light;glm::vec4 settings;};Capture bounce{};bool source=false;
    auto sun=std::find_if(frame.lights.begin(),frame.lights.end(),[](const LightData& l){return l.positionType.w==0;});
    if(rsm.useSunSky && (sun!=frame.lights.end() || frame.sky)){
        source=true;if(sun!=frame.lights.end())bounce.light=*sun;else {const float angle=glm::radians(frame.sunAngle),azimuth=glm::radians(frame.sunAzimuth);bounce.light.directionOuter={-std::cos(angle)*std::sin(azimuth),-std::sin(angle),std::cos(angle)*std::cos(azimuth),0};}
        if(!rsm.sunBounce || !frame.directionalEnabled)bounce.light.colorInner=glm::vec4(0);bounce.settings={frame.sky&&rsm.skyBounce?1.f:0.f,1,0,0};
        const auto axis=glm::normalize(glm::vec3(bounce.light.directionOuter));auto center=frame.cameraPosition+glm::vec3(glm::inverse(frame.view)*glm::vec4(0,0,-1,0))*(rsm.worldRadius*.5f);
        const auto right=glm::normalize(glm::cross(axis,safeUp(axis))),up=glm::cross(right,axis);const float texel=2*rsm.worldRadius/sourceExtent_;center-=right*std::fmod(glm::dot(center,right),texel)+up*std::fmod(glm::dot(center,up),texel);
        data_.rsmMatrix=depthCorrection()*glm::ortho(-rsm.worldRadius,rsm.worldRadius,-rsm.worldRadius,rsm.worldRadius,.1f,4*rsm.worldRadius)*glm::lookAt(center-axis*(2*rsm.worldRadius),center,safeUp(axis));
    }else {auto spot=std::find_if(frame.lights.begin(),frame.lights.end(),[](const LightData& l){return l.positionType.w==2;});if(spot!=frame.lights.end()){source=true;bounce.light=*spot;bounce.settings={0,1,0,0};const auto index=size_t(spot-frame.lights.begin());data_.rsmMatrix=data_.matrices[data_.lights[index].x];}}
    if(source && frame.rsm){data_.rsmRect={0,0,1,1};}else data_.settings.z=0;
    Resources frameResources(resources_.device);auto commands=resources_.device->createCommandList();rhi::RenderPassDesc clear;clear.depth=view_;clear.color=rsmViews_[0];clear.clearColor={0,0,0,0};clear.additionalColors={{rsmViews_[1]},{rsmViews_[2]}};commands.beginRenderPass(clear);commands.endRenderPass();
    std::vector<rhi::BufferHandle> objects;
    for(const auto& packet:packets){const std::array<glm::mat4,2> object{packet.model,glm::transpose(glm::inverse(packet.model))};objects.push_back(frameResources.buffer({sizeof(object),rhi::BufferUsage::Uniform,"Shadow object"},object.data()));}
    uint32_t lightIndex=0;
    const uint32_t drawnTiles=frame.shadows||frame.rsm?tiles:0;
    for(uint32_t iteration=0;iteration<drawnTiles+(source&&frame.rsm?1u:0u);++iteration) {
        const bool indirect=iteration==drawnTiles;const uint32_t tile=indirect?180:iteration;
        while(!indirect && lightIndex+1<frame.lights.size() && tile>=uint32_t(data_.lights[lightIndex].x+data_.lights[lightIndex].y))++lightIndex;
        Capture capture=indirect?bounce:Capture{frame.lights[lightIndex],glm::vec4(0)};auto light=frameResources.buffer({sizeof(Capture),rhi::BufferUsage::Uniform,"RSM light"},&capture);
        auto layout=objectLayout();layout.entries.push_back({2,rhi::BindingType::UniformBuffer,rhi::ShaderStage::Fragment,"RsmLight",64});layout.entries.push_back({4,rhi::BindingType::SampledTexture,rhi::ShaderStage::Fragment,"skyIrradiance",0});
        auto camera=frameResources.buffer({64,rhi::BufferUsage::Uniform,"Shadow face/cascade matrix"},indirect?&data_.rsmMatrix:&data_.matrices[tile]);
        rhi::RenderPassDesc pass;pass.depth=view_;pass.color=rsmViews_[0];pass.colorLoad=rhi::LoadOp::Load;pass.additionalColors={{rsmViews_[1],rhi::LoadOp::Load},{rsmViews_[2],rhi::LoadOp::Load}};pass.depthLoad=rhi::LoadOp::Load;pass.viewport={tile%columns*tileSize,tile/columns*tileSize,tileSize,tileSize};if(indirect){pass={};pass.depth=sourceDepthView_;pass.color=sourceRsmViews_[0];pass.clearColor={0,0,0,0};pass.additionalColors={{sourceRsmViews_[1]},{sourceRsmViews_[2]}};}commands.beginRenderPass(pass);commands.bindPipeline(pipeline_);
        for(size_t i=0;i<packets.size();++i){auto objectLayout=layout;std::vector<rhi::BindingEntry> entries{{0,camera,0,64,{},{}},{1,objects[i],0,128,{},{}},{2,light,0,64,{},{}},{4,{},0,0,skyIrradiance_,skySampler_}};if(packets[i].mesh->instances()){objectLayout.entries.push_back({3,rhi::BindingType::StorageRead,rhi::ShaderStage::Vertex,"OutPose",64});entries.push_back({3,packets[i].mesh->instances(),0,size_t(packets[i].mesh->instanceCapacity())*64,{},{}});commands.bindPipeline(instanced_);}else commands.bindPipeline(pipeline_);auto bindings=frameResources.bindings({objectLayout,entries});commands.bindBindingSet(bindings);packets[i].material->bindRsm(commands);packets[i].mesh->draw(commands);}commands.endRenderPass();
    }
    resources_.device->writeBuffer(parameters_,0,sizeof(data_),&data_);resources_.device->submit(commands);
}
}
