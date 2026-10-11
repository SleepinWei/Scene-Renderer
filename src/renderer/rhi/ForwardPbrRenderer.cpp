#include "renderer/rhi/ForwardPbrRenderer.h"
#include "engine/RenderGraph.h"
#include "renderer/rhi/GraphTextures.h"
#include "renderer/rhi/GpuGeometryCuller.h"
#include "renderer/rhi/GpuHiZCuller.h"
#include "renderer/rhi/GpuClusterCuller.h"
#include "renderer/rhi/RasterVisibility.h"
#include "rhi/ShaderAssets.h"
#include "renderer/rhi/ShadowRenderer.h"
#include "renderer/rhi/GpuClouds.h"
#include "renderer/rhi/GpuRain.h"
#include "renderer/rhi/GpuAirFog.h"
#include "renderer/rhi/GpuDdgi.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <json/json.hpp>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <numeric>
#include <stdexcept>
namespace render {
namespace {
struct alignas(16) LightingBlock { glm::vec4 cameraAmbient;glm::ivec4 counts;std::array<LightData, 30> lights{}; };
struct alignas(16) SkyBlock {glm::mat4 inverseVP;glm::vec4 camera,settings,sunDirectionRadius,sunRadiance;};
struct alignas(16) EffectsBlock {glm::mat4 view,projection;glm::vec4 settings,options;glm::mat4 inverseProjection;glm::vec4 depthOptions;};
static_assert(sizeof(SkyBlock)==128,"Sky display ABI");
static_assert(sizeof(EffectsBlock)==240,"Effects ABI");
struct alignas(16) ObjectBlock { glm::mat4 model, normalMatrix; };
static_assert(sizeof(LightData) == 48 && offsetof(LightData, colorInner) == 16 && offsetof(LightData, directionOuter) == 32, "Light std140 ABI changed");
static_assert(sizeof(LightingBlock) == 1472 && offsetof(LightingBlock, counts) == 16 && offsetof(LightingBlock, lights) == 32, "Lighting std140 ABI changed");
static_assert(sizeof(ObjectBlock) == 128 && sizeof(glm::mat4) == 64, "Matrix std140 ABI changed");
rhi::ShaderAsset shader(const std::string& root, const std::string& name) { auto path = root + "/" + name;return {path + ".glsl", path + ".metallib", path + ".spv", path + ".json", "main0"}; }
rhi::BindingLayout withMeshFrames(rhi::BindingLayout layout,bool supported) {
    if(supported)layout.entries.push_back({6,rhi::BindingType::StorageRead,rhi::ShaderStage::Vertex,"MeshTangents",32});
    return layout;
}
void meshFrameEntry(std::vector<rhi::BindingEntry>& entries,const GpuMesh& mesh) {
    if(mesh.materialTangents())entries.push_back({6,mesh.materialTangents(),0,mesh.materialTangentBytes(),{}, {}});
}
rhi::BindingLayout withWaterWet(rhi::BindingLayout layout) {
    layout.entries.push_back({4,rhi::BindingType::UniformBuffer,rhi::ShaderStage::Fragment,"WaterWetParameters",32});
    layout.entries.push_back({5,rhi::BindingType::SampledTexture,rhi::ShaderStage::Fragment,"waterWetHistory",0});return layout;
}
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
    std::unique_ptr<GraphTextures> transients;
    ~Targets(){resources.clear();transients.reset();}
    uint32_t width, height;
    rhi::TextureHandle hdr, depth, output,clusterEarlyAlbedo,clusterEarlyDepth;
    rhi::TextureViewHandle hdrView, depthView, outputView;
    rhi::BindingSetHandle deferredBindings, lightBindings,ssaoBindings,aoFilterBindings,effectBindings;
    rhi::TextureHandle ao,rawAo,opaque,motion,backDepth,backTest; rhi::TextureViewHandle aoView,rawAoView,opaqueView,motionView,backDepthView,backTestView;
    rhi::TextureHandle aoDepth;
    rhi::TextureViewHandle aoDepthChain;
    rhi::BindingSetHandle aoDepthBindings;
    std::vector<rhi::TextureViewHandle> aoDepthViews;
    std::vector<rhi::BindingSetHandle> aoReduceBindings;
    std::array<rhi::TextureHandle,6> gbuffer{};
    std::array<rhi::TextureViewHandle,6> gbufferViews{};
    Targets(std::shared_ptr<rhi::GraphicsDevice> d, uint32_t w, uint32_t h, rhi::SamplerHandle sampler, rhi::BufferHandle lighting, PbrPath path,ShadowRenderer* shadow,rhi::BufferHandle effects,rhi::BufferHandle skyParams,rhi::TextureViewHandle sky,rhi::TextureViewHandle irradiance,rhi::SamplerHandle skySampler) : resources(std::move(d)), width(w), height(h) {
        using namespace rhi;
        hdr = resources.texture({w, h, Format::RGBA16Float, TextureUsage::ColorAttachment | TextureUsage::Sampled | TextureUsage::CopySource, "Forward HDR"});hdrView = resources.view(hdr);
        if(path==PbrPath::Scene) {
            engine::RenderGraph lifetime;
            for(auto name:{"sss-depth","depth"})lifetime.describe(name,{1,1,true,"depth32"});
            lifetime.add("back-faces",{{"sss-depth",engine::RenderGraph::Access::Write}},[]{});
            lifetime.add("geometry",{{"depth",engine::RenderGraph::Access::Write}},[]{});
            lifetime.add("later-effects",{{"depth",engine::RenderGraph::Access::Read}},[]{});
            const TextureDesc descriptor{w,h,Format::Depth32Float,TextureUsage::DepthAttachment|TextureUsage::Sampled|TextureUsage::CopySource,"Shared SSS/scene depth"};
            transients=std::make_unique<GraphTextures>(resources.device,lifetime.compile(),std::map<std::string,TextureDesc>{{"sss-depth",descriptor},{"depth",descriptor}});
            depth=transients->texture("depth");depthView=transients->view("depth");
        } else {depth = resources.texture({w, h, Format::Depth32Float, TextureUsage::DepthAttachment|TextureUsage::Sampled|TextureUsage::CopySource, "Forward depth"});depthView = resources.view(depth);}
        output = resources.texture({w, h, Format::RGBA8UNorm, TextureUsage::ColorAttachment | TextureUsage::CopySource | TextureUsage::Sampled, "Tone mapped output"});outputView = resources.view(output);
        if (path != PbrPath::Forward) {
            BindingLayout images{1,{}};std::vector<BindingEntry> entries;
            const char* names[] = {"positionBuffer","normalBuffer","albedoBuffer","emissiveBuffer"};
            for (uint32_t i=0;i<4;++i) {
                // World positions need float32: half precision rounds kilometre
                // coordinates by metres and corrupts shadow depth/receiver planes.
                gbuffer[i] = resources.texture({w,h,i==0?Format::RGBA32Float:Format::RGBA16Float,TextureUsage::ColorAttachment | TextureUsage::Sampled | TextureUsage::CopySource,names[i]});gbufferViews[i] = resources.view(gbuffer[i]);
                images.entries.push_back({i+1,BindingType::SampledTexture,ShaderStage::Fragment,names[i],0});entries.push_back({i+1,{},0,0,gbufferViews[i],sampler});
            }
                BindingLayout effects{2,{}};std::vector<BindingEntry> effectEntries;
                for(uint32_t i=4;i<6;++i){gbuffer[i]=resources.texture({w,h,Format::RGBA16Float,TextureUsage::ColorAttachment|TextureUsage::Sampled|TextureUsage::CopySource,"Material effect G-buffer"});gbufferViews[i]=resources.view(gbuffer[i]);effects.entries.push_back({i-4,BindingType::SampledTexture,ShaderStage::Fragment,i==4?"materialEffectsBuffer":"tangentBuffer",0});effectEntries.push_back({i-4,{},0,0,gbufferViews[i],sampler});}
            if(path==PbrPath::Scene){
                backDepth=resources.texture({w,h,Format::RGBA32Float,TextureUsage::ColorAttachment|TextureUsage::Sampled|TextureUsage::CopySource,"SSS back-face depth"});backDepthView=resources.view(backDepth);backTest=transients->texture("sss-depth");backTestView=transients->view("sss-depth");

                effects.entries.push_back({2,BindingType::SampledTexture,ShaderStage::Fragment,"backDepthBuffer",0});effectEntries.push_back({2,{},0,0,backDepthView,sampler});
                opaque=resources.texture({w,h,Format::RGBA16Float,TextureUsage::CopyDestination|TextureUsage::Sampled|TextureUsage::CopySource,"Opaque HDR snapshot"});opaqueView=resources.view(opaque);
                motion=resources.texture({w,h,Format::RGBA16Float,TextureUsage::ColorAttachment|TextureUsage::Sampled|TextureUsage::CopySource,"Temporal motion"});motionView=resources.view(motion);
                ao=resources.texture({w,h,Format::R16Float,TextureUsage::ColorAttachment|TextureUsage::Sampled|TextureUsage::CopySource,"Filtered ambient occlusion"});aoView=resources.view(ao);
                rawAo=resources.texture({w,h,Format::R16Float,TextureUsage::ColorAttachment|TextureUsage::Sampled|TextureUsage::CopySource,"Raw horizon/SSAO visibility"});rawAoView=resources.view(rawAo);
                uint32_t levels=1;
                if(resources.device->supportsTextureSubresources())for(uint32_t size=std::max(w,h);size>1;size>>=1)++levels;
                aoDepth=resources.texture({w,h,Format::R32Float,TextureUsage::ColorAttachment|TextureUsage::Sampled|TextureUsage::CopySource,"AO linear nearest-depth pyramid",levels});
                aoDepthChain=resources.view(aoDepth,{0,levels,0,1});
                for(uint32_t mip=0;mip<levels;++mip) {
                    aoDepthViews.push_back(resources.view(aoDepth,{mip,1,0,1}));
                    if(mip)aoReduceBindings.push_back(resources.bindings({{1,{{0,BindingType::SampledTexture,ShaderStage::Fragment,"aoDepthSource",0}}},{{0,{},0,0,aoDepthViews[mip-1],sampler}}}));
                }
                aoDepthBindings=resources.bindings({{1,{images.entries[0]}},{entries[0]}});
                BindingLayout ssaoImages{1,{images.entries[0],images.entries[1],{4,BindingType::SampledTexture,ShaderStage::Fragment,"aoLinearDepth",0}}};
                const BindingEntry depthEntry{4,{},0,0,aoDepthChain,sampler};
                ssaoBindings=resources.bindings({ssaoImages,{entries[0],entries[1],depthEntry}});
                auto filterImages=ssaoImages;filterImages.entries.push_back({3,BindingType::SampledTexture,ShaderStage::Fragment,"aoRawBuffer",0});
                aoFilterBindings=resources.bindings({filterImages,{entries[0],entries[1],depthEntry,{3,{},0,0,rawAoView,sampler}}});
                images.entries.push_back({5,BindingType::SampledTexture,ShaderStage::Fragment,"shadowAtlas",0});entries.push_back({5,{},0,0,shadow->view(),sampler});
                images.entries.push_back({6,BindingType::SampledTexture,ShaderStage::Fragment,"aoBuffer",0});entries.push_back({6,{},0,0,aoView,sampler});
                images.entries.push_back({7,BindingType::SampledTexture,ShaderStage::Fragment,"rsmFlux",0});entries.push_back({7,{},0,0,shadow->rsmSourceView(0),sampler});
            }
            effectBindings=resources.bindings({effects,effectEntries});
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
    const bool meshFrames=resources_.device->computeLimits().supported;
    GraphicsPipelineDesc p;p.vertex = shader(directory,meshFrames?"forward-material.vert":"forward.vert");p.fragment = shader(directory,"forward.frag");p.vertexStride = sizeof(MeshVertex);
    if (path_ != PbrPath::Forward) { p.fragment = shader(directory,"gbuffer.frag");geometryLayout_.entries.pop_back();p.additionalColorFormats = {Format::RGBA16Float,Format::RGBA16Float,Format::RGBA16Float}; }
    if(path_!=PbrPath::Forward)p.additionalColorFormats.insert(p.additionalColorFormats.end(),2,Format::RGBA16Float);
    if(path_==PbrPath::Scene)p.fragment=shader(directory,"scene-gbuffer.frag");
    geometryLayout_=withMeshFrames(withWaterWet(geometryLayout_),meshFrames);
    p.attributes = GpuMesh::attributes();p.bindings = {geometryLayout_, GpuMaterial::layout()};p.colorFormat = path_==PbrPath::Forward?Format::RGBA16Float:Format::RGBA32Float;p.depthAttachment = p.depthTest = p.depthWrite = true;p.label = "Forward Cook-Torrance PBR";
    checkBlock(p.vertex,"CameraVertex",{{"viewProjection",0}},64);checkBlock(p.vertex,"ObjectData",{{"model",0},{"normalMatrix",64}},128);
    checkBlock(p.fragment,"MaterialData",{{"albedoAlpha",0},{"factors",16},{"emissiveNormal",32}},48);
    if (path_ == PbrPath::Forward) checkBlock(p.fragment,"SceneLighting",{{"cameraAmbient",0},{"counts",16},{"lights",32}},1472);
    forward_ = resources_.pipeline(p);
    if(resources_.device->supportsWireframe()){auto wire=p;wire.wireframe=true;wireframe_=resources_.pipeline(wire);}
    if(resources_.device->computeLimits().supported){auto instance=p;instance.vertex=shader(directory,"instanced-material.vert");instance.bindings[0].entries.push_back({3,BindingType::StorageRead,ShaderStage::Vertex,"OutPose",64});instanced_=resources_.pipeline(instance);if(resources_.device->supportsWireframe()){instance.wireframe=true;wireframeInstanced_=resources_.pipeline(instance);}}
    post_=std::make_unique<GpuPostProcessor>(resources_.device,directory);
    if(path_==PbrPath::Scene){auto back=rhi::GraphicsPipelineDesc{};back.vertex=shader(directory,"forward.vert");back.fragment=shader(directory,"material-backdepth.frag");back.vertexStride=32;back.attributes=GpuMesh::attributes();back.bindings={ShadowRenderer::objectLayout(),GpuMaterial::shadowLayout()};back.colorFormat=Format::RGBA32Float;back.depthAttachment=back.depthTest=back.depthWrite=true;back.depthCompare=DepthCompare::Greater;back.cull=CullMode::Front;backDepthPipeline_=resources_.pipeline(back);}
    camera_ = resources_.buffer({64,BufferUsage::Uniform | BufferUsage::CopyDestination,"PBR camera"});
    lighting_ = resources_.buffer({1472,BufferUsage::Uniform | BufferUsage::CopyDestination,"PBR lights"});
    const float quad[] = {-1,-1,0,1, 1,-1,1,1, 1,1,1,0, -1,-1,0,1, 1,1,1,0, -1,1,0,0};
    quad_ = resources_.buffer({sizeof(quad),BufferUsage::Vertex,"Tone map fullscreen"},quad);
    hdrSampler_ = resources_.sampler({Filter::Nearest,AddressMode::ClampToEdge});skySampler_=resources_.sampler({Filter::Linear,AddressMode::Repeat});
    const std::array<glm::vec4,2> wetZero={glm::vec4(0,0,1,1),glm::vec4(0)};
    inactiveWetParameters_=resources_.buffer({32,BufferUsage::Uniform,"Inactive water wetness"},wetZero.data());
    auto wetTexture=resources_.texture({1,1,Format::RGBA32Float,TextureUsage::Sampled|TextureUsage::CopyDestination,"Inactive water wetness history"});
    const glm::vec4 zero(0);resources_.device->writeTextureFloat(wetTexture,&zero.x,16);inactiveWetView_=resources_.view(wetTexture);wetSampler_=resources_.sampler({Filter::Linear,AddressMode::ClampToEdge});
    if(path_==PbrPath::Scene){
        if(resources_.device->computeLimits().maxStorageImages){atmosphere_=std::make_unique<GpuAtmosphere>(resources_.device,directory);atmosphere_->update({},10);skyView_=atmosphere_->sky();irradianceView_=atmosphere_->irradiance();}
        else {auto black=resources_.texture({1,1,Format::RGBA8UNorm,TextureUsage::Sampled|TextureUsage::CopyDestination,"Unavailable atmosphere fallback"});const uint8_t data[]={0,0,0,255};resources_.device->writeTexture(black,data,4);skyView_=irradianceView_=resources_.view(black);}
        skyParameters_=resources_.buffer({128,BufferUsage::Uniform|BufferUsage::CopyDestination,"Sky display parameters"});
        shadows_=std::make_unique<ShadowRenderer>(resources_.device,directory,128,irradianceView_);
        effects_=resources_.buffer({240,BufferUsage::Uniform|BufferUsage::CopyDestination,"Ambient occlusion parameters"});
        p={};p.vertex=shader(directory,"tonemap.vert");p.fragment=shader(directory,"ssao.frag");p.vertexStride=16;p.attributes={{0,VertexFormat::Float2,0},{1,VertexFormat::Float2,8}};p.colorFormat=Format::R16Float;
        p.bindings={{0,{{0,BindingType::UniformBuffer,ShaderStage::Fragment,"EffectsParameters",240}}},{1,{{1,BindingType::SampledTexture,ShaderStage::Fragment,"positionBuffer",0},{2,BindingType::SampledTexture,ShaderStage::Fragment,"normalBuffer",0},{4,BindingType::SampledTexture,ShaderStage::Fragment,"aoLinearDepth",0}}}};
        effectsBindings_=resources_.bindings({p.bindings[0],{{0,effects_,0,240,{},{}}}});
        checkBlock(p.fragment,"EffectsParameters",{{"effectView",0},{"effectProjection",64},{"aoSettings",128},{"aoOptions",144},{"aoInverseProjection",160},{"aoDepthOptions",224}},240);ssaoPipeline_=resources_.pipeline(p);
        p.fragment=shader(directory,"ao-filter.frag");p.bindings[1].entries.push_back({3,BindingType::SampledTexture,ShaderStage::Fragment,"aoRawBuffer",0});
        checkBlock(p.fragment,"EffectsParameters",{{"effectView",0},{"effectProjection",64},{"aoSettings",128},{"aoOptions",144},{"aoInverseProjection",160},{"aoDepthOptions",224}},240);aoFilterPipeline_=resources_.pipeline(p);
        p.fragment=shader(directory,"ao-depth.frag");p.colorFormat=Format::R32Float;
        p.bindings[1].entries={{1,BindingType::SampledTexture,ShaderStage::Fragment,"positionBuffer",0}};
        checkBlock(p.fragment,"EffectsParameters",{{"effectView",0},{"aoInverseProjection",160},{"aoDepthOptions",224}},240);
        aoDepthPipeline_=resources_.pipeline(p);
        p.fragment=shader(directory,"ao-depth-reduce.frag");p.bindings={{1,{{0,BindingType::SampledTexture,ShaderStage::Fragment,"aoDepthSource",0}}}};
        aoReducePipeline_=resources_.pipeline(p);
    }
    if(path_==PbrPath::Scene && resources_.device->computeLimits().maxStorageImages){
        temporal_=std::make_unique<GpuTemporal>(resources_.device,directory);p={};p.vertex=shader(directory,"motion-tree.vert");p.fragment=shader(directory,"motion.frag");p.vertexStride=32;p.attributes=GpuMesh::attributes();motionLayout_={0,{{0,BindingType::UniformBuffer,ShaderStage::Vertex,"MotionCamera",192},{1,BindingType::UniformBuffer,ShaderStage::Vertex,"MotionObject",144}}};motionLayout_.entries.push_back({5,BindingType::StorageRead,ShaderStage::Vertex,"PreviousVertices",32});p.bindings={motionLayout_,GpuMaterial::shadowLayout()};p.colorFormat=Format::RGBA16Float;p.depthAttachment=p.depthTest=true;p.depthCompare=DepthCompare::LessEqual;motionPipeline_=resources_.pipeline(p);
        p.vertex=shader(directory,"motion-tree-instanced.vert");p.bindings[0].entries.push_back({3,BindingType::StorageRead,ShaderStage::Vertex,"OutPose",64});p.bindings[0].entries.push_back({4,BindingType::StorageRead,ShaderStage::Vertex,"PreviousPose",64});motionInstanced_=resources_.pipeline(p);
    }
    if(path_==PbrPath::Scene){
        transparentLayout_={2,{{0,BindingType::UniformBuffer,ShaderStage::Fragment,"SkyData",128},{1,BindingType::UniformBuffer,ShaderStage::Fragment,"ShadowData",sizeof(ShadowParameters)},{2,BindingType::SampledTexture,ShaderStage::Fragment,"skyRadianceLut",0},{3,BindingType::SampledTexture,ShaderStage::Fragment,"skyIrradianceLut",0},{4,BindingType::SampledTexture,ShaderStage::Fragment,"shadowAtlas",0}}};
        p={};p.vertex=shader(directory,meshFrames?"forward-material.vert":"forward.vert");p.fragment=shader(directory,"transparent.frag");p.vertexStride=32;p.attributes=GpuMesh::attributes();p.bindings={withMeshFrames(frameLayout(),meshFrames),GpuMaterial::layout(),transparentLayout_};p.colorFormat=Format::RGBA16Float;p.additionalColorFormats={Format::RGBA16Float};p.attachmentBlend={true,false};p.depthAttachment=p.depthTest=true;p.depthWrite=false;transparentPipeline_=resources_.pipeline(p);
        if(resources_.device->computeLimits().supported){p.vertex=shader(directory,"instanced-material.vert");p.bindings[0].entries.push_back({3,BindingType::StorageRead,ShaderStage::Vertex,"OutPose",64});transparentInstanced_=resources_.pipeline(p);}
    }
    if(path_==PbrPath::Scene){
        sceneForwardLayout_=transparentLayout_;for(uint32_t i=0;i<3;++i)sceneForwardLayout_.entries.push_back({5+i,BindingType::SampledTexture,ShaderStage::Fragment,i==0?"indirectBuffer":i==1?"aoBuffer":"backDepthBuffer",0});
        p={};p.vertex=shader(directory,meshFrames?"forward-material.vert":"forward.vert");p.fragment=shader(directory,"scene-forward.frag");p.vertexStride=32;p.attributes=GpuMesh::attributes();p.bindings={withMeshFrames(withWaterWet(frameLayout()),meshFrames),GpuMaterial::layout(),sceneForwardLayout_};p.colorFormat=Format::RGBA16Float;p.depthAttachment=p.depthTest=true;p.depthWrite=false;p.depthCompare=DepthCompare::LessEqual;sceneForward_=resources_.pipeline(p);if(resources_.device->supportsWireframe()){auto wire=p;wire.wireframe=true;sceneForwardWire_=resources_.pipeline(wire);}
        if(resources_.device->computeLimits().supported){p.vertex=shader(directory,"instanced-material.vert");p.bindings[0].entries.push_back({3,BindingType::StorageRead,ShaderStage::Vertex,"OutPose",64});sceneForwardInstanced_=resources_.pipeline(p);if(resources_.device->supportsWireframe()){p.wireframe=true;sceneForwardWireInstanced_=resources_.pipeline(p);}}
    }
    resize(width,height);
    if (path_ != PbrPath::Forward) {
        p = {};p.vertex = shader(directory,"tonemap.vert");p.fragment = shader(directory,"deferred.frag");p.vertexStride = 16;
        p.attributes = {{0,VertexFormat::Float2,0},{1,VertexFormat::Float2,8}};p.colorFormat = Format::RGBA16Float;p.label = "Deferred PBR lighting";
        p.bindings = {{0,{{2,BindingType::UniformBuffer,ShaderStage::Fragment,"SceneLighting",1472}}},{1,{}}};
        const char* names[] = {"positionBuffer","normalBuffer","albedoBuffer","emissiveBuffer"};
        for (uint32_t i=0;i<4;++i) p.bindings[1].entries.push_back({i+1,BindingType::SampledTexture,ShaderStage::Fragment,names[i],0});
        p.bindings.push_back({2,{{0,BindingType::SampledTexture,ShaderStage::Fragment,"materialEffectsBuffer",0},{1,BindingType::SampledTexture,ShaderStage::Fragment,"tangentBuffer",0}}});
        if(path_==PbrPath::Scene){p.fragment=shader(directory,"scene-light.frag");p.bindings[2].entries.push_back({2,BindingType::SampledTexture,ShaderStage::Fragment,"backDepthBuffer",0});
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
    aoBenchmarkReady_=false;aoDepthLevelsReady_=0;clusterHistoryValid_=false;
    auto targets = std::make_unique<Targets>(resources_.device,width,height,hdrSampler_,lighting_,path_,shadows_.get(),effects_,skyParameters_,skyView_,irradianceView_,skySampler_);if(temporal_)temporal_->resize(width,height);targets_.swap(targets);previousModels_.clear();temporalOutput_=false;
}
void ForwardPbrRenderer::resetTemporal() {
    clusterHistoryValid_=false;
    if(clouds_)clouds_->reset();
    if(post_)post_->reset();
    if(temporal_)temporal_->reset();previousModels_.clear();previousTaa_=false;temporalOutput_=false;
}
void ForwardPbrRenderer::render(const FrameData& source, const std::vector<DrawPacket>& inputPackets, float exposure, float gamma, const std::vector<DrawPacket>* auxiliaryPackets) {
    const auto& auxiliary=auxiliaryPackets?*auxiliaryPackets:inputPackets;
    auto packets=inputPackets;
    std::vector<size_t> originalIndices(packets.size());std::iota(originalIndices.begin(),originalIndices.end(),0);
    aoBenchmarkReady_=false;aoDepthLevelsReady_=0;
    validatePostProcessSettings(source.postProcess);
    FrameData frame=source;frame.viewportWidth=targets_->width;frame.viewportHeight=targets_->height;
    auto disabled=[](const char* name){const auto value=std::getenv(name);return value && std::strcmp(value,"1")==0;};
    frame.objectCulling=frame.objectCulling&&!disabled("SCENERENDERER_DISABLE_OBJECT_CULLING");
    frame.triangleCulling=(frame.triangleCulling||disabled("SCENERENDERER_ENABLE_TRIANGLE_CULLING"))&&!disabled("SCENERENDERER_DISABLE_TRIANGLE_CULLING");
    frame.hizCulling=(frame.hizCulling||disabled("SCENERENDERER_ENABLE_HIZ_CULLING"))&&!disabled("SCENERENDERER_DISABLE_HIZ_CULLING");
    frame.clusters.enabled=(frame.clusters.enabled||disabled("SCENERENDERER_ENABLE_CLUSTER_GEOMETRY"))&&!disabled("SCENERENDERER_DISABLE_CLUSTER_GEOMETRY");
    frame.hizCulling=(frame.hizCulling||(frame.clusters.enabled&&frame.clusters.batched&&frame.clusters.occlusion&&!frame.clusters.wireframe))&&!disabled("SCENERENDERER_DISABLE_HIZ_CULLING");
    if(!std::isfinite(frame.clusters.pixelError)||frame.clusters.pixelError<0)throw std::invalid_argument("Invalid cluster pixel error");
    if(frame.clusters.enabled&&frame.clusters.wireframe&&resources_.device->supportsWireframe())for(auto& p:packets)if(p.clusters)p.wireframe=true;
    if(!frame.triangleCullMinimum)throw std::invalid_argument("Triangle cull threshold must be positive");
    if(!std::isfinite(frame.hizOccluderMinArea) || frame.hizOccluderMinArea<0 || frame.hizOccluderMinArea>1 ||
       !frame.hizMaxOccluders || !frame.hizOccluderTriangleBudget)throw std::invalid_argument("HiZ occluder settings outside domain");
    frame.weather.validate();
    if(!std::isfinite(frame.rainMetersPerSecond)||frame.rainMetersPerSecond<0||frame.rainMetersPerSecond>float(1000./3600000)||
       !std::isfinite(frame.rainWind.x)||!std::isfinite(frame.rainWind.y))throw std::invalid_argument("Rain input outside domain");
    SunState solar;
    if (!std::isfinite(exposure) || exposure < 0 || !std::isfinite(gamma) || gamma <= 0 || !std::isfinite(frame.ambient) || frame.ambient < 0 || frame.lights.size() > 30)
        throw std::invalid_argument("Renderer: invalid frame parameters");
    for(float value:{frame.aoRadius,frame.aoBias,frame.aoPower})if(!std::isfinite(value))throw std::invalid_argument("AO: nonfinite parameter");
    if(frame.aoRadius<0||frame.aoRadius>20||frame.aoBias<0||frame.aoBias>5||frame.aoPower<=0||frame.aoPower>4||frame.aoSlices<2||frame.aoSlices>8||frame.aoSteps<2||frame.aoSteps>8)throw std::invalid_argument("AO: parameter outside domain");
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

        if(sun!=frame.lights.end())sun->colorInner=glm::vec4(solar.irradiance*solarTransmittance(frame.atmosphere,solar),sun->colorInner.w);
        // Invalidate temporal color when lighting changes, while preserving normal camera reprojection.
        auto hash=[&](float x){uint32_t bits;std::memcpy(&bits,&x,4);frame.historyKey^=uint64_t(bits)+0x9e3779b97f4a7c15ull+(frame.historyKey<<6)+(frame.historyKey>>2);};
        for(float x:{solar.direction.x,solar.direction.y,solar.direction.z,solar.irradiance.x,solar.irradiance.y,solar.irradiance.z,solar.multipleScattering,solar.groundAlbedo})hash(x);
        const auto* parameters=reinterpret_cast<const float*>(&frame.atmosphere);for(size_t i=0;i<24;++i)hash(parameters[i]);
    }
    if(!frame.directionalEnabled)for(auto& light:frame.lights)if(light.positionType.w==0)light.colorInner=glm::vec4(0);
    if(frame.clouds.enabled){
        frame.clouds.validate();
        if(path_!=PbrPath::Scene || !frame.sky || !resources_.device->computeLimits().maxStorageImages)
            throw std::invalid_argument("Clouds require scene atmosphere and storage compute");
        if(!clouds_)clouds_=std::make_unique<GpuClouds>(resources_.device,directory_);
        clouds_->beginFrame();
    }else if(clouds_)clouds_.reset();
    if(frame.taa && !temporal_)throw std::invalid_argument("Temporal compute unavailable on this path/backend");
    auto hashShadow=[&](float value){uint32_t bits;std::memcpy(&bits,&value,4);frame.historyKey^=uint64_t(bits)+0x9e3779b97f4a7c15ull+(frame.historyKey<<6)+(frame.historyKey>>2);};
    for(const auto& packet:packets)if(packet.material)for(int c=0;c<4;++c)hashShadow(packet.material->extension().specularShape[c]);
    hashShadow(frame.ddgi.enabled?1.f:0.f);
    if(!std::isfinite(frame.skySpecularScale)||frame.skySpecularScale<0||frame.skySpecularScale>1)throw std::invalid_argument("Sky specular scale must be 0..1");
    hashShadow(frame.skySpecularScale);
    hashShadow(frame.taaHighQuality?1.f:0.f);
    if(frame.ddgi.enabled)for(float value:{float(frame.ddgi.resetSerial),frame.ddgi.intensity,frame.ddgi.visibilityPower,frame.ddgi.visibility?1.f:0.f,frame.ddgi.normalBias,frame.ddgi.viewBias,frame.ddgi.origin.x,frame.ddgi.origin.y,frame.ddgi.origin.z,frame.ddgi.spacing,float(frame.ddgi.counts.x),float(frame.ddgi.counts.y),float(frame.ddgi.counts.z)})hashShadow(value);
    const auto& shadows=frame.shadowSettings;
    for(float value:{frame.shadows?1.f:0.f,shadows.pcss?1.f:0.f,shadows.instanceCulling?1.f:0.f,shadows.distance,shadows.cascadeBlend,shadows.depthBias,
                    shadows.sunAngularRadius,shadows.localLightRadius,shadows.maxFilterTexels})hashShadow(value);
    for(float value:{frame.ssao?1.f:0.f,frame.aoRadius,frame.aoBias,frame.aoPower,frame.aoHorizon?1.f:0.f,frame.aoDenoise?1.f:0.f,frame.aoLinearDepth?1.f:0.f,frame.aoDepthHierarchy?1.f:0.f,float(frame.aoSlices),float(frame.aoSteps)})hashShadow(value);
    for(const auto& water:frame.oceans)for(float value:{water.surfaceModel.physical?1.f:0.f,water.surfaceModel.effectiveRoughness(),water.sceneReflection?1.f:0.f,water.cameraGrid?1.f:0.f,water.gridFocus,water.shortWaveRipples?1.f:0.f,water.rippleRmsHeight,water.bedCaustics?1.f:0.f,water.causticStrength,water.causticCascades?1.f:0.f,water.causticMeshReceivers?1.f:0.f,water.underwaterWideRefraction?1.f:0.f,water.underwaterSunShafts?1.f:0.f,water.sunShaftStrength,water.underwaterParticles?1.f:0.f,water.particleDensity,float(water.underwaterVolumeSteps),float(water.underwaterVolumeDownsample),water.detailWaves?1.f:0.f,water.detailStrength,water.underwaterCapture?1.f:0.f,water.volumeIntegration?1.f:0.f,float(water.opticalDebug),water.robustRefraction?1.f:0.f,water.multipleScattering?1.f:0.f,water.shore.enabled?1.f:0.f,water.shore.foam?1.f:0.f,water.shore.wetSand?1.f:0.f,float(water.shore.resolution),water.shore.length,water.underwaterView?1.f:0.f,water.underwaterFog?1.f:0.f,water.underwaterView&&frame.cameraPosition.y<water.seaLevel?1.f:0.f})hashShadow(value);
    for(float value:{frame.weather.enabled?1.f:0.f,frame.weather.cloudShadows?1.f:0.f,frame.weather.fog?1.f:0.f,frame.weather.aerialPerspective?1.f:0.f,frame.weather.aerialShadows?1.f:0.f,frame.weather.cloudShadowStrength,frame.weather.aerialDistanceScale,frame.weather.aerialStartDistance,frame.weather.aerialMaxDistance,frame.weather.dryExtinction,frame.weather.rainExtinction,frame.weather.height,frame.weather.fogColour.x,frame.weather.fogColour.y,frame.weather.fogColour.z})hashShadow(value);
    hashShadow(frame.clouds.enabled?1.f:0.f);
    if(frame.clouds.enabled){const auto& c=frame.clouds;for(float value:{c.baseHeight,c.thickness,c.dynamicCoverage?0.f:c.coverage,c.dynamicCoverage?1.f:0.f,c.density,c.shapeScale,c.weatherScale,c.erosion,c.maxDistance,float(c.steps),float(c.lightSteps),float(c.downsample),float(c.seed),c.temporal?1.f:0.f})hashShadow(value);}
    if(frame.clouds.enabled){const auto& c=frame.clouds;for(float value:{c.voxel?1.f:0.f,c.distanceSkipping?1.f:0.f,c.coreIntegration?1.f:0.f,c.volumeCenter.x,c.volumeCenter.y,c.volumeCenter.z,c.volumeSize.x,c.volumeSize.y,c.volumeSize.z,float(c.voxelResolution),c.storm,c.lightning})hashShadow(value);}
    const auto shadowViewProjection=frame.viewProjection;
    if(temporal_){const auto projection=frame.viewProjection*glm::inverse(frame.view);float difference=0;for(int c=0;c<4;++c)for(int r=0;r<4;++r)difference=std::max(difference,std::abs(projection[c][r]-previousProjection_[c][r]));
        // Compare the unjittered projection so FOV/near/far cuts invalidate
        // cloud history without discarding it for the normal TSAA jitter.
        if(clouds_ && difference>1e-4f)clouds_->reset();
        if(!frame.taa || !previousTaa_ || historyKey_!=frame.historyKey || difference>1e-4f || glm::length(frame.cameraPosition-previousCamera_)>2) {temporal_->reset();previousModels_.clear();}
        if(frame.taa){const auto j=GpuTemporal::jitter(temporal_->samples());frame.temporalJitter=j;glm::mat4 shift(1);shift[3].x=j.x*2/targets_->width;shift[3].y=-j.y*2/targets_->height;frame.viewProjection=shift*frame.viewProjection;}
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
    if(auxiliaryPackets)for(const auto& packet:auxiliary){
        if(!packet.mesh || !packet.material || packet.mesh->owner()!=owner() || packet.material->owner()!=owner())throw std::invalid_argument("Renderer: foreign auxiliary draw packet");
        finiteMatrix(packet.model);const auto determinant=glm::determinant(packet.model);
        if(!std::isfinite(determinant)||std::abs(determinant)<1e-12f)throw std::invalid_argument("Renderer: singular auxiliary transform");
        finiteMatrix(glm::inverse(packet.model));
    }
    geometryCullStats_={};geometryCullStats_.sourcePackets=packets.size();
    if(frame.objectCulling){
        size_t keep=0;
        for(size_t i=0;i<packets.size();++i)if(rasterPacketVisible(packets[i],frame)){
            if(keep!=i){packets[keep]=std::move(packets[i]);blocks[keep]=blocks[i];originalIndices[keep]=originalIndices[i];}++keep;
        }
        packets.resize(keep);blocks.resize(keep);originalIndices.resize(keep);
    }
    geometryCullStats_.visiblePackets=packets.size();
    geometryCullStats_.culledPackets=geometryCullStats_.sourcePackets-packets.size();
    if(frame.sky && !atmosphere_)throw std::invalid_argument("Atmosphere compute unavailable on this renderer path/backend");
    if(!frame.oceans.empty() && path_!=PbrPath::Scene)throw std::invalid_argument("Ocean surface requires scene renderer");
    std::map<uint64_t,bool> used;
    for(const auto& ocean:frame.oceans){if(!ocean.id || used.count(ocean.id))throw std::invalid_argument("Duplicate ocean identifier");used[ocean.id]=true;auto& surface=oceans_[ocean.id];if(!surface || !surface->compatible(ocean))surface=std::make_unique<OceanSurface>(resources_.device,directory_,ocean);}
    for(auto it=oceans_.begin();it!=oceans_.end();)if(!used.count(it->first))it=oceans_.erase(it);else ++it;

    auto& device = *resources_.device;
    if(shadows_){const auto transmission=frame.sky?solarTransmittance(frame.atmosphere,solar,false):glm::vec3(0);
        const float diskArea=3.14159265359f*std::pow(std::sin(frame.atmosphere.radii.y),2);
        SkyBlock sky{glm::inverse(frame.viewProjection),glm::vec4(frame.cameraPosition,0),{frame.sky?1.f:0.f,frame.forwardShading?1.f:0.f,frame.sky?atmosphereHorizon(frame.atmosphere,solar.observerHeightKm):0,1},glm::vec4(solar.direction,frame.atmosphere.radii.y),glm::vec4(solar.irradiance*transmission/std::max(diskArea,1e-8f),frame.skySpecularScale)};device.writeBuffer(skyParameters_,0,sizeof(sky),&sky);finiteMatrix(frame.view);const auto projection=frame.viewProjection*glm::inverse(frame.view);
        const auto inverseProjection=glm::inverse(projection);finiteMatrix(inverseProjection);
        const float projectionTolerance=1e-5f*std::max({1.f,std::abs(projection[0][0]),std::abs(projection[1][1])});
        const bool standardProjection=std::abs(projection[0][1])+std::abs(projection[1][0])+std::abs(projection[0][3])+std::abs(projection[1][3])<projectionTolerance &&
            ((std::abs(projection[2][3]+1)+std::abs(projection[3][3])<1e-4f)||(std::abs(projection[2][3])+std::abs(projection[3][3]-1)<1e-4f)) && std::abs(projection[0][0])>1e-8f && std::abs(projection[1][1])>1e-8f;
        aoProjectionFast_=standardProjection;
        aoDepthLevelsReady_=frame.ssao&&frame.aoRadius>0&&frame.aoLinearDepth?(frame.aoDepthHierarchy?uint32_t(targets_->aoDepthViews.size()):1):0;
        EffectsBlock effects{frame.view,projection,{frame.aoRadius,frame.aoBias,frame.aoPower,frame.ssao?1.f:0.f},{frame.aoHorizon?1.f:0.f,float(frame.aoSlices),float(frame.aoSteps),frame.aoDenoise?1.f:0.f},inverseProjection,{frame.aoLinearDepth?1.f:0.f,aoDepthLevelsReady_>1?1.f:0.f,float(std::max(1u,aoDepthLevelsReady_)),standardProjection?1.f:0.f}};
        device.writeBuffer(effects_,0,sizeof(effects),&effects);}
    device.writeBuffer(camera_,0,64,&frame.viewProjection);device.writeBuffer(lighting_,0,sizeof(lighting),&lighting);
    while (objects_.size() < packets.size()) {
        auto data = resources_.buffer({128,rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDestination,"PBR object"});
        std::vector<rhi::BindingEntry> entries{{0,camera_,0,64,{},{}},{1,data,0,128,{},{}}};
        if (path_ == PbrPath::Forward) entries.push_back({2,lighting_,0,1472,{},{}});
        entries.push_back({4,inactiveWetParameters_,0,32,{},{}});entries.push_back({5,{},0,0,inactiveWetView_,wetSampler_});
        meshFrameEntry(entries,*packets[objects_.size()].mesh);
        auto bindings = resources_.bindings({geometryLayout_,entries});
        objects_.push_back({data,bindings,packets[objects_.size()].mesh->materialTangents()});
    }
    Resources frameResources(resources_.device);
    auto commands = device.createCommandList();
    std::vector<CulledMeshDraw> visibleDraws(packets.size());
    const auto& computeLimits=device.computeLimits();
    const bool triangleCull=frame.triangleCulling && computeLimits.supported && computeLimits.maxInvocations>=128 && computeLimits.maxThreads[0]>=128;
    if(!triangleCull)geometryCuller_.reset();
    bool hizCull=false;
    bool twoPhase=path_==PbrPath::Scene&&frame.hizCulling&&frame.clusters.enabled&&frame.clusters.batched&&frame.clusters.occlusion&&frame.clusters.twoPhase&&!frame.clusters.wireframe;
    if(!twoPhase)clusterHistoryValid_=false;
    if(frame.hizCulling && computeLimits.supported && computeLimits.maxInvocations>=64 && computeLimits.maxThreads[0]>=64 && computeLimits.maxThreads[1]>=8 && computeLimits.maxStorageImages && device.supportsTextureSubresources()){
        try{
            if(!hizCuller_)hizCuller_=std::make_unique<GpuHiZCuller>(resources_.device,directory_);
            if(twoPhase){
                if(!clusterHistory_)clusterHistory_=std::make_unique<GpuHiZCuller>(resources_.device,directory_);
                hizCull=hizCuller_->prepareDepth(frame.viewportWidth,frame.viewportHeight,frame.clusters.fusedPyramid?2:0);
            }else hizCull=hizCuller_->prepare(frame,packets);
        }catch(const rhi::ResourceBudgetExceeded&){hizCuller_.reset();}
    }else hizCuller_.reset();
    twoPhase=twoPhase&&hizCull;
    const bool useClusterHistory=twoPhase&&clusterHistoryValid_;
    if(twoPhase&&captureClusterIntermediates_&&!targets_->clusterEarlyAlbedo)targets_->clusterEarlyAlbedo=targets_->resources.texture({frame.viewportWidth,frame.viewportHeight,rhi::Format::RGBA16Float,rhi::TextureUsage::CopyDestination|rhi::TextureUsage::CopySource,"Cluster actual early G-buffer albedo"});
    if(twoPhase&&captureClusterIntermediates_&&!targets_->clusterEarlyDepth)targets_->clusterEarlyDepth=targets_->resources.texture({frame.viewportWidth,frame.viewportHeight,rhi::Format::R32Float,rhi::TextureUsage::Storage|rhi::TextureUsage::CopySource,"Cluster actual early depth"});
    std::vector<CulledMeshDraw> lateDraws(packets.size());
    const std::array<glm::vec4,2> wetZero={glm::vec4(0,0,1,1),glm::vec4(0)};
    auto waterWetBuffer=frameResources.buffer({32,rhi::BufferUsage::Uniform|rhi::BufferUsage::CopyDestination,"Frame water wetness"},wetZero.data());
    auto waterWetView=inactiveWetView_;
    std::vector<rhi::BindingSetHandle> geometrySets;geometrySets.reserve(packets.size());
    for(size_t i=0;i<packets.size();++i){
        auto& slot=objects_[i];const auto& mesh=*packets[i].mesh;
        if(slot.frameTangents.value!=mesh.materialTangents().value){
            std::vector<rhi::BindingEntry> entries{{0,camera_,0,64,{},{}},{1,slot.data,0,128,{},{}},{4,inactiveWetParameters_,0,32,{},{}},{5,{},0,0,inactiveWetView_,wetSampler_}};
            if(path_==PbrPath::Forward)entries.push_back({2,lighting_,0,1472,{},{}});
            meshFrameEntry(entries,mesh);slot.bindings=resources_.bindings({geometryLayout_,entries});slot.frameTangents=mesh.materialTangents();
        }
        geometrySets.push_back(slot.bindings);
    }
    rhi::RenderPassDesc pass;
    pass.color = targets_->hdrView;
    pass.depth = targets_->depthView;
    pass.clearColor = {0, 0, 0, 1};
    engine::RenderGraph graph;
    const auto disableCulling=std::getenv("SCENERENDERER_DISABLE_GRAPH_CULLING");
    graph.enableCulling(frame.graphCulling && (!disableCulling || std::strcmp(disableCulling,"1")!=0));
    graph.exportResource("output");
    if(path_==PbrPath::Scene)for(auto name:{"sss-depth","depth"})graph.describe(name,{1,1,true,"depth32"});
    using A = engine::RenderGraph::Access;
    using F = engine::RenderGraph::PassFlags;
    graph.import("assets");
    if(triangleCull && std::any_of(packets.begin(),packets.end(),[&](const auto& p){return p.mesh->indexCount()/3>=frame.triangleCullMinimum && !p.mesh->instances() && !p.material->transparent() && p.material->extension().lobes.w==0 && !p.wireframe;})){
        if(!geometryCuller_)geometryCuller_=std::make_unique<GpuGeometryCuller>(resources_.device,directory_);
        graph.add("geometry-visibility",{{"assets",A::Read},{"geometry-visibility",A::Write}},[&]{
            visibleDraws=geometryCuller_->record(frameResources,commands,frame,packets);
            geometryCullStats_.triangleCandidates=geometryCuller_->candidateTriangles();
            geometryCullStats_.bufferBytes=geometryCuller_->allocatedBytes();
            geometryCullStats_.triangleCacheHits=geometryCuller_->cacheHits();geometryCullStats_.triangleDispatches=geometryCuller_->dispatches();
            for(const auto& draw:visibleDraws)if(draw.arguments)++geometryCullStats_.triangleDraws;
        });
    }else {geometryCuller_.reset();graph.import("geometry-visibility");}
    if(frame.clusters.enabled&&computeLimits.supported&&computeLimits.maxInvocations>=128&&computeLimits.maxThreads[0]>=128&&
       std::any_of(packets.begin(),packets.end(),[](const auto& p){return bool(p.clusters);})){
        if(!clusterCuller_)clusterCuller_=std::make_unique<GpuClusterCuller>(resources_.device,directory_);
        graph.add("cluster-visibility",{{"assets",A::Read},{"geometry-visibility",A::Read},{"cluster-visibility",A::Write}},[&]{
            visibleDraws=clusterCuller_->record(frameResources,commands,frame,packets,std::move(visibleDraws),twoPhase,useClusterHistory?clusterHistory_->pyramidView():rhi::TextureViewHandle{},useClusterHistory?clusterHistory_->pyramidLevels():0,clusterHistoryVP_);
            if(twoPhase&&!clusterCuller_->stats().twoPhase){twoPhase=false;hizCull=false;clusterHistoryValid_=false;hizCuller_->cancel();}
        });
    }else {clusterCuller_.reset();graph.import("cluster-visibility");}
    if(twoPhase&&!clusterCuller_){twoPhase=false;hizCull=false;}
    if(!twoPhase)clusterHistoryValid_=false;
    const bool clusterOcclusion=hizCull&&frame.clusters.enabled&&frame.clusters.batched&&frame.clusters.occlusion&&clusterCuller_&&!frame.clusters.wireframe;
    if(clusterOcclusion&&!twoPhase){
        graph.add("cluster-occluder-depth",{{"assets",A::Read},{"cluster-visibility",A::Read},{"geometry-visibility",A::Read},{"cluster-pyramid",A::Write}},[&]{
            hizCuller_->recordPyramid(frameResources,commands,frame,packets,visibleDraws);
        });
        graph.add("cluster-occlusion",{{"assets",A::Read},{"cluster-visibility",A::Read},{"cluster-pyramid",A::Read},{"cluster-occlusion",A::Write}},[&]{
            visibleDraws=clusterCuller_->recordOcclusion(frameResources,commands,frame,packets,std::move(visibleDraws),hizCuller_->pyramidView(),hizCuller_->pyramidLevels());
        });
        graph.add("hiz-visibility",{{"assets",A::Read},{"geometry-visibility",A::Read},{"cluster-occlusion",A::Read},{"cluster-pyramid",A::Read},{"hiz-visibility",A::Write}},[&]{
            visibleDraws=hizCuller_->recordVisibility(frameResources,commands,frame,packets,visibleDraws);
        });
    }else if(hizCull&&!twoPhase)graph.add("hiz-visibility",{{"assets",A::Read},{"geometry-visibility",A::Read},{"cluster-visibility",A::Read},{"hiz-visibility",A::Write}},[&]{
        visibleDraws=hizCuller_->record(frameResources,commands,frame,packets,visibleDraws);
    });else graph.import("hiz-visibility");
    if(frame.sky)graph.add("atmosphere",{{"environment",A::Write}},[&]{atmosphere_->update(frame.atmosphere,solar);},F::NeverCull);
    else graph.import("environment");
    if(!frame.oceans.empty())graph.add("ocean-simulation",{{"ocean-state",A::Write}},[&]{
        for(const auto& ocean:frame.oceans)oceans_.at(ocean.id)->simulate(frame.timeSeconds,ocean,frame.cameraPosition);
        // The first enabled local water domain owns wetness for this frame.
        for(const auto& ocean:frame.oceans)if(ocean.shore.enabled&&ocean.shore.wetSand&&ocean.bathymetry){
            auto& surface=*oceans_.at(ocean.id);waterWetView=surface.wetnessView();
            const std::array<glm::vec4,2> data={surface.shorePatch(),glm::vec4(1,ocean.shore.foam?1.f:0.f,0,0)};
            device.writeBuffer(waterWetBuffer,0,32,data.data());
            for(size_t i=0;i<packets.size();++i){
                std::vector<rhi::BindingEntry> entries={{0,camera_,0,64,{},{}},{1,objects_[i].data,0,128,{},{}},{4,waterWetBuffer,0,32,{},{}},{5,{},0,0,waterWetView,wetSampler_}};
                if(path_==PbrPath::Forward)entries.push_back({2,lighting_,0,1472,{},{}});
                meshFrameEntry(entries,*packets[i].mesh);
                geometrySets[i]=frameResources.bindings({geometryLayout_,entries});
            }break;
        }
    },F::NeverCull);else graph.import("ocean-state");
    if(shadows_)graph.add("shadows-rsm",{{"assets",A::Read},{"environment",A::Read},{"shadow-state",A::Write}},[&]{auto shadowFrame=frame;shadowFrame.viewProjection=shadowViewProjection;shadows_->render(shadowFrame,auxiliary);});
    if(frame.clouds.enabled && frame.weather.enabled && frame.weather.cloudShadows)graph.add("cloud-ground-shadow",{{"environment",A::Read},{"shadow-state",A::ReadWrite}},[&]{auto field=clouds_->recordGroundShadow(frameResources,commands,frame,solar,irradianceView_,shadows_->view(),shadows_->cloudViewport());shadows_->setCloudShadow(field);},F::NeverCull);
    if(frame.ddgi.enabled){
        if(path_!=PbrPath::Scene)throw std::invalid_argument("DDGI stage one requires the Scene rendering path");
        frame.ddgi.validate();
        if(!ddgi_)ddgi_=std::make_unique<GpuDdgi>(resources_.device,directory_);
        ddgi_->setProbeTracer(ddgiProbeTracer_);
        graph.add("ddgi-update",{{"assets",A::Read},{"environment",A::Read},{"shadow-state",A::Read},{"ddgi-field",A::Write}},[&]{ddgi_->update(frameResources,commands,frame,auxiliary,lighting_,shadows_->parameters(),shadows_->view(),skyView_);},F::NeverCull);
    }else ddgi_.reset();
    rhi::TextureViewHandle resolved;
    for(size_t i=0;i<packets.size();++i)device.writeBuffer(objects_[i].data,0,128,&blocks[i]);
    if (shadows_)
        graph.add("back", {{"assets", A::Read}, {"backdepth", A::Write}, {"sss-depth", A::Write}}, [&] {
            if (shadows_) {
                pass = {};
                pass.color = targets_->backDepthView;
                pass.clearColor = {0, 0, 0, 0};
                pass.depth = targets_->backTestView;
                pass.clearDepth = 0;
                commands.beginRenderPass(pass);
                commands.bindPipeline(backDepthPipeline_);
                for (size_t i = 0; i < packets.size(); ++i)
                    if (packets[i].material->extension().lobes.w > 0 && !packets[i].mesh->instances()) {
                        auto b = frameResources.bindings(
                            {ShadowRenderer::objectLayout(),
                             {{0, camera_, 0, 64, {}, {}}, {1, objects_[i].data, 0, 128, {}, {}}}});
                        commands.bindBindingSet(b);
                        packets[i].material->bindShadow(commands);
                        packets[i].mesh->draw(commands);
                    }
                commands.endRenderPass();
                pass = {};
            }
        });
    auto recordGeometry=[&](bool late){
                  if(late&&std::none_of(lateDraws.begin(),lateDraws.end(),[](const auto& d){return bool(d.arguments);}))return;
                  pass={};pass.depth=targets_->depthView;pass.clearColor={0,0,0,1};pass.color=targets_->hdrView;
                  if (path_ != PbrPath::Forward) {
                      pass.color = targets_->gbufferViews[0];
                      pass.clearColor = {0, 0, 0, 0};
                      for (size_t i = 1; i < 6u; ++i)
                          pass.additionalColors.push_back({targets_->gbufferViews[i]});
                  }
                  if(late){pass.colorLoad=rhi::LoadOp::Load;pass.depthLoad=rhi::LoadOp::Load;for(auto& a:pass.additionalColors)a.load=rhi::LoadOp::Load;}
                  commands.beginRenderPass(pass);
                  commands.bindPipeline(forward_);
                  for (size_t i = 0; i < packets.size(); ++i) {
                      if(late&&!lateDraws[i].arguments)continue;
                      if (path_ == PbrPath::Scene && packets[i].material->transparent())
                          continue;
                      if (packets[i].mesh->instances()) {
                          auto layout = geometryLayout_;
                          layout.entries.push_back(
                              {3, rhi::BindingType::StorageRead, rhi::ShaderStage::Vertex, "OutPose", 64});
                          std::vector<rhi::BindingEntry> entries{
                              {0, camera_, 0, 64, {}, {}},
                              {1, objects_[i].data, 0, 128, {}, {}},
                              {3,
                               packets[i].mesh->instances(),
                               0,
                               size_t(packets[i].mesh->instanceCapacity()) * 64,
                               {},
                               {}}};
                          if (path_ == PbrPath::Forward)
                              entries.push_back({2, lighting_, 0, 1472, {}, {}});
                          entries.push_back({4,waterWetBuffer,0,32,{},{}});entries.push_back({5,{},0,0,waterWetView,wetSampler_});
                          meshFrameEntry(entries,*packets[i].mesh);
                          commands.bindPipeline(packets[i].wireframe ? wireframeInstanced_ : instanced_);
                          commands.bindBindingSet(frameResources.bindings({layout, entries}));
                      } else {
                          commands.bindPipeline(packets[i].wireframe ? wireframe_ : forward_);
                          commands.bindBindingSet(geometrySets[i]);
                      }
                      packets[i].material->bind(commands);
                      const auto& draw=late?lateDraws[i]:visibleDraws[i];
                      packets[i].mesh->draw(commands,draw.indices,draw.arguments,draw.argumentOffset);
                  }
                  commands.endRenderPass();
                  if(!late&&twoPhase&&captureClusterIntermediates_){commands.copyTexture(targets_->gbuffer[2],targets_->clusterEarlyAlbedo);hizCuller_->recordDepthCopy(frameResources,commands,targets_->depthView,frameResources.view(targets_->clusterEarlyDepth),frame.viewportWidth,frame.viewportHeight);}
                  pass = {};
                  pass.clearColor = {0, 0, 0, 1};
    };
    graph.add(twoPhase?"geometry-early":"geometry",
              {{"geometry-visibility",A::Read}, {"hiz-visibility",A::Read},{"cluster-visibility",A::Read}, {"ocean-state", A::Read},
               {"assets", A::Read},
               {"environment", A::Read},
               {"depth", A::Write},
               {path_ == PbrPath::Forward ? "hdr" : "gbuffer", A::Write}},
[&]{recordGeometry(false);});
    if(twoPhase){
        graph.add("cluster-current-pyramid",{{"depth",A::Read},{"cluster-pyramid",A::Write}},[&]{if(twoPhase){hizCuller_->fusedBuild(frame.clusters.fusedPyramid);hizCuller_->recordDepthPyramid(frameResources,commands,targets_->depthView,"hiz/current-");}});
        graph.add("cluster-late-visibility",{{"cluster-visibility",A::Read},{"cluster-pyramid",A::Read},{"cluster-late",A::Write}},[&]{if(twoPhase)lateDraws=clusterCuller_->recordLate(frameResources,commands,frame,packets,hizCuller_->pyramidView(),hizCuller_->pyramidLevels());});
        graph.add("geometry-late",{{"cluster-late",A::Read},{"depth",A::ReadWrite},{"gbuffer",A::ReadWrite}},[&]{recordGeometry(true);});
    }
    if(shadows_)graph.add("ao-depth",{{"gbuffer",A::Read},{"ao-depth",A::Write}},[&]{recordAoDepth(commands);});
    if (shadows_)
        graph.add("ao-raw", {{"gbuffer", A::Read}, {"ao-depth",A::Read}, {"ao-raw", A::Write}}, [&] {
            if (shadows_) {
                pass.color = targets_->rawAoView;
                pass.clearColor = {1, 1, 1, 1};
                commands.beginRenderPass(pass);
                if(frame.ssao && frame.aoRadius>0){
                commands.bindPipeline(ssaoPipeline_);
                commands.bindBindingSet(effectsBindings_);
                commands.bindBindingSet(targets_->ssaoBindings);
                commands.bindVertexBuffer(quad_);
                commands.draw(6);
                }
                commands.endRenderPass();
            }
        });
    if(shadows_)graph.add("ao-filter",{{"ao-raw",A::Read},{"gbuffer",A::Read},{"ao-depth",A::Read},{"ao",A::Write}},[&]{
        pass.color=targets_->aoView;pass.clearColor={1,1,1,1};commands.beginRenderPass(pass);
        if(frame.ssao && frame.aoRadius>0){commands.bindPipeline(aoFilterPipeline_);commands.bindBindingSet(effectsBindings_);commands.bindBindingSet(targets_->aoFilterBindings);commands.bindVertexBuffer(quad_);commands.draw(6);}
        commands.endRenderPass();
    });
    std::vector<engine::RenderGraph::Use> lightingUses{
        {"gbuffer", A::Read}, {"environment", A::Read}, {"hdr", A::Write}};
    if (shadows_) {
        lightingUses.push_back({"shadow-state", A::Read});
        lightingUses.push_back({"ao", A::Read});
        lightingUses.push_back({"backdepth", A::Read});
    }
    if (path_ != PbrPath::Forward)
        graph.add("light", std::move(lightingUses), [&] {
            if (path_ != PbrPath::Forward) {
                pass.clearColor = {0, 0, 0, 1};
                pass.color = targets_->hdrView;
                commands.beginRenderPass(pass);
                commands.bindPipeline(deferredPipeline_);
                commands.bindBindingSet(targets_->lightBindings);
                commands.bindBindingSet(targets_->deferredBindings);
                commands.bindBindingSet(targets_->effectBindings);
                commands.bindVertexBuffer(quad_);
                commands.draw(6);
                commands.endRenderPass();
            }
        });
    if (frame.forwardShading && path_ == PbrPath::Scene && !(frame.rsm && frame.rsmSettings.indirectOnly))
        graph.add("forward-copy", {{"hdr", A::Read}, {"opaque", A::Write}},
                  [&] { commands.copyTexture(targets_->hdr, targets_->opaque); });
    if (frame.forwardShading && path_ == PbrPath::Scene && !(frame.rsm && frame.rsmSettings.indirectOnly))
        graph.add("forward",
                  {{"shadow-state", A::Read}, {"assets", A::Read},
                   {"environment", A::Read},
                   {"hdr", A::ReadWrite},
                   {"depth", A::ReadWrite},
                   {"ao", A::Read},
                   {"backdepth", A::Read},
                   {"opaque", A::Read}},
                  [&] {
                      if (frame.forwardShading && path_ == PbrPath::Scene &&
                          !(frame.rsm && frame.rsmSettings.indirectOnly)) {
                          auto environment = frameResources.bindings(
                              {sceneForwardLayout_,
                               {{0, skyParameters_, 0, 128, {}, {}},
                                {1, shadows_->parameters(), 0, sizeof(ShadowParameters), {}, {}},
                                {2, {}, 0, 0, skyView_, skySampler_},
                                {3, {}, 0, 0, irradianceView_, skySampler_},
                                {4, {}, 0, 0, shadows_->view(), hdrSampler_},
                                {5, {}, 0, 0, targets_->opaqueView, hdrSampler_},
                                {6, {}, 0, 0, targets_->aoView, hdrSampler_},
                                {7, {}, 0, 0, targets_->backDepthView, hdrSampler_}}});
                          pass = {};
                          pass.color = targets_->hdrView;
                          pass.colorLoad = rhi::LoadOp::Load;
                          pass.depth = targets_->depthView;
                          pass.depthLoad = rhi::LoadOp::Load;
                          commands.beginRenderPass(pass);
                          for (size_t i = 0; i < packets.size(); ++i) {
                              auto &packet = packets[i];
                              if (packet.material->transparent())
                                  continue;
                              auto layout = withMeshFrames(withWaterWet(frameLayout()),resources_.device->computeLimits().supported);
                              std::vector<rhi::BindingEntry> entries{{0, camera_, 0, 64, {}, {}},
                                                                     {1, objects_[i].data, 0, 128, {}, {}},
                                                                     {2, lighting_, 0, 1472, {}, {}},
                                                                     {4,waterWetBuffer,0,32,{},{}},{5,{},0,0,waterWetView,wetSampler_}};
                              if (packet.mesh->instances()) {
                                  layout.entries.push_back({3, rhi::BindingType::StorageRead,
                                                            rhi::ShaderStage::Vertex, "OutPose", 64});
                                  entries.push_back({3,
                                                     packet.mesh->instances(),
                                                     0,
                                                     size_t(packet.mesh->instanceCapacity()) * 64,
                                                     {},
                                                     {}});
                                  commands.bindPipeline(packet.wireframe ? sceneForwardWireInstanced_
                                                                         : sceneForwardInstanced_);
                              } else
                                  commands.bindPipeline(packet.wireframe ? sceneForwardWire_ : sceneForward_);
                              meshFrameEntry(entries,*packet.mesh);
                            commands.bindBindingSet(frameResources.bindings({layout, entries}));
                              packet.material->bind(commands);
                              commands.bindBindingSet(environment);
                              packet.mesh->draw(commands,visibleDraws[i].indices,visibleDraws[i].arguments,visibleDraws[i].argumentOffset);
                          }
                          commands.endRenderPass();
                          pass = {};
                      }
                  });
    if (shadows_)
        graph.add("motion", {{"depth", A::Read}, {"assets", A::Read}, {"motion", A::Write}}, [&] {
            if (shadows_) {
                pass = {};
                pass.color = targets_->motionView;
                pass.clearColor = {0, 0, 0, 0};
                pass.depth = targets_->depthView;
                pass.depthLoad = rhi::LoadOp::Load;
                commands.beginRenderPass(pass);
                if (frame.taa) {
                    std::array<glm::mat4, 3> camera{
                        frame.viewProjection,
                        temporal_->valid() ? temporal_->previousVP() : frame.viewProjection,
                        temporal_->valid() ? temporal_->previousView() : frame.view};
                    auto cameraBuffer = frameResources.buffer(
                        {sizeof(camera), rhi::BufferUsage::Uniform, "Motion camera"}, camera.data());
                    for (size_t i = 0; i < packets.size(); ++i) {
                        const auto &packet = packets[i];
                        if (packet.material->transparent())
                            continue;
                        const uint64_t id = packet.id
                                                ? packet.id
                                                : uint64_t(reinterpret_cast<uintptr_t>(packet.mesh.get())) ^
                                                      (uint64_t(originalIndices[i] + 1) << 32);
                        auto old = previousModels_.find(id);
                        struct alignas(16) Object {
                            glm::mat4 model, previous;
                            glm::ivec4 flags;
                        };
                        Object object{
                            packet.model,
                            old == previousModels_.end() ? packet.model : old->second,
                            {(((packet.mesh->instances() || packet.mesh->indirectBuffer()) && !packet.reliableGeneratedHistory)||(packet.mesh->previousVertices()&&!packet.mesh->vertexHistoryReliable())) ? 1 : 0, packet.mesh->previousVertices()?1:0, 0, 0}};
                        auto data = frameResources.buffer(
                            {sizeof(object), rhi::BufferUsage::Uniform, "Motion object"}, &object);
                        auto layout = motionLayout_;
                        std::vector<rhi::BindingEntry> entries{{0, cameraBuffer, 0, 192, {}, {}},
                                                               {1, data, 0, 144, {}, {}},{5,packet.mesh->previousVertices()?packet.mesh->previousVertices():packet.mesh->vertexBuffer(),0,size_t(packet.mesh->vertexCount())*sizeof(MeshVertex),{},{}}};
                        if (packet.mesh->instances()) {
                            layout.entries.push_back(
                                {3, rhi::BindingType::StorageRead, rhi::ShaderStage::Vertex, "OutPose", 64});
                            entries.push_back({3,
                                               packet.mesh->instances(),
                                               0,
                                               size_t(packet.mesh->instanceCapacity()) * 64,
                                               {},
                                               {}});
                            layout.entries.push_back({4,rhi::BindingType::StorageRead,rhi::ShaderStage::Vertex,"PreviousPose",64});
                            entries.push_back({4,packet.mesh->previousInstances()?packet.mesh->previousInstances():packet.mesh->instances(),0,size_t(packet.mesh->instanceCapacity())*64,{},{}});
                            commands.bindPipeline(motionInstanced_);
                        } else
                            commands.bindPipeline(motionPipeline_);
                        commands.bindBindingSet(frameResources.bindings({layout, entries}));
                        packet.material->bindShadow(commands);
                        packet.mesh->draw(commands,visibleDraws[i].indices,visibleDraws[i].arguments,visibleDraws[i].argumentOffset);
                    }
                }
                commands.endRenderPass();
                pass = {};
            }
        });
    if(frame.ddgi.enabled)graph.add("ddgi-surface",{{"ddgi-field",A::Read},{"gbuffer",A::Read},{"ao",A::Read},{"environment",A::Read},{"hdr",A::ReadWrite}},[&]{ddgi_->compose(frameResources,commands,frame,targets_->hdrView,std::vector<rhi::TextureViewHandle>(targets_->gbufferViews.begin(),targets_->gbufferViews.end()),targets_->aoView,skyParameters_,irradianceView_);});
    if(frame.clouds.enabled)
        graph.add("clouds",{{"environment",A::Read},{"depth",A::Read},{"hdr",A::ReadWrite},{"motion",A::ReadWrite}},[&]{
            clouds_->record(frameResources,commands,frame,solar,targets_->depthView,irradianceView_,targets_->hdrView,targets_->motionView);
        },F::NeverCull);
    if (!frame.oceans.empty())
        graph.add("ocean-copy", {{"hdr", A::Read}, {"opaque", A::Write}},
                  [&] { commands.copyTexture(targets_->hdr, targets_->opaque); });
    for(size_t oceanIndex=0;oceanIndex<frame.oceans.size();++oceanIndex){
        const auto captureState="water-capture-"+std::to_string(frame.oceans[oceanIndex].id);
        graph.add("water-capture/"+std::to_string(frame.oceans[oceanIndex].id),
            {{"assets",A::Read},{"shadow-state",A::Read},{"ocean-state",A::Read},{"environment",A::Read},{"gbuffer",A::Read},{captureState,A::Write}},[&,oceanIndex]{
                const auto& ocean=frame.oceans[oceanIndex];auto& water=*oceans_.at(ocean.id);
                std::vector<rhi::BindingEntry> environment={{0,skyParameters_,0,128,{},{}},
                    {1,shadows_->parameters(),0,sizeof(ShadowParameters),{},{}},{2,{},0,0,skyView_,skySampler_},
                    {3,{},0,0,irradianceView_,skySampler_},{4,{},0,0,shadows_->view(),hdrSampler_}};
                water.captureUnderwater(frameResources,commands,frame,ocean,auxiliary,camera_,lighting_,environment,targets_->width,targets_->height,targets_->gbufferViews[0],aerialParameters(frame,solar),frame.sky?atmosphere_->transmittance():skyView_,frame.sky?atmosphere_->multiple():skyView_,shadows_->parameters(),shadows_->view());
            },F::NeverCull);
        const auto volumeState="water-volume-"+std::to_string(frame.oceans[oceanIndex].id);
        graph.add("water-volume/"+std::to_string(frame.oceans[oceanIndex].id),
            {{captureState,A::Read},{"ocean-state",A::Read},{"shadow-state",A::Read},{"environment",A::Read},{"gbuffer",A::Read},{"opaque",A::Read},{volumeState,A::Write}},[&,oceanIndex]{
                const auto& ocean=frame.oceans[oceanIndex];auto& water=*oceans_.at(ocean.id);
                water.recordVolume(frameResources,commands,frame,ocean,skyView_,targets_->opaqueView,targets_->gbufferViews[0],targets_->gbufferViews[1],shadows_->parameters(),shadows_->view());
            });
        graph.add("water-surface/"+std::to_string(frame.oceans[oceanIndex].id),
            {{captureState,A::Read},{volumeState,A::Read},{"ocean-state",A::Read},{"shadow-state",A::Read},{"environment",A::Read},{"gbuffer",A::Read},{"depth",A::ReadWrite},{"hdr",A::ReadWrite},{"motion",A::ReadWrite},{"opaque",A::Read}},[&,oceanIndex]{
                const auto& ocean=frame.oceans[oceanIndex];auto& water=*oceans_.at(ocean.id);
                pass={};pass.color=targets_->hdrView;pass.colorLoad=rhi::LoadOp::Load;
                pass.depth=targets_->depthView;pass.depthLoad=rhi::LoadOp::Load;pass.additionalColors={{targets_->motionView,rhi::LoadOp::Load}};
                commands.setLabel("water/surface");commands.beginRenderPass(pass);
                water.record(frameResources,commands,frame,ocean,skyView_,targets_->opaqueView,targets_->gbufferViews[0],targets_->gbufferViews[1],shadows_->parameters(),shadows_->view());
                commands.endRenderPass();pass={};
            },F::NeverCull);
        graph.add("water-eye/"+std::to_string(frame.oceans[oceanIndex].id),
            {{captureState,A::Read},{"ocean-state",A::Read},{"shadow-state",A::Read},{"environment",A::Read},{"gbuffer",A::Read},{"depth",A::Read},{"hdr",A::ReadWrite},{"opaque",A::Read}},[&,oceanIndex]{
                const auto& ocean=frame.oceans[oceanIndex];auto& water=*oceans_.at(ocean.id);
                water.recordUnderwaterFog(frameResources,commands,frame,ocean,targets_->hdrView,targets_->opaqueView,targets_->gbufferViews[0],targets_->gbufferViews[1],skyView_,shadows_->parameters(),shadows_->view(),targets_->depthView);
            });
    }
    if (path_ == PbrPath::Scene && std::any_of(packets.begin(),packets.end(),[](const auto& p){return p.material->transparent();}))
        graph.add(
            "alpha",
            {{"assets", A::Read},
             {"environment", A::Read},
             {"depth", A::ReadWrite},
             {"hdr", A::ReadWrite},
             {"motion", A::ReadWrite}},
            [&] {
                if (path_ == PbrPath::Scene) {
                    std::vector<size_t> sorted;
                    for (size_t i = 0; i < packets.size(); ++i)
                        if (packets[i].material->transparent())
                            sorted.push_back(i);
                    std::stable_sort(sorted.begin(), sorted.end(), [&](size_t a, size_t b) {
                        auto center = [&](size_t i) {
                            return frame.view * packets[i].model *
                                   glm::vec4((packets[i].mesh->boundsMin() + packets[i].mesh->boundsMax()) *
                                                 .5f,
                                             1);
                        };
                        return center(a).z < center(b).z;
                    });
                    if (!sorted.empty()) {
                        auto environment = frameResources.bindings(
                            {transparentLayout_,
                             {{0, skyParameters_, 0, 128, {}, {}},
                              {1, shadows_->parameters(), 0, sizeof(ShadowParameters), {}, {}},
                              {2, {}, 0, 0, skyView_, skySampler_},
                              {3, {}, 0, 0, irradianceView_, skySampler_},
                              {4, {}, 0, 0, shadows_->view(), hdrSampler_}}});
                        pass = {};
                        pass.color = targets_->hdrView;
                        pass.colorLoad = rhi::LoadOp::Load;
                        pass.depth = targets_->depthView;
                        pass.depthLoad = rhi::LoadOp::Load;
                        pass.additionalColors = {{targets_->motionView, rhi::LoadOp::Load}};
                        commands.beginRenderPass(pass);
                        for (auto i : sorted) {
                            auto layout = withMeshFrames(frameLayout(),resources_.device->computeLimits().supported);
                            std::vector<rhi::BindingEntry> entries{{0, camera_, 0, 64, {}, {}},
                                                                   {1, objects_[i].data, 0, 128, {}, {}},
                                                                   {2, lighting_, 0, 1472, {}, {}}};
                            if (packets[i].mesh->instances()) {
                                layout.entries.push_back({3, rhi::BindingType::StorageRead,
                                                          rhi::ShaderStage::Vertex, "OutPose", 64});
                                entries.push_back({3,
                                                   packets[i].mesh->instances(),
                                                   0,
                                                   size_t(packets[i].mesh->instanceCapacity()) * 64,
                                                   {},
                                                   {}});
                                commands.bindPipeline(transparentInstanced_);
                            } else
                                commands.bindPipeline(transparentPipeline_);
                            meshFrameEntry(entries,*packets[i].mesh);
                            commands.bindBindingSet(frameResources.bindings({layout, entries}));
                            packets[i].material->bind(commands);
                            commands.bindBindingSet(environment);
                            packets[i].mesh->draw(commands);
                        }
                        commands.endRenderPass();
                        pass = {};
                    }
                }
            });
    if(frame.weather.enabled&&(frame.weather.fog||(frame.sky&&frame.weather.aerialPerspective&&frame.weather.aerialDistanceScale>0))){
        if(!airFog_)airFog_=std::make_unique<GpuAirFog>(resources_.device,directory_);
        graph.add("air-fog",{{"depth",A::Read},{"motion",A::Read},{"environment",A::Read},{"shadow-state",A::Read},{"hdr",A::ReadWrite}},[&]{airFog_->record(frameResources,commands,frame,targets_->hdr,targets_->hdrView,targets_->depthView,targets_->motionView,solar,frame.sky?atmosphere_->transmittance():skyView_,frame.sky?atmosphere_->multiple():skyView_,shadows_->parameters(),shadows_->view());});
    }
    if(frame.rainVisible&&frame.rainMetersPerSecond>0){
        if(!rain_)rain_=std::make_unique<GpuRain>(resources_.device,directory_);
        graph.add("rain",{{"depth",A::Read},{"hdr",A::ReadWrite}},[&]{rain_->record(frameResources,commands,frame,targets_->hdrView,targets_->depthView);});
    }
    if (frame.taa)
        graph.add("temporal",
                  {{"hdr", A::Read}, {"depth", A::Read}, {"motion", A::Read}, {"resolved", A::Write}}, [&] {
                      if (frame.taa)
                          resolved = temporal_->record(frameResources, commands, frame, targets_->hdrView,
                                                       targets_->depthView, targets_->motionView);
                  },F::NeverCull);
    graph.add("post-process", {{frame.taa ? "resolved" : "hdr", A::Read}, {"depth", A::Read}, {"output", A::Write}}, [&] {
        PostProcessView view;view.view=frame.view;view.viewProjection=frame.viewProjection;
        view.unjitteredViewProjection=source.viewProjection;view.cameraPosition=frame.cameraPosition;
        view.time=frame.timeSeconds;view.historyKey=frame.historyKey;
        post_->record(frameResources,commands,targets_->width,targets_->height,
                      resolved?resolved:targets_->hdrView,targets_->depthView,targets_->outputView,
                      frame.postProcess,view,exposure,gamma,frame.toneMapping);
    });
    if(frame.ddgi.enabled&&frame.ddgi.debugProbes)
        graph.add("ddgi-probes",{{"ddgi-field",A::Read},{"gbuffer",A::Read},{"output",A::ReadWrite}},[&]{
            auto debugFrame=frame;
            if(frame.taa&&frame.taaHighQuality)debugFrame.viewProjection=source.viewProjection;
            else debugFrame.temporalJitter={0,0};
            ddgi_->visualize(frameResources,commands,debugFrame,targets_->outputView,targets_->gbufferViews[0],exposure,gamma);
        });
    engine::RenderGraph::Plan plan;
    {engine::TraceScope trace("graph/compile","render");plan=graph.compile();}
    if(path_==PbrPath::Scene) {
        size_t a=SIZE_MAX,b=SIZE_MAX;
        for(const auto& life:plan.lifetimes){if(life.resource=="depth")a=life.aliasSlot;if(life.resource=="sss-depth")b=life.aliasSlot;}
        if(a==SIZE_MAX || a!=b)throw std::logic_error("SSS and scene depth lifetimes overlap; cannot reuse storage");
    }
    graphPassStats_={};const auto names=graph.names();
    for(auto i:plan.activePasses)graphPassStats_.executed.push_back(names[i]);
    for(auto i:plan.culledPasses)graphPassStats_.culled.push_back(names[i]);
    clusterHistoryValid_=false; // A failed submission must never publish partial history.
    graph.execute(plan,[&](const std::string& name){commands.setLabel(name);});
    device.submit(commands);
    if(geometryCuller_)geometryCuller_->commit();
    if(clusterCuller_)clusterCuller_->commit();
    if(hizCull)hizCuller_->commit();
    if(twoPhase&&clusterCuller_&&clusterCuller_->stats().twoPhase){std::swap(hizCuller_,clusterHistory_);clusterHistoryVP_=frame.viewProjection;clusterHistoryValid_=true;}
    post_->commit();
    if(frame.clouds.enabled)clouds_->commit(frame);
    renderedVP_=frame.viewProjection;clusterPacketIndices_=originalIndices;
    temporalOutput_ = frame.taa;
    if (frame.taa)
        temporal_->commit(frame);
    previousTaa_ = source.taa;
    previousCamera_ = source.cameraPosition;
    previousProjection_ = source.viewProjection * glm::inverse(source.view);
    historyKey_ = frame.historyKey;
    std::map<uint64_t, glm::mat4> models;
    for (size_t i = 0; i < inputPackets.size(); ++i)
        models[inputPackets[i].id ? inputPackets[i].id
                             : uint64_t(reinterpret_cast<uintptr_t>(inputPackets[i].mesh.get())) ^
                                   (uint64_t(i + 1) << 32)] = inputPackets[i].model;
    previousModels_.swap(models);
    aoBenchmarkReady_=bool(shadows_)&&frame.ssao&&frame.aoRadius>0;
}
std::vector<float> ForwardPbrRenderer::readBackDepth() {
    if (!shadows_)
        throw std::invalid_argument("SSS unavailable on this path");
    return resources_.device->readTextureFloat(targets_->backDepth);
}
namespace {
std::vector<float> aoExpand(const std::vector<float>& values) {
    std::vector<float> rgba(values.size()*4,1);
    for(size_t i=0;i<values.size();++i)rgba[i*4]=rgba[i*4+1]=rgba[i*4+2]=values[i];
    return rgba;
}
}
void ForwardPbrRenderer::recordAoDepth(rhi::CommandList& commands) {
    for(uint32_t mip=0;mip<aoDepthLevelsReady_;++mip) {
        rhi::RenderPassDesc pass;pass.color=targets_->aoDepthViews[mip];pass.clearColor={0,0,0,0};
        commands.beginRenderPass(pass);commands.bindPipeline(mip?aoReducePipeline_:aoDepthPipeline_);
        if(mip)commands.bindBindingSet(targets_->aoReduceBindings[mip-1]);
        else {commands.bindBindingSet(effectsBindings_);commands.bindBindingSet(targets_->aoDepthBindings);}
        commands.bindVertexBuffer(quad_);commands.draw(6);commands.endRenderPass();
    }
}
uint32_t ForwardPbrRenderer::aoDepthLevels() const {resources_.device->checkThread();return aoBenchmarkReady_?aoDepthLevelsReady_:0;}
std::vector<float> ForwardPbrRenderer::readAoDepth(uint32_t mip) {
    if(!aoBenchmarkReady_ || mip>=aoDepthLevelsReady_)throw std::invalid_argument("AO depth mip has not been generated for this frame");
    const auto bytes=resources_.device->readTextureSubresource(targets_->aoDepth,{mip,0});
    std::vector<float> values(bytes.size()/4);std::memcpy(values.data(),bytes.data(),bytes.size());return values;
}
AoPassTimings ForwardPbrRenderer::measureAO(uint32_t warmup,uint32_t samples,uint32_t repetitions) {
    if(!aoBenchmarkReady_ || !samples || samples>256 || warmup>64 || !repetitions || repetitions>64)
        throw std::invalid_argument("AO benchmark requires a successful active Scene frame and bounded samples");
    auto& device=*resources_.device;device.waitIdle();AoPassTimings result;result.warmup=warmup;result.repetitions=repetitions;result.fastReconstruction=aoProjectionFast_;
    result.scope="isolated native submission with repeated identical AO encoders; GPU duration divided by repetitions, includes attachment/command-buffer overhead, warm-cache replay, excludes scene/presentation/readback";
    auto measure=[&](int stage,std::vector<double>& times){
        const bool filter=stage==2;
        for(uint32_t i=0;i<warmup+samples;++i){
            const auto before=device.gpuTimingStats();auto commands=device.createCommandList();commands.setLabel(stage==0?"ao-depth benchmark":filter?"ao-filter benchmark":"ao-raw benchmark");
            rhi::RenderPassDesc pass;pass.color=filter?targets_->aoView:targets_->rawAoView;pass.clearColor={1,1,1,1};
            for(uint32_t repeat=0;repeat<repetitions;++repeat){
                if(stage==0){recordAoDepth(commands);continue;}
                commands.beginRenderPass(pass);commands.bindPipeline(filter?aoFilterPipeline_:ssaoPipeline_);
                commands.bindBindingSet(effectsBindings_);commands.bindBindingSet(filter?targets_->aoFilterBindings:targets_->ssaoBindings);
                commands.bindVertexBuffer(quad_);commands.draw(6);commands.endRenderPass();
            }
            device.submit(commands);device.waitIdle();
            const auto timing=device.gpuTimingStats();
            if(!timing.supported||timing.samples<=before.samples||!std::isfinite(timing.milliseconds)||timing.milliseconds<=0)return false;
            if(i>=warmup)times.push_back(timing.milliseconds/repetitions);
        }
        return true;
    };
    result.supported=(!aoDepthLevelsReady_||measure(0,result.depthMilliseconds))&&measure(1,result.rawMilliseconds)&&measure(2,result.filterMilliseconds);
    if(!result.supported){result.depthMilliseconds.clear();result.rawMilliseconds.clear();result.filterMilliseconds.clear();}
    return result;
}
std::vector<float> ForwardPbrRenderer::readRawAO() {
    if(!targets_->rawAo)throw std::invalid_argument("AO unavailable on this path");
    return aoExpand(resources_.device->readTextureFloat(targets_->rawAo));
}
std::vector<float> ForwardPbrRenderer::readSSAO() {
    if (!shadows_)
        throw std::invalid_argument("SSAO unavailable on this path");
    return aoExpand(resources_.device->readTextureFloat(targets_->ao));
}
std::array<uint64_t,4> ForwardPbrRenderer::readShadowInstanceStats(){return shadows_?shadows_->readInstanceStats():std::array<uint64_t,4>{};}
std::vector<float> ForwardPbrRenderer::readShadowDepth() {
    if (!shadows_)
        throw std::invalid_argument("Shadows unavailable on this path");
    return shadows_->readDepth();
}
ShadowParameters ForwardPbrRenderer::shadowParameters() const {
    resources_.device->checkThread();
    if (!shadows_) throw std::invalid_argument("Shadows unavailable on this path");
    return shadows_->data();
}
std::vector<float> ForwardPbrRenderer::readMotion(){return resources_.device->readTextureFloat(targets_->motion);}
uint32_t ForwardPbrRenderer::waveParticleCount(uint64_t ocean)const{return oceans_.at(ocean)->waveParticleCount();}
std::vector<float> ForwardPbrRenderer::readWaterCaustics(uint64_t ocean,uint32_t cascade){return oceans_.at(ocean)->readCaustics(cascade);}
std::vector<float> ForwardPbrRenderer::readWaterCapture(uint64_t ocean,bool positions,bool aboveWater) {return oceans_.at(ocean)->readCapture(positions,aboveWater);}
std::vector<float> ForwardPbrRenderer::readShoreWater(uint64_t ocean,bool foam) {return oceans_.at(ocean)->readShore(foam);}
uint32_t ForwardPbrRenderer::shoreSubsteps(uint64_t ocean) const {return oceans_.at(ocean)->shoreSubsteps();}
std::vector<float> ForwardPbrRenderer::readGBuffer(uint32_t attachment) {
    if (path_ == PbrPath::Forward || attachment >= 6u)
        throw std::invalid_argument("Renderer: invalid G-buffer attachment");
    return resources_.device->readTextureFloat(targets_->gbuffer[attachment]);
}
std::vector<float> ForwardPbrRenderer::readClouds(){if(!clouds_)throw std::logic_error("Clouds disabled");return clouds_->read();}
std::vector<float> ForwardPbrRenderer::readCloudMetadata(){if(!clouds_)throw std::logic_error("Clouds disabled");return clouds_->readMetadata();}
std::vector<uint8_t> ForwardPbrRenderer::readCloudVoxels(){if(!clouds_)throw std::logic_error("Clouds disabled");return clouds_->readVoxels();}
std::vector<uint8_t> ForwardPbrRenderer::readCloudDistance(){if(!clouds_)throw std::logic_error("Clouds disabled");return clouds_->readDistance();}
std::array<uint64_t,3> ForwardPbrRenderer::cloudCacheBakes() const {if(!clouds_)throw std::logic_error("Clouds disabled");return clouds_->cacheBakes();}
std::vector<float> ForwardPbrRenderer::readCloudLight(){if(!clouds_)throw std::logic_error("Clouds disabled");return clouds_->readLight();}
std::array<uint32_t,2> ForwardPbrRenderer::cloudTileCounts(){if(!clouds_)throw std::logic_error("Clouds disabled");return {clouds_->readDispatch()[0],clouds_->totalTiles()};}
ClusterCullStats ForwardPbrRenderer::clusterCullStats()const{return clusterCuller_?clusterCuller_->stats():ClusterCullStats{};}
std::vector<ClusterCullCount> ForwardPbrRenderer::readClusterCullCounts(){auto out=clusterCuller_?clusterCuller_->readCounts():std::vector<ClusterCullCount>{};for(auto& c:out)c.packet=clusterPacketIndices_.at(c.packet);return out;}
std::vector<uint32_t> ForwardPbrRenderer::readClusterIndices(size_t packet){auto found=std::find(clusterPacketIndices_.begin(),clusterPacketIndices_.end(),packet);return clusterCuller_&&found!=clusterPacketIndices_.end()?clusterCuller_->readIndices(size_t(found-clusterPacketIndices_.begin())):std::vector<uint32_t>{};}
std::vector<TriangleCullCounts> ForwardPbrRenderer::readTriangleCullCounts() {return geometryCuller_?geometryCuller_->readCounts():std::vector<TriangleCullCounts>{};}
HiZCullStats ForwardPbrRenderer::hizCullStats() const {return clusterCuller_&&clusterCuller_->stats().twoPhase?clusterHistory_->stats():hizCuller_?hizCuller_->stats():HiZCullStats{};}
std::vector<HiZCullCount> ForwardPbrRenderer::readHiZCullCounts() {return hizCuller_?hizCuller_->readCounts():std::vector<HiZCullCount>{};}
std::vector<float> ForwardPbrRenderer::readHiZDepth(uint32_t mip) {return clusterCuller_&&clusterCuller_->stats().twoPhase?clusterHistory_->readDepth(mip):hizCuller_?hizCuller_->readDepth(mip):std::vector<float>{};}
std::vector<HiZCullBounds> ForwardPbrRenderer::hizCullBounds() const {return hizCuller_?hizCuller_->bounds():std::vector<HiZCullBounds>{};}
rhi::TextureHandle ForwardPbrRenderer::output() const { return targets_->output; }
std::vector<float> ForwardPbrRenderer::readHDR() {
    return temporalOutput_ ? temporal_->read() : resources_.device->readTextureFloat(targets_->hdr);
}
std::vector<uint8_t> ForwardPbrRenderer::readOutput() {
    return resources_.device->readTexture(targets_->output);
}
}

namespace render {rhi::TextureViewHandle ForwardPbrRenderer::depthView() const {return targets_->depthView;}}

namespace render {
std::vector<glm::mat4> ForwardPbrRenderer::shadowVisibilityViews() const {
    std::vector<glm::mat4> result;if(!shadows_ || (shadows_->data().settings.y<.5f && shadows_->data().settings.z<=0))return result;
    const auto& data=shadows_->data();
    if(data.settings.y>.5f)for(const auto& light:data.lights)for(int i=0;i<light.y && result.size()<7;++i)if(light.x+i>=0 && light.x+i<int(MaxShadowTiles))result.push_back(data.matrices[light.x+i]);
    if(data.rsmRect.z>0 && result.size()<8)result.push_back(data.rsmMatrix);return result;
}
}

namespace render {std::vector<float> ForwardPbrRenderer::readCloudShadow(){if(!clouds_)throw std::logic_error("Clouds disabled");return clouds_->readGroundShadow();}uint64_t ForwardPbrRenderer::cloudShadowBakes() const{return clouds_?clouds_->shadowBakes():0;}}

namespace render {std::vector<float> ForwardPbrRenderer::readAerialPerspective(bool transmission){if(!airFog_)throw std::logic_error("Aerial perspective disabled");return airFog_->readAerial(transmission);}}

namespace render {DdgiStats ForwardPbrRenderer::ddgiStats() const {return ddgi_?ddgi_->stats():DdgiStats{};}
void ForwardPbrRenderer::setDdgiProbeTracer(std::shared_ptr<pt::GpuProbeTracer> tracer){ddgi_.reset();ddgiProbeTracer_=std::move(tracer);}
std::vector<glm::vec4> ForwardPbrRenderer::readDdgiField() const {return ddgi_?ddgi_->readField():std::vector<glm::vec4>{};}
std::vector<float> ForwardPbrRenderer::readDdgiCapture() const {return ddgi_?ddgi_->readCapture():std::vector<float>{};}
std::vector<glm::vec4> ForwardPbrRenderer::sampleDdgi(const std::vector<glm::vec4>& p,const std::vector<glm::vec4>& n){if(!ddgi_)throw std::logic_error("DDGI disabled");return ddgi_->sample(p,n);}}

namespace render {
std::vector<ClusterVisibility> ForwardPbrRenderer::readClusterVisibility(){auto out=clusterCuller_?clusterCuller_->readVisibility():std::vector<ClusterVisibility>{};for(auto& c:out)c.packet=clusterPacketIndices_.at(c.packet);return out;}
std::vector<float> ForwardPbrRenderer::readClusterHistoryDepth(uint32_t mip){return clusterHistoryValid_&&clusterCuller_->stats().historyUsed?hizCuller_->readDepth(mip):std::vector<float>{};}
}

namespace render {std::vector<float> ForwardPbrRenderer::readDepth(){return resources_.device->readTextureFloat(targets_->depth);}}

namespace render {std::vector<float> ForwardPbrRenderer::readClusterEarlyAlbedo(){return targets_->clusterEarlyAlbedo?resources_.device->readTextureFloat(targets_->clusterEarlyAlbedo):std::vector<float>{};}}

namespace render {
std::vector<float> ForwardPbrRenderer::readClusterEarlyDepth(){return targets_->clusterEarlyDepth?resources_.device->readTextureFloat(targets_->clusterEarlyDepth):std::vector<float>{};}
uint32_t ForwardPbrRenderer::clusterPyramidBaseMip(bool history)const{auto* culler=history?hizCuller_.get():clusterHistory_.get();return culler?culler->pyramidBaseMip():0;}
}
