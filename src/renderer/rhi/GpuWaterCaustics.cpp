#include "renderer/rhi/GpuWaterCaustics.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <cmath>
namespace render {
namespace {
rhi::ShaderAsset shader(const std::string& dir,const char* name){const auto p=dir+"/"+name;return {p+".glsl",p+".metallib",p+".spv",p+".json","main0"};}
// Orthographic shear: points on the same refracted, flat-surface solar ray
// share an XY texel. Increasing depth follows that ray down into the water.
glm::mat4 receiverMatrix(glm::vec4 patch,glm::vec4 projection){
    glm::mat4 m(0);const float scale=2.f/patch.z;
    m[0][0]=scale;m[1][0]=-scale*projection.x;m[3][0]=scale*(projection.x*projection.z-patch.x)-1;
    m[2][1]=-scale;m[1][1]=scale*projection.y;m[3][1]=1-scale*(projection.y*projection.z-patch.y);
    m[1][2]=-1.f/132;m[3][2]=(projection.z+4)/132;m[3][3]=1;return m;
}
}
GpuWaterCaustics::GpuWaterCaustics(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& dir):resources_(d){
    using namespace rhi;static_assert(sizeof(Parameters)==320,"Caustic ABI");
    repeat_=resources_.sampler({Filter::Linear,AddressMode::Repeat});clamp_=resources_.sampler({Filter::Linear,AddressMode::ClampToEdge});nearest_=resources_.sampler({Filter::Nearest,AddressMode::ClampToEdge});
    for(auto* v:{&hit_,&flux_})*v=resources_.view(resources_.texture({513,513,Format::RGBA32Float,TextureUsage::Storage|TextureUsage::Sampled,"Caustic refracted photons"}));
    accumulated_=resources_.view(resources_.texture({1536,512,Format::RGBA16Float,TextureUsage::ColorAttachment|TextureUsage::Sampled,"Cascaded caustic receiver irradiance"}));
    output_=resources_.texture({1536,512,Format::RGBA16Float,TextureUsage::Storage|TextureUsage::Sampled|TextureUsage::CopySource,"Cascaded caustic relative irradiance and receiver height"});outputView_=resources_.view(output_);
    receiverPosition_=resources_.view(resources_.texture({1536,512,Format::RGBA32Float,TextureUsage::ColorAttachment|TextureUsage::Sampled,"Caustic sun-aligned receiver positions"}));
    receiverNormal_=resources_.view(resources_.texture({1536,512,Format::RGBA16Float,TextureUsage::ColorAttachment|TextureUsage::Sampled,"Caustic sun-aligned receiver normals"}));
    receiverDepth_=resources_.view(resources_.texture({1536,512,Format::Depth32Float,TextureUsage::DepthAttachment,"Caustic receiver depth"}));
    receiverLayout_={0,{{0,BindingType::UniformBuffer,ShaderStage::Vertex,"CameraVertex",64},{1,BindingType::UniformBuffer,ShaderStage::Vertex,"ObjectData",128},{2,BindingType::UniformBuffer,ShaderStage::Fragment,"CausticReceiverSettings",16}}};
    GraphicsPipelineDesc p;p.vertex=shader(dir,"forward.vert");p.fragment=shader(dir,"water-caustics-receiver.frag");p.vertexStride=sizeof(MeshVertex);p.attributes=GpuMesh::attributes();
    p.bindings={receiverLayout_,GpuMaterial::shadowLayout()};p.colorFormat=Format::RGBA32Float;p.additionalColorFormats={Format::RGBA16Float};p.depthAttachment=p.depthTest=p.depthWrite=true;p.label="Submerged caustic receiver capture";receiver_=resources_.pipeline(p);
    if(d->computeLimits().supported){p.vertex=shader(dir,"instanced.vert");p.bindings[0].entries.push_back({3,BindingType::StorageRead,ShaderStage::Vertex,"OutPose",64});receiverInstanced_=resources_.pipeline(p);}
    rayLayouts_={{0,{{0,BindingType::UniformBuffer,ShaderStage::Compute,"CausticParameters",320}}},{1,{{0,BindingType::StorageTextureWrite,ShaderStage::Compute,"causticHit",0},{1,BindingType::StorageTextureWrite,ShaderStage::Compute,"causticFlux",0},{2,BindingType::SampledTexture,ShaderStage::Compute,"causticReceiverPosition",0},{3,BindingType::SampledTexture,ShaderStage::Compute,"causticReceiverNormal",0}}}};
    const char* names[]={"causticBedMap","causticLarge","causticDetail","causticLargeNormal","causticDetailNormal","causticShore","causticMask"};
    for(uint32_t i=0;i<7;++i)rayLayouts_[0].entries.push_back({i+1,BindingType::SampledTexture,ShaderStage::Compute,names[i],0});
    rays_=resources_.computePipeline({shader(dir,"water-caustics-rays.comp"),rayLayouts_,{8,8,1},"Water caustic ray projection"});
    projectionLayout_={0,{{0,BindingType::UniformBuffer,ShaderStage::Vertex,"CausticParameters",320},{1,BindingType::SampledTexture,ShaderStage::Vertex,"causticHits",0},{2,BindingType::SampledTexture,ShaderStage::Vertex,"causticFluxes",0}}};
    p={};p.vertex=shader(dir,"water-caustics-project.vert");p.fragment=shader(dir,"water-caustics-project.frag");p.vertexStride=16;p.attributes={{0,VertexFormat::Float4,0}};p.bindings={projectionLayout_};p.colorFormat=Format::RGBA16Float;p.blend=p.additiveBlend=true;p.label="Water caustic flux accumulation";project_=resources_.pipeline(p);
    normalizeLayouts_={{0,{{0,BindingType::UniformBuffer,ShaderStage::Compute,"CausticParameters",320},{1,BindingType::SampledTexture,ShaderStage::Compute,"causticBedMap",0},{2,BindingType::SampledTexture,ShaderStage::Compute,"causticAccumulation",0},{3,BindingType::SampledTexture,ShaderStage::Compute,"causticLarge",0},{4,BindingType::SampledTexture,ShaderStage::Compute,"causticShore",0},{5,BindingType::SampledTexture,ShaderStage::Compute,"causticReceiverPosition",0},{6,BindingType::SampledTexture,ShaderStage::Compute,"causticReceiverNormal",0}}},
        {1,{{0,BindingType::StorageTextureWrite,ShaderStage::Compute,"causticOutput",0,Format::RGBA16Float}}}};
    normalize_=resources_.computePipeline({shader(dir,"water-caustics-normalize.comp"),normalizeLayouts_,{8,8,1},"Water caustic footprint filter and normalization"});
    for(uint32_t mesh=0;mesh<2;++mesh){const uint32_t cells=mesh?512:256,n=cells+1;std::vector<glm::vec4> vertices;vertices.reserve(cells*cells*6);
        auto triangle=[&](uint32_t a,uint32_t b,uint32_t c){for(int corner=0;corner<3;++corner)vertices.push_back({float(a),float(b),float(c),float(corner)});};
        for(uint32_t y=0;y<cells;++y)for(uint32_t x=0;x<cells;++x){auto a=y*n+x;triangle(a,a+n,a+1);triangle(a+1,a+n,a+n+1);}
        vertices_[mesh]=resources_.buffer({vertices.size()*16,BufferUsage::Vertex,"Caustic source grid triangle topology"},vertices.data());}
}
std::vector<float> GpuWaterCaustics::read(uint32_t cascade)const{
    if(cascade>=3)throw std::out_of_range("Caustic cascade index");auto atlas=resources_.device->readTextureFloat(output_);std::vector<float> result(512*512*4);
    for(uint32_t y=0;y<512;++y)std::copy_n(atlas.data()+4*(y*1536+cascade*512),512*4,result.data()+y*512*4);return result;
}
void GpuWaterCaustics::record(Resources& frame,rhi::CommandList& commands,const OceanSurfaceSettings& s,glm::vec3 camera,glm::vec3 L,
    const std::array<rhi::TextureViewHandle,6>& fft,rhi::TextureViewHandle bed,rhi::TextureViewHandle shore,glm::vec4 shorePatch,rhi::TextureViewHandle mask,const std::vector<DrawPacket>& packets){
    using namespace rhi;const bool receivers=s.causticMeshReceivers&&!packets.empty();cascadeCount_=s.causticCascades?3:1;
    auto ray=glm::refract(-L,glm::vec3(0,1,0),1.f/1.333f);projection_={0,0,s.seaLevel,receivers?1.f:0.f};
    if(receivers&&ray.y<-.05f){projection_.x=ray.x/ray.y;projection_.y=ray.z/ray.y;}
    constexpr float lengths[]={16,48,128},margins[]={4,4,8};constexpr uint32_t grids[]={257,513,513};
    const char* receiverLabels[]={"water/caustic-receivers-near","water/caustic-receivers-mid","water/caustic-receivers-far"};
    for(uint32_t cascade=0;cascade<cascadeCount_;++cascade){const float length=lengths[cascade],sourceCell=(length+2*margins[cascade])/(grids[cascade]-1);
        // Snap each source lattice independently; blending happens in world space.
        const auto cameraCoordinate=glm::vec2(camera.x,camera.z)-glm::vec2(projection_)*(camera.y-s.seaLevel);
        auto centre=glm::floor(cameraCoordinate/sourceCell)*sourceCell;patches_[cascade]={centre.x-length*.5f,centre.y-length*.5f,length,length/512};
        const auto patch=patches_[cascade];
        if(receivers){auto vp=receiverMatrix(patch,projection_);auto view=frame.buffer({64,BufferUsage::Uniform,"Caustic receiver projection"},&vp);auto settings=frame.buffer({16,BufferUsage::Uniform,"Caustic receiver medium"},&projection_);
            RenderPassDesc pass;pass.color=receiverPosition_;pass.additionalColors={{receiverNormal_}};pass.depth=receiverDepth_;pass.clearColor={0,0,0,0};pass.viewport={cascade*512,0,512,512};
            if(cascade){pass.colorLoad=pass.depthLoad=LoadOp::Load;pass.additionalColors[0].load=LoadOp::Load;}
            commands.setLabel(receiverLabels[cascade]);commands.beginRenderPass(pass);
            for(const auto& packet:packets){if(packet.material->transparent())continue;
                const std::array<glm::mat4,2> object={packet.model,glm::transpose(glm::inverse(packet.model))};auto model=frame.buffer({128,BufferUsage::Uniform,"Caustic receiver object"},object.data());
                auto layout=receiverLayout_;std::vector<BindingEntry> bindings={{0,view,0,64,{},{}},{1,model,0,128,{},{}},{2,settings,0,16,{},{}}};
                if(packet.mesh->instances()){layout.entries.push_back({3,BindingType::StorageRead,ShaderStage::Vertex,"OutPose",64});bindings.push_back({3,packet.mesh->instances(),0,size_t(packet.mesh->instanceCapacity())*64,{},{}});commands.bindPipeline(receiverInstanced_);}else commands.bindPipeline(receiver_);
                commands.bindBindingSet(frame.bindings({layout,bindings}));packet.material->bindShadow(commands);packet.mesh->draw(commands);}
            commands.endRenderPass();}
        Parameters p{glm::inverse(s.bathymetryModel),s.bathymetryModel,shorePatch,shorePatch,{0,0,s.shore.enabled?1.f:0.f,0},{1,0,0,0},patch,
            {s.seaLevel,s.spectrum.length,32,s.detailWaves?1.f:0.f},glm::vec4(L,0),glm::vec4(s.absorption+s.scattering,0),{64,(s.surfaceLength>0?s.surfaceLength:s.spectrum.length)*.5f,0,0},projection_,{margins[cascade],sourceCell,0,0},{int(grids[cascade]),512,int(cascade),0}};
        auto buffer=frame.buffer({sizeof(p),BufferUsage::Uniform,"Water caustic parameters"},&p);
        const TextureViewHandle textures[]={bed,fft[0],fft[3],fft[1],fft[4],shore,mask};
        std::vector<BindingEntry> entries={{0,buffer,0,sizeof(p),{},{}}};for(uint32_t i=0;i<7;++i)entries.push_back({i+1,{},0,0,textures[i],i>=1&&i<=4?repeat_:clamp_});
        auto a=frame.bindings({rayLayouts_[0],entries}),b=frame.bindings({rayLayouts_[1],{{0,{},0,0,hit_,{}},{1,{},0,0,flux_,{}},{2,{},0,0,receiverPosition_,nearest_},{3,{},0,0,receiverNormal_,nearest_}}});
        commands.setLabel("water/caustic-rays");commands.dispatch(rays_,{a,b},{(grids[cascade]+7)/8,(grids[cascade]+7)/8,1});
        auto projection=frame.bindings({projectionLayout_,{{0,buffer,0,sizeof(p),{},{}},{1,{},0,0,hit_,nearest_},{2,{},0,0,flux_,nearest_}}});
        RenderPassDesc pass;pass.color=accumulated_;pass.clearColor={0,0,0,0};pass.viewport={cascade*512,0,512,512};if(cascade)pass.colorLoad=LoadOp::Load;
        commands.setLabel("water/caustic-mesh");commands.beginRenderPass(pass);commands.bindPipeline(project_);commands.bindBindingSet(projection);commands.bindVertexBuffer(vertices_[cascade?1:0]);commands.draw((grids[cascade]-1)*(grids[cascade]-1)*6);commands.endRenderPass();
        auto n=frame.bindings({normalizeLayouts_[0],{{0,buffer,0,sizeof(p),{},{}},{1,{},0,0,bed,clamp_},{2,{},0,0,accumulated_,nearest_},{3,{},0,0,fft[0],repeat_},{4,{},0,0,shore,clamp_},{5,{},0,0,receiverPosition_,nearest_},{6,{},0,0,receiverNormal_,nearest_}}});
        auto o=frame.bindings({normalizeLayouts_[1],{{0,{},0,0,outputView_,{}}}});commands.setLabel("water/caustic-filter");commands.dispatch(normalize_,{n,o},{64,64,1});
    }
}
}
