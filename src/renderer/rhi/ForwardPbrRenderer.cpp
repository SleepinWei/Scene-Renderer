#include "renderer/rhi/ForwardPbrRenderer.h"
#include "rhi/ShaderAssets.h"
#include "renderer/rhi/ShadowRenderer.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <json/json.hpp>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace render {
namespace {
struct alignas(16) LightingBlock { glm::vec4 cameraAmbient;glm::ivec4 counts;std::array<LightData, 30> lights{}; };
struct alignas(16) SkyBlock {glm::mat4 inverseVP;glm::vec4 camera,settings,sunDirectionRadius,sunRadiance;};
struct alignas(16) EffectsBlock {glm::mat4 view,projection;glm::vec4 settings;};
static_assert(sizeof(SkyBlock)==128,"Sky display ABI");
static_assert(sizeof(EffectsBlock)==144,"Effects ABI");
struct alignas(16) ObjectBlock { glm::mat4 model, normalMatrix; };
static_assert(sizeof(LightData) == 48 && offsetof(LightData, colorInner) == 16 && offsetof(LightData, directionOuter) == 32, "Light std140 ABI changed");
static_assert(sizeof(LightingBlock) == 1472 && offsetof(LightingBlock, counts) == 16 && offsetof(LightingBlock, lights) == 32, "Lighting std140 ABI changed");
static_assert(sizeof(ObjectBlock) == 128 && sizeof(glm::mat4) == 64, "Matrix std140 ABI changed");
rhi::ShaderAsset shader(const std::string& root, const std::string& name) { auto path = root + "/" + name;return {path + ".glsl", path + ".metallib", path + ".spv", path + ".json", "main0"}; }
void finiteMatrix(const glm::mat4& matrix) { for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) if (!std::isfinite(matrix[c][r])) throw std::invalid_argument("Renderer: nonfinite matrix"); }
void checkBlock(const rhi::ShaderAsset& asset, const char* block, std::initializer_list<std::pair<const char*, size_t>> fields, size_t bytes) {
    const auto json = nlohmann::json::parse(rhi::readShaderText(asset.reflectionPath));
    for (const auto& resource : json.at("ubos")) if (resource.at("name") == block) {
        if (resource.at("block_size") != bytes) throw std::invalid_argument("Renderer: reflected uniform block size differs");
        const auto& members = json.at("types").at(resource.at("type").get<std::string>()).at("members");
        for (const auto& field : fields) {
            bool found = false;for (const auto& member : members) if (member.at("name") == field.first && member.at("offset") == field.second) found = true;
            if (!found) throw std::invalid_argument("Renderer: reflected uniform offset differs");
        }return;
    }throw std::invalid_argument("Renderer: reflected uniform block is absent");
}
}
struct ForwardPbrRenderer::Targets {
    Resources resources;
    uint32_t width, height;
    rhi::TextureHandle hdr, depth, output;
    rhi::TextureViewHandle hdrView, depthView, outputView;
    rhi::BindingSetHandle toneBindings, deferredBindings, lightBindings,ssaoBindings,effectBindings;
    rhi::TextureHandle ao,opaque,motion,backDepth,backTest; rhi::TextureViewHandle aoView,opaqueView,motionView,backDepthView,backTestView;
    std::array<rhi::TextureHandle,6> gbuffer{};
    std::array<rhi::TextureViewHandle,6> gbufferViews{};
    Targets(std::shared_ptr<rhi::GraphicsDevice> d, uint32_t w, uint32_t h, rhi::BufferHandle tone, rhi::SamplerHandle sampler, rhi::BufferHandle lighting, PbrPath path,ShadowRenderer* shadow,rhi::BufferHandle effects,rhi::BufferHandle skyParams,rhi::TextureViewHandle sky,rhi::TextureViewHandle irradiance,rhi::SamplerHandle skySampler) : resources(std::move(d)), width(w), height(h) {
        using namespace rhi;
        hdr = resources.texture({w, h, Format::RGBA16Float, TextureUsage::ColorAttachment | TextureUsage::Sampled | TextureUsage::CopySource, "Forward HDR"});hdrView = resources.view(hdr);
        depth = resources.texture({w, h, Format::Depth32Float, TextureUsage::DepthAttachment|TextureUsage::Sampled|TextureUsage::CopySource, "Forward depth"});depthView = resources.view(depth);
        output = resources.texture({w, h, Format::RGBA8UNorm, TextureUsage::ColorAttachment | TextureUsage::CopySource | TextureUsage::Sampled, "Tone mapped output"});outputView = resources.view(output);
        BindingLayout layout{0, {{0, BindingType::UniformBuffer, ShaderStage::Fragment, "ToneMap", 16}, {1, BindingType::SampledTexture, ShaderStage::Fragment, "hdrBuffer", 0}}};
        toneBindings = resources.bindings({layout, {{0, tone, 0, 16, {}, {}}, {1, {}, 0, 0, hdrView, sampler}}});
        if (path != PbrPath::Forward) {
            BindingLayout images{1,{}};std::vector<BindingEntry> entries;
            const char* names[] = {"positionBuffer","normalBuffer","albedoBuffer","emissiveBuffer"};
            for (uint32_t i=0;i<4;++i) {
                gbuffer[i] = resources.texture({w,h,Format::RGBA16Float,TextureUsage::ColorAttachment | TextureUsage::Sampled | TextureUsage::CopySource,names[i]});gbufferViews[i] = resources.view(gbuffer[i]);
                images.entries.push_back({i+1,BindingType::SampledTexture,ShaderStage::Fragment,names[i],0});entries.push_back({i+1,{},0,0,gbufferViews[i],sampler});
            }
            if(path==PbrPath::Scene){
                backDepth=resources.texture({w,h,Format::RGBA32Float,TextureUsage::ColorAttachment|TextureUsage::Sampled|TextureUsage::CopySource,"SSS back-face depth"});backDepthView=resources.view(backDepth);backTest=resources.texture({w,h,Format::Depth32Float,TextureUsage::DepthAttachment,"SSS farthest depth"});backTestView=resources.view(backTest);
                BindingLayout effects{2,{}};std::vector<BindingEntry> effectEntries;
                for(uint32_t i=4;i<6;++i){gbuffer[i]=resources.texture({w,h,Format::RGBA16Float,TextureUsage::ColorAttachment|TextureUsage::Sampled|TextureUsage::CopySource,"Material effect G-buffer"});gbufferViews[i]=resources.view(gbuffer[i]);effects.entries.push_back({i-4,BindingType::SampledTexture,ShaderStage::Fragment,i==4?"materialEffectsBuffer":"tangentBuffer",0});effectEntries.push_back({i-4,{},0,0,gbufferViews[i],sampler});}
                effects.entries.push_back({2,BindingType::SampledTexture,ShaderStage::Fragment,"backDepthBuffer",0});effectEntries.push_back({2,{},0,0,backDepthView,sampler});effectBindings=resources.bindings({effects,effectEntries});
                opaque=resources.texture({w,h,Format::RGBA16Float,TextureUsage::CopyDestination|TextureUsage::Sampled|TextureUsage::CopySource,"Opaque HDR snapshot"});opaqueView=resources.view(opaque);
                motion=resources.texture({w,h,Format::RGBA16Float,TextureUsage::ColorAttachment|TextureUsage::Sampled|TextureUsage::CopySource,"Temporal motion"});motionView=resources.view(motion);
                ao=resources.texture({w,h,Format::RGBA16Float,TextureUsage::ColorAttachment|TextureUsage::Sampled|TextureUsage::CopySource,"SSAO"});aoView=resources.view(ao);
                BindingLayout ssaoImages{1,{images.entries[0],images.entries[1]}};
                ssaoBindings=resources.bindings({ssaoImages,{entries[0],entries[1]}});
                images.entries.push_back({5,BindingType::SampledTexture,ShaderStage::Fragment,"shadowAtlas",0});entries.push_back({5,{},0,0,shadow->view(),sampler});
                images.entries.push_back({6,BindingType::SampledTexture,ShaderStage::Fragment,"aoBuffer",0});entries.push_back({6,{},0,0,aoView,sampler});
                images.entries.push_back({7,BindingType::SampledTexture,ShaderStage::Fragment,"rsmFlux",0});entries.push_back({7,{},0,0,shadow->rsmSourceView(0),sampler});
            }
            deferredBindings = resources.bindings({images,entries});
            BindingLayout lights{0,{{2,BindingType::UniformBuffer,ShaderStage::Fragment,"SceneLighting",1472}}};std::vector<BindingEntry> lightEntries{{2,lighting,0,1472,{},{}}};
            if(path==PbrPath::Scene){
                lights.entries.push_back({0,BindingType::SampledTexture,ShaderStage::Fragment,"skyRadianceLut",0});lightEntries.push_back({0,{},0,0,sky,skySampler});
                lights.entries.push_back({1,BindingType::UniformBuffer,ShaderStage::Fragment,"SkyData",128});lightEntries.push_back({1,skyParams,0,128,{},{}});
                lights.entries.push_back({6,BindingType::SampledTexture,ShaderStage::Fragment,"skyIrradianceLut",0});lightEntries.push_back({6,{},0,0,irradiance,skySampler});
                lights.entries.push_back({3,BindingType::UniformBuffer,ShaderStage::Fragment,"ShadowData",sizeof(ShadowParameters)});lightEntries.push_back({3,shadow->parameters(),0,sizeof(ShadowParameters),{},{}});
                for(uint32_t i=0;i<2;++i){lights.entries.push_back({4+i,BindingType::SampledTexture,ShaderStage::Fragment,i==0?"rsmPosition":"rsmNormal",0});lightEntries.push_back({4+i,{},0,0,shadow->rsmSourceView(i+1),sampler});}}
            lightBindings=resources.bindings({lights,lightEntries});
        }
    }
};
rhi::BindingLayout ForwardPbrRenderer::frameLayout() {
    using namespace rhi;
    return {0, {{0, BindingType::UniformBuffer, ShaderStage::Vertex, "CameraVertex", 64},
        {1, BindingType::UniformBuffer, ShaderStage::Vertex, "ObjectData", 128},
        {2, BindingType::UniformBuffer, ShaderStage::Fragment, "SceneLighting", 1472}}};
}
ForwardPbrRenderer::~ForwardPbrRenderer() = default;
ForwardPbrRenderer::ForwardPbrRenderer(std::shared_ptr<rhi::GraphicsDevice> device, const std::string& directory, uint32_t width, uint32_t height, PbrPath path) : resources_(std::move(device)), path_(path), geometryLayout_(frameLayout()) {
    using namespace rhi;directory_=directory;
    GraphicsPipelineDesc p;p.vertex = shader(directory,"forward.vert");p.fragment = shader(directory,"forward.frag");p.vertexStride = sizeof(MeshVertex);
    if (path_ != PbrPath::Forward) { p.fragment = shader(directory,"gbuffer.frag");geometryLayout_.entries.pop_back();p.additionalColorFormats = {Format::RGBA16Float,Format::RGBA16Float,Format::RGBA16Float}; }
    if(path_==PbrPath::Scene){p.fragment=shader(directory,"scene-gbuffer.frag");p.additionalColorFormats.insert(p.additionalColorFormats.end(),2,Format::RGBA16Float);}
    p.attributes = GpuMesh::attributes();p.bindings = {geometryLayout_, GpuMaterial::layout()};p.colorFormat = Format::RGBA16Float;p.depthAttachment = p.depthTest = p.depthWrite = true;p.label = "Forward Cook-Torrance PBR";
    checkBlock(p.vertex,"CameraVertex",{{"viewProjection",0}},64);checkBlock(p.vertex,"ObjectData",{{"model",0},{"normalMatrix",64}},128);
    checkBlock(p.fragment,"MaterialData",{{"albedoAlpha",0},{"factors",16},{"emissiveNormal",32}},48);
    if (path_ == PbrPath::Forward) checkBlock(p.fragment,"SceneLighting",{{"cameraAmbient",0},{"counts",16},{"lights",32}},1472);
    forward_ = resources_.pipeline(p);
    if(resources_.device->supportsWireframe()){auto wire=p;wire.wireframe=true;wireframe_=resources_.pipeline(wire);}
    if(resources_.device->computeLimits().supported){auto instance=p;instance.vertex=shader(directory,"instanced.vert");instance.bindings[0].entries.push_back({3,BindingType::StorageRead,ShaderStage::Vertex,"OutPose",64});instanced_=resources_.pipeline(instance);if(resources_.device->supportsWireframe()){instance.wireframe=true;wireframeInstanced_=resources_.pipeline(instance);}}
    p = {};p.vertex = shader(directory,"tonemap.vert");p.fragment = shader(directory,"tonemap.frag");p.vertexStride = 16;
    p.attributes = {{0,VertexFormat::Float2,0},{1,VertexFormat::Float2,8}};
    p.bindings = {{0, {{0,BindingType::UniformBuffer,ShaderStage::Fragment,"ToneMap",16},{1,BindingType::SampledTexture,ShaderStage::Fragment,"hdrBuffer",0}}}};p.label = "HDR tone map";
    checkBlock(p.fragment,"ToneMap",{{"exposureGamma",0}},16);tonePipeline_ = resources_.pipeline(p);
    if(path_==PbrPath::Scene){auto back=rhi::GraphicsPipelineDesc{};back.vertex=shader(directory,"forward.vert");back.fragment=shader(directory,"material-backdepth.frag");back.vertexStride=32;back.attributes=GpuMesh::attributes();back.bindings={ShadowRenderer::objectLayout(),GpuMaterial::shadowLayout()};back.colorFormat=Format::RGBA32Float;back.depthAttachment=back.depthTest=back.depthWrite=true;back.depthCompare=DepthCompare::Greater;back.cull=CullMode::Front;backDepthPipeline_=resources_.pipeline(back);}
    camera_ = resources_.buffer({64,BufferUsage::Uniform | BufferUsage::CopyDestination,"PBR camera"});
    lighting_ = resources_.buffer({1472,BufferUsage::Uniform | BufferUsage::CopyDestination,"PBR lights"});
    tone_ = resources_.buffer({16,BufferUsage::Uniform | BufferUsage::CopyDestination,"PBR exposure"});
    const float quad[] = {-1,-1,0,1, 1,-1,1,1, 1,1,1,0, -1,-1,0,1, 1,1,1,0, -1,1,0,0};
    quad_ = resources_.buffer({sizeof(quad),BufferUsage::Vertex,"Tone map fullscreen"},quad);
    hdrSampler_ = resources_.sampler({Filter::Nearest,AddressMode::ClampToEdge});skySampler_=resources_.sampler({Filter::Linear,AddressMode::Repeat});
    if(path_==PbrPath::Scene){
        if(resources_.device->computeLimits().maxStorageImages){atmosphere_=std::make_unique<GpuAtmosphere>(resources_.device,directory);atmosphere_->update({},10);skyView_=atmosphere_->sky();irradianceView_=atmosphere_->irradiance();}
        else {auto black=resources_.texture({1,1,Format::RGBA8UNorm,TextureUsage::Sampled|TextureUsage::CopyDestination,"Unavailable atmosphere fallback"});const uint8_t data[]={0,0,0,255};resources_.device->writeTexture(black,data,4);skyView_=irradianceView_=resources_.view(black);}
        skyParameters_=resources_.buffer({128,BufferUsage::Uniform|BufferUsage::CopyDestination,"Sky display parameters"});
        shadows_=std::make_unique<ShadowRenderer>(resources_.device,directory,128,irradianceView_);
        effects_=resources_.buffer({144,BufferUsage::Uniform|BufferUsage::CopyDestination,"SSAO parameters"});
        p={};p.vertex=shader(directory,"tonemap.vert");p.fragment=shader(directory,"ssao.frag");p.vertexStride=16;p.attributes={{0,VertexFormat::Float2,0},{1,VertexFormat::Float2,8}};p.colorFormat=Format::RGBA16Float;
        p.bindings={{0,{{0,BindingType::UniformBuffer,ShaderStage::Fragment,"EffectsParameters",144}}},{1,{{1,BindingType::SampledTexture,ShaderStage::Fragment,"positionBuffer",0},{2,BindingType::SampledTexture,ShaderStage::Fragment,"normalBuffer",0}}}};
        effectsBindings_=resources_.bindings({p.bindings[0],{{0,effects_,0,144,{},{}}}});
        checkBlock(p.fragment,"EffectsParameters",{{"effectView",0},{"effectProjection",64},{"aoSettings",128}},144);ssaoPipeline_=resources_.pipeline(p);
    }
    if(path_==PbrPath::Scene && resources_.device->computeLimits().maxStorageImages){
        temporal_=std::make_unique<GpuTemporal>(resources_.device,directory);p={};p.vertex=shader(directory,"motion.vert");p.fragment=shader(directory,"motion.frag");p.vertexStride=32;p.attributes=GpuMesh::attributes();motionLayout_={0,{{0,BindingType::UniformBuffer,ShaderStage::Vertex,"MotionCamera",192},{1,BindingType::UniformBuffer,ShaderStage::Vertex,"MotionObject",144}}};p.bindings={motionLayout_,GpuMaterial::shadowLayout()};p.colorFormat=Format::RGBA16Float;p.depthAttachment=p.depthTest=true;p.depthCompare=DepthCompare::LessEqual;motionPipeline_=resources_.pipeline(p);
        p.vertex=shader(directory,"motion-instanced.vert");p.bindings[0].entries.push_back({3,BindingType::StorageRead,ShaderStage::Vertex,"OutPose",64});motionInstanced_=resources_.pipeline(p);
    }
    if(path_==PbrPath::Scene){
        transparentLayout_={2,{{0,BindingType::UniformBuffer,ShaderStage::Fragment,"SkyData",128},{1,BindingType::UniformBuffer,ShaderStage::Fragment,"ShadowData",sizeof(ShadowParameters)},{2,BindingType::SampledTexture,ShaderStage::Fragment,"skyRadianceLut",0},{3,BindingType::SampledTexture,ShaderStage::Fragment,"skyIrradianceLut",0},{4,BindingType::SampledTexture,ShaderStage::Fragment,"shadowAtlas",0}}};
        p={};p.vertex=shader(directory,"forward.vert");p.fragment=shader(directory,"transparent.frag");p.vertexStride=32;p.attributes=GpuMesh::attributes();p.bindings={frameLayout(),GpuMaterial::layout(),transparentLayout_};p.colorFormat=Format::RGBA16Float;p.additionalColorFormats={Format::RGBA16Float};p.attachmentBlend={true,false};p.depthAttachment=p.depthTest=true;p.depthWrite=false;transparentPipeline_=resources_.pipeline(p);
        if(resources_.device->computeLimits().supported){p.vertex=shader(directory,"instanced.vert");p.bindings[0].entries.push_back({3,BindingType::StorageRead,ShaderStage::Vertex,"OutPose",64});transparentInstanced_=resources_.pipeline(p);}
    }
    if(path_==PbrPath::Scene){
        sceneForwardLayout_=transparentLayout_;for(uint32_t i=0;i<3;++i)sceneForwardLayout_.entries.push_back({5+i,BindingType::SampledTexture,ShaderStage::Fragment,i==0?"indirectBuffer":i==1?"aoBuffer":"backDepthBuffer",0});
        p={};p.vertex=shader(directory,"forward.vert");p.fragment=shader(directory,"scene-forward.frag");p.vertexStride=32;p.attributes=GpuMesh::attributes();p.bindings={frameLayout(),GpuMaterial::layout(),sceneForwardLayout_};p.colorFormat=Format::RGBA16Float;p.depthAttachment=p.depthTest=true;p.depthWrite=false;p.depthCompare=DepthCompare::LessEqual;sceneForward_=resources_.pipeline(p);if(resources_.device->supportsWireframe()){auto wire=p;wire.wireframe=true;sceneForwardWire_=resources_.pipeline(wire);}
        if(resources_.device->computeLimits().supported){p.vertex=shader(directory,"instanced.vert");p.bindings[0].entries.push_back({3,BindingType::StorageRead,ShaderStage::Vertex,"OutPose",64});sceneForwardInstanced_=resources_.pipeline(p);if(resources_.device->supportsWireframe()){p.wireframe=true;sceneForwardWireInstanced_=resources_.pipeline(p);}}
    }
    resize(width,height);
    if (path_ != PbrPath::Forward) {
        p = {};p.vertex = shader(directory,"tonemap.vert");p.fragment = shader(directory,"deferred.frag");p.vertexStride = 16;
        p.attributes = {{0,VertexFormat::Float2,0},{1,VertexFormat::Float2,8}};p.colorFormat = Format::RGBA16Float;p.label = "Deferred PBR lighting";
        p.bindings = {{0,{{2,BindingType::UniformBuffer,ShaderStage::Fragment,"SceneLighting",1472}}},{1,{}}};
        const char* names[] = {"positionBuffer","normalBuffer","albedoBuffer","emissiveBuffer"};
        for (uint32_t i=0;i<4;++i) p.bindings[1].entries.push_back({i+1,BindingType::SampledTexture,ShaderStage::Fragment,names[i],0});
        if(path_==PbrPath::Scene){p.fragment=shader(directory,"scene-light.frag");p.bindings.push_back({2,{{0,BindingType::SampledTexture,ShaderStage::Fragment,"materialEffectsBuffer",0},{1,BindingType::SampledTexture,ShaderStage::Fragment,"tangentBuffer",0},{2,BindingType::SampledTexture,ShaderStage::Fragment,"backDepthBuffer",0}}});
            p.bindings[0].entries.push_back({0,BindingType::SampledTexture,ShaderStage::Fragment,"skyRadianceLut",0});p.bindings[0].entries.push_back({1,BindingType::UniformBuffer,ShaderStage::Fragment,"SkyData",128});p.bindings[0].entries.push_back({6,BindingType::SampledTexture,ShaderStage::Fragment,"skyIrradianceLut",0});
            checkBlock(p.fragment,"SkyData",{{"inverseViewProjection",0},{"skyCamera",64},{"skySettings",80},{"sunDirectionRadius",96},{"sunRadiance",112}},128);p.bindings[0].entries.push_back({3,BindingType::UniformBuffer,ShaderStage::Fragment,"ShadowData",sizeof(ShadowParameters)});
            p.bindings[0].entries.push_back({4,BindingType::SampledTexture,ShaderStage::Fragment,"rsmPosition",0});p.bindings[0].entries.push_back({5,BindingType::SampledTexture,ShaderStage::Fragment,"rsmNormal",0});
            p.bindings[1].entries.push_back({5,BindingType::SampledTexture,ShaderStage::Fragment,"shadowAtlas",0});p.bindings[1].entries.push_back({6,BindingType::SampledTexture,ShaderStage::Fragment,"aoBuffer",0});p.bindings[1].entries.push_back({7,BindingType::SampledTexture,ShaderStage::Fragment,"rsmFlux",0});
            checkBlock(p.fragment,"ShadowData",{{"shadowCameraView",0},{"shadowMatrices",64},{"shadowRects",11584},{"shadowLights",14464},{"shadowSplits",14944},{"shadowSettings",15424}},sizeof(ShadowParameters));
        }
        checkBlock(p.fragment,"SceneLighting",{{"cameraAmbient",0},{"counts",16},{"lights",32}},1472);deferredPipeline_ = resources_.pipeline(p);
    }
}
void ForwardPbrRenderer::resize(uint32_t width, uint32_t height) {
    if (targets_ && targets_->width == width && targets_->height == height) return;
    auto targets = std::make_unique<Targets>(resources_.device,width,height,tone_,hdrSampler_,lighting_,path_,shadows_.get(),effects_,skyParameters_,skyView_,irradianceView_,skySampler_);if(temporal_)temporal_->resize(width,height);targets_.swap(targets);previousModels_.clear();temporalOutput_=false;
}
void ForwardPbrRenderer::resetTemporal() {
    if(temporal_)temporal_->reset();previousModels_.clear();previousTaa_=false;temporalOutput_=false;
}
void ForwardPbrRenderer::render(const FrameData& source, const std::vector<DrawPacket>& packets, float exposure, float gamma) {
    FrameData frame=source;
    SunState solar;
    if (!std::isfinite(exposure) || exposure < 0 || !std::isfinite(gamma) || gamma <= 0 || !std::isfinite(frame.ambient) || frame.ambient < 0 || frame.lights.size() > 30)
        throw std::invalid_argument("Renderer: invalid frame parameters");
    finiteMatrix(frame.viewProjection);
    for (int i = 0; i < 3; ++i) if (!std::isfinite(frame.cameraPosition[i])) throw std::invalid_argument("Renderer: invalid camera position");
    if(frame.sky) {
        const float atmosphereHeight=frame.atmosphere.radii.z-frame.atmosphere.radii.w;
        if(!std::isfinite(atmosphereHeight) || atmosphereHeight<=.002f || !std::isfinite(frame.seaLevelMeters))throw std::invalid_argument("Atmosphere invalid height/sea level");
        float elevation=glm::radians(frame.sunAngle),azimuth=glm::radians(frame.sunAzimuth);
        solar.direction={std::cos(elevation)*std::sin(azimuth),std::sin(elevation),-std::cos(elevation)*std::cos(azimuth)};
        solar.irradiance=glm::vec3(frame.atmosphere.radii.x);
        solar.observerHeightKm=std::clamp((frame.cameraPosition.y-frame.seaLevelMeters)*.001f,.001f,atmosphereHeight-.001f);
        solar.multipleScattering=frame.multipleScattering;solar.groundAlbedo=frame.groundAlbedo;
        auto sun=std::find_if(frame.lights.begin(),frame.lights.end(),[](const auto& l){return l.positionType.w==0;});
        if(sun!=frame.lights.end()) {
            if(glm::dot(glm::vec3(sun->directionOuter),glm::vec3(sun->directionOuter))<1e-10f)throw std::invalid_argument("Sun needs a nonzero direction");
            solar.direction=-glm::normalize(glm::vec3(sun->directionOuter));solar.irradiance=glm::vec3(sun->colorInner);
        }
        // Update validates solar/atmospheric domains before any integration or draw.
        if(!atmosphere_)throw std::invalid_argument("Atmosphere compute unavailable on this renderer path/backend");
        atmosphere_->update(frame.atmosphere,solar);
        if(sun!=frame.lights.end())sun->colorInner=glm::vec4(solar.irradiance*solarTransmittance(frame.atmosphere,solar),sun->colorInner.w);
        // Invalidate temporal color when lighting changes, while preserving normal camera reprojection.
        auto hash=[&](float x){uint32_t bits;std::memcpy(&bits,&x,4);frame.historyKey^=uint64_t(bits)+0x9e3779b97f4a7c15ull+(frame.historyKey<<6)+(frame.historyKey>>2);};
        for(float x:{solar.direction.x,solar.direction.y,solar.direction.z,solar.irradiance.x,solar.irradiance.y,solar.irradiance.z,solar.multipleScattering,solar.groundAlbedo})hash(x);
        const auto* parameters=reinterpret_cast<const float*>(&frame.atmosphere);for(size_t i=0;i<24;++i)hash(parameters[i]);
    }
    if(!frame.directionalEnabled)for(auto& light:frame.lights)if(light.positionType.w==0)light.colorInner=glm::vec4(0);
    if(frame.taa && !temporal_)throw std::invalid_argument("Temporal compute unavailable on this path/backend");
    if(temporal_){const auto projection=frame.viewProjection*glm::inverse(frame.view);float difference=0;for(int c=0;c<4;++c)for(int r=0;r<4;++r)difference=std::max(difference,std::abs(projection[c][r]-previousProjection_[c][r]));
        if(!frame.taa || !previousTaa_ || historyKey_!=frame.historyKey || difference>1e-4f || glm::length(frame.cameraPosition-previousCamera_)>2) {temporal_->reset();previousModels_.clear();}
        if(frame.taa){const auto j=GpuTemporal::jitter(temporal_->samples());glm::mat4 shift(1);shift[3].x=j.x*2/targets_->width;shift[3].y=-j.y*2/targets_->height;frame.viewProjection=shift*frame.viewProjection;}
    }
    LightingBlock lighting{};lighting.cameraAmbient = glm::vec4(frame.cameraPosition,frame.ambient);lighting.counts.x = int(frame.lights.size());lighting.counts.y=frame.inverseSquareLocalLights?1:0;
    for (size_t i = 0; i < frame.lights.size(); ++i) {
        auto light = frame.lights[i];for (const auto& v : {light.positionType,light.colorInner,light.directionOuter}) for (int c = 0; c < 4; ++c)
            if (!std::isfinite(v[c])) throw std::invalid_argument("Renderer: invalid light");
        if (light.positionType.w != 0 && light.positionType.w != 1 && light.positionType.w != 2) throw std::invalid_argument("Renderer: invalid light type");
        if (light.positionType.w != 1) {
            if (glm::dot(glm::vec3(light.directionOuter),glm::vec3(light.directionOuter)) < 1e-10f) throw std::invalid_argument("Renderer: zero light direction");
            light.directionOuter = glm::vec4(glm::normalize(glm::vec3(light.directionOuter)),light.directionOuter.w);
        }
        for (int c = 0; c < 3; ++c) if (light.colorInner[c] < 0) throw std::invalid_argument("Renderer: negative light radiance");
        if (light.positionType.w == 2 && (light.colorInner.w <= light.directionOuter.w || light.colorInner.w > 1 || light.directionOuter.w < -1)) throw std::invalid_argument("Renderer: invalid spot cone");
        lighting.lights[i] = light;
    }
    std::vector<ObjectBlock> blocks;blocks.reserve(packets.size());
    for (const auto& packet : packets) {
        if (!packet.mesh || !packet.material || packet.mesh->owner() != owner() || packet.material->owner() != owner()) throw std::invalid_argument("Renderer: foreign draw packet");
        finiteMatrix(packet.model);const float determinant = glm::determinant(packet.model);
        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12f) throw std::invalid_argument("Renderer: singular object transform");
        ObjectBlock block{packet.model,glm::transpose(glm::inverse(packet.model))};finiteMatrix(block.normalMatrix);blocks.push_back(block);
    }
    if(frame.sky && !atmosphere_)throw std::invalid_argument("Atmosphere compute unavailable on this renderer path/backend");
    if(!frame.oceans.empty() && path_!=PbrPath::Scene)throw std::invalid_argument("Ocean surface requires scene renderer");
    std::map<uint64_t,bool> used;
    for(const auto& ocean:frame.oceans){if(!ocean.id || used.count(ocean.id))throw std::invalid_argument("Duplicate ocean identifier");used[ocean.id]=true;auto& surface=oceans_[ocean.id];if(!surface || !surface->compatible(ocean))surface=std::make_unique<OceanSurface>(resources_.device,directory_,ocean);surface->simulate(frame.timeSeconds,ocean);}
    for(auto it=oceans_.begin();it!=oceans_.end();)if(!used.count(it->first))it=oceans_.erase(it);else ++it;
    if(shadows_)shadows_->render(frame,packets);
    auto& device = *resources_.device;
    if(shadows_){const auto transmission=frame.sky?solarTransmittance(frame.atmosphere,solar,false):glm::vec3(0);
        const float diskArea=3.14159265359f*std::pow(std::sin(frame.atmosphere.radii.y),2);
        SkyBlock sky{glm::inverse(frame.viewProjection),glm::vec4(frame.cameraPosition,0),{frame.sky?1.f:0.f,frame.forwardShading?1.f:0.f,frame.sky?atmosphereHorizon(frame.atmosphere,solar.observerHeightKm):0,1},glm::vec4(solar.direction,frame.atmosphere.radii.y),glm::vec4(solar.irradiance*transmission/std::max(diskArea,1e-8f),0)};device.writeBuffer(skyParameters_,0,sizeof(sky),&sky);finiteMatrix(frame.view);EffectsBlock effects{frame.view,frame.viewProjection*glm::inverse(frame.view),{frame.aoRadius,frame.aoBias,frame.aoPower,frame.ssao?1.f:0.f}};device.writeBuffer(effects_,0,sizeof(effects),&effects);}
    device.writeBuffer(camera_,0,64,&frame.viewProjection);device.writeBuffer(lighting_,0,sizeof(lighting),&lighting);
    const glm::vec4 tone(exposure,gamma,frame.toneMapping?0.f:1.f,0);device.writeBuffer(tone_,0,16,&tone);
    while (objects_.size() < packets.size()) {
        auto data = resources_.buffer({128,rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDestination,"PBR object"});
        std::vector<rhi::BindingEntry> entries{{0,camera_,0,64,{},{}},{1,data,0,128,{},{}}};
        if (path_ == PbrPath::Forward) entries.push_back({2,lighting_,0,1472,{},{}});
        auto bindings = resources_.bindings({geometryLayout_,entries});
        objects_.push_back({data,bindings});
    }
    Resources frameResources(resources_.device);
    auto commands = device.createCommandList();rhi::RenderPassDesc pass;pass.color = targets_->hdrView;pass.depth = targets_->depthView;pass.clearColor = {0,0,0,1};
    if (path_ != PbrPath::Forward) {
        pass.color = targets_->gbufferViews[0];pass.clearColor = {0,0,0,0};
        for (size_t i=1;i<(path_==PbrPath::Scene?6u:4u);++i) pass.additionalColors.push_back({targets_->gbufferViews[i]});
    }
    commands.beginRenderPass(pass);commands.bindPipeline(forward_);
    for (size_t i = 0; i < packets.size(); ++i) {
        device.writeBuffer(objects_[i].data,0,128,&blocks[i]);if(path_==PbrPath::Scene && packets[i].material->transparent())continue;if(packets[i].mesh->instances()){auto layout=geometryLayout_;layout.entries.push_back({3,rhi::BindingType::StorageRead,rhi::ShaderStage::Vertex,"OutPose",64});std::vector<rhi::BindingEntry> entries{{0,camera_,0,64,{},{}},{1,objects_[i].data,0,128,{},{}},{3,packets[i].mesh->instances(),0,size_t(packets[i].mesh->instanceCapacity())*64,{},{}}};if(path_==PbrPath::Forward)entries.push_back({2,lighting_,0,1472,{},{}});commands.bindPipeline(packets[i].wireframe?wireframeInstanced_:instanced_);commands.bindBindingSet(frameResources.bindings({layout,entries}));}else {commands.bindPipeline(packets[i].wireframe?wireframe_:forward_);commands.bindBindingSet(objects_[i].bindings);}packets[i].material->bind(commands);packets[i].mesh->draw(commands);
    }
    commands.endRenderPass();pass = {};pass.clearColor = {0,0,0,1};
    if(shadows_){
        pass.color=targets_->aoView;pass.clearColor={1,1,1,1};commands.beginRenderPass(pass);commands.bindPipeline(ssaoPipeline_);
        commands.bindBindingSet(effectsBindings_);commands.bindBindingSet(targets_->ssaoBindings);commands.bindVertexBuffer(quad_);commands.draw(6);commands.endRenderPass();
    }
    if(shadows_){pass={};pass.color=targets_->backDepthView;pass.clearColor={0,0,0,0};pass.depth=targets_->backTestView;pass.clearDepth=0;commands.beginRenderPass(pass);commands.bindPipeline(backDepthPipeline_);
        for(size_t i=0;i<packets.size();++i)if(packets[i].material->extension().lobes.w>0 && !packets[i].mesh->instances()){auto b=frameResources.bindings({ShadowRenderer::objectLayout(),{{0,camera_,0,64,{},{}},{1,objects_[i].data,0,128,{},{}}}});commands.bindBindingSet(b);packets[i].material->bindShadow(commands);packets[i].mesh->draw(commands);}commands.endRenderPass();pass={};}
    if (path_ != PbrPath::Forward) {
        pass.clearColor={0,0,0,1};pass.color = targets_->hdrView;commands.beginRenderPass(pass);commands.bindPipeline(deferredPipeline_);
        commands.bindBindingSet(targets_->lightBindings);commands.bindBindingSet(targets_->deferredBindings);if(shadows_)commands.bindBindingSet(targets_->effectBindings);commands.bindVertexBuffer(quad_);commands.draw(6);commands.endRenderPass();
    }
    if(frame.forwardShading && path_==PbrPath::Scene && !(frame.rsm && frame.rsmSettings.indirectOnly)){
        commands.copyTexture(targets_->hdr,targets_->opaque);
        auto environment=frameResources.bindings({sceneForwardLayout_,{{0,skyParameters_,0,128,{},{}},{1,shadows_->parameters(),0,sizeof(ShadowParameters),{},{}},{2,{},0,0,skyView_,skySampler_},{3,{},0,0,irradianceView_,skySampler_},{4,{},0,0,shadows_->view(),hdrSampler_},{5,{},0,0,targets_->opaqueView,hdrSampler_},{6,{},0,0,targets_->aoView,hdrSampler_},{7,{},0,0,targets_->backDepthView,hdrSampler_}}});
        pass={};pass.color=targets_->hdrView;pass.colorLoad=rhi::LoadOp::Load;pass.depth=targets_->depthView;pass.depthLoad=rhi::LoadOp::Load;commands.beginRenderPass(pass);
        for(size_t i=0;i<packets.size();++i){auto& packet=packets[i];if(packet.material->transparent())continue;auto layout=frameLayout();std::vector<rhi::BindingEntry> entries{{0,camera_,0,64,{},{}},{1,objects_[i].data,0,128,{},{}},{2,lighting_,0,1472,{},{}}};
            if(packet.mesh->instances()){layout.entries.push_back({3,rhi::BindingType::StorageRead,rhi::ShaderStage::Vertex,"OutPose",64});entries.push_back({3,packet.mesh->instances(),0,size_t(packet.mesh->instanceCapacity())*64,{},{}});commands.bindPipeline(packet.wireframe?sceneForwardWireInstanced_:sceneForwardInstanced_);}else commands.bindPipeline(packet.wireframe?sceneForwardWire_:sceneForward_);
            commands.bindBindingSet(frameResources.bindings({layout,entries}));packet.material->bind(commands);commands.bindBindingSet(environment);packet.mesh->draw(commands);
        }commands.endRenderPass();pass={};
    }
    if(shadows_){
        pass={};pass.color=targets_->motionView;pass.clearColor={0,0,0,0};pass.depth=targets_->depthView;pass.depthLoad=rhi::LoadOp::Load;commands.beginRenderPass(pass);
        if(frame.taa){std::array<glm::mat4,3> camera{frame.viewProjection,temporal_->valid()?temporal_->previousVP():frame.viewProjection,temporal_->valid()?temporal_->previousView():frame.view};auto cameraBuffer=frameResources.buffer({sizeof(camera),rhi::BufferUsage::Uniform,"Motion camera"},camera.data());
            for(size_t i=0;i<packets.size();++i){const auto& packet=packets[i];if(packet.material->transparent())continue;const uint64_t id=packet.id?packet.id:uint64_t(reinterpret_cast<uintptr_t>(packet.mesh.get()))^(uint64_t(i+1)<<32);auto old=previousModels_.find(id);struct alignas(16) Object {glm::mat4 model,previous;glm::ivec4 flags;};Object object{packet.model,old==previousModels_.end()?packet.model:old->second,{(packet.mesh->instances() || packet.mesh->indirectBuffer())?1:0,0,0,0}};auto data=frameResources.buffer({sizeof(object),rhi::BufferUsage::Uniform,"Motion object"},&object);
                auto layout=motionLayout_;std::vector<rhi::BindingEntry> entries{{0,cameraBuffer,0,192,{},{}},{1,data,0,144,{},{}}};if(packet.mesh->instances()){layout.entries.push_back({3,rhi::BindingType::StorageRead,rhi::ShaderStage::Vertex,"OutPose",64});entries.push_back({3,packet.mesh->instances(),0,size_t(packet.mesh->instanceCapacity())*64,{},{}});commands.bindPipeline(motionInstanced_);}else commands.bindPipeline(motionPipeline_);commands.bindBindingSet(frameResources.bindings({layout,entries}));packet.material->bindShadow(commands);packet.mesh->draw(commands);
            }
        }
        commands.endRenderPass();pass={};
    }
    if(!frame.oceans.empty()){
        commands.copyTexture(targets_->hdr,targets_->opaque);pass={};pass.color=targets_->hdrView;pass.colorLoad=rhi::LoadOp::Load;pass.depth=targets_->depthView;pass.depthLoad=rhi::LoadOp::Load;pass.additionalColors={{targets_->motionView,rhi::LoadOp::Load}};
        commands.beginRenderPass(pass);for(const auto& ocean:frame.oceans)oceans_.at(ocean.id)->record(frameResources,commands,frame,ocean,skyView_,targets_->opaqueView,targets_->gbufferViews[0],targets_->gbufferViews[1]);commands.endRenderPass();pass={};
    }
    if(path_==PbrPath::Scene){
        std::vector<size_t> sorted;for(size_t i=0;i<packets.size();++i)if(packets[i].material->transparent())sorted.push_back(i);
        std::stable_sort(sorted.begin(),sorted.end(),[&](size_t a,size_t b){auto center=[&](size_t i){return frame.view*packets[i].model*glm::vec4((packets[i].mesh->boundsMin()+packets[i].mesh->boundsMax())*.5f,1);};return center(a).z<center(b).z;});
        if(!sorted.empty()){
            auto environment=frameResources.bindings({transparentLayout_,{{0,skyParameters_,0,128,{},{}},{1,shadows_->parameters(),0,sizeof(ShadowParameters),{},{}},{2,{},0,0,skyView_,skySampler_},{3,{},0,0,irradianceView_,skySampler_},{4,{},0,0,shadows_->view(),hdrSampler_}}});
            pass={};pass.color=targets_->hdrView;pass.colorLoad=rhi::LoadOp::Load;pass.depth=targets_->depthView;pass.depthLoad=rhi::LoadOp::Load;pass.additionalColors={{targets_->motionView,rhi::LoadOp::Load}};commands.beginRenderPass(pass);
            for(auto i:sorted){auto layout=frameLayout();std::vector<rhi::BindingEntry> entries{{0,camera_,0,64,{},{}},{1,objects_[i].data,0,128,{},{}},{2,lighting_,0,1472,{},{}}};if(packets[i].mesh->instances()){layout.entries.push_back({3,rhi::BindingType::StorageRead,rhi::ShaderStage::Vertex,"OutPose",64});entries.push_back({3,packets[i].mesh->instances(),0,size_t(packets[i].mesh->instanceCapacity())*64,{},{}});commands.bindPipeline(transparentInstanced_);}else commands.bindPipeline(transparentPipeline_);commands.bindBindingSet(frameResources.bindings({layout,entries}));packets[i].material->bind(commands);commands.bindBindingSet(environment);packets[i].mesh->draw(commands);}commands.endRenderPass();pass={};
        }
    }
    rhi::TextureViewHandle resolved;
    if(frame.taa)resolved=temporal_->record(frameResources,commands,frame,targets_->hdrView,targets_->depthView,targets_->motionView);
    pass.color = targets_->outputView;commands.beginRenderPass(pass);
    commands.bindPipeline(tonePipeline_);commands.bindBindingSet(resolved?frameResources.bindings({{0,{{0,rhi::BindingType::UniformBuffer,rhi::ShaderStage::Fragment,"ToneMap",16},{1,rhi::BindingType::SampledTexture,rhi::ShaderStage::Fragment,"hdrBuffer",0}}},{{0,tone_,0,16,{},{}},{1,{},0,0,resolved,hdrSampler_}}}):targets_->toneBindings);commands.bindVertexBuffer(quad_);commands.draw(6);commands.endRenderPass();device.submit(commands);
    temporalOutput_=frame.taa;if(frame.taa)temporal_->commit(frame);
    previousTaa_=source.taa;previousCamera_=source.cameraPosition;previousProjection_=source.viewProjection*glm::inverse(source.view);historyKey_=frame.historyKey;
    std::map<uint64_t,glm::mat4> models;for(size_t i=0;i<packets.size();++i)models[packets[i].id?packets[i].id:uint64_t(reinterpret_cast<uintptr_t>(packets[i].mesh.get()))^(uint64_t(i+1)<<32)]=packets[i].model;previousModels_.swap(models);
}
std::vector<float> ForwardPbrRenderer::readBackDepth(){if(!shadows_)throw std::invalid_argument("SSS unavailable on this path");return resources_.device->readTextureFloat(targets_->backDepth);}
std::vector<float> ForwardPbrRenderer::readSSAO(){if(!shadows_)throw std::invalid_argument("SSAO unavailable on this path");return resources_.device->readTextureFloat(targets_->ao);}
std::vector<float> ForwardPbrRenderer::readShadowDepth(){if(!shadows_)throw std::invalid_argument("Shadows unavailable on this path");return shadows_->readDepth();}
std::vector<float> ForwardPbrRenderer::readGBuffer(uint32_t attachment) {
    if (path_ == PbrPath::Forward || attachment >= (path_==PbrPath::Scene?6u:4u)) throw std::invalid_argument("Renderer: invalid G-buffer attachment");
    return resources_.device->readTextureFloat(targets_->gbuffer[attachment]);
}
rhi::TextureHandle ForwardPbrRenderer::output() const { return targets_->output; }
std::vector<float> ForwardPbrRenderer::readHDR() { return temporalOutput_?temporal_->read():resources_.device->readTextureFloat(targets_->hdr); }
std::vector<uint8_t> ForwardPbrRenderer::readOutput() { return resources_.device->readTexture(targets_->output); }
}
