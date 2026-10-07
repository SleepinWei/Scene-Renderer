#include "renderer/rhi/GpuWaterSunShafts.h"
#include <glm/gtc/matrix_inverse.hpp>
namespace render {
namespace {
rhi::ShaderAsset shaftShader(const std::string& dir,const char* name){const auto p=dir+"/"+name;return {p+".glsl",p+".metallib",p+".spv",p+".json","main0"};}
}
GpuWaterSunShafts::GpuWaterSunShafts(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& dir):resources_(d){
    using namespace rhi;static_assert(sizeof(Parameters)==288,"Sun shaft ABI");
    repeat_=resources_.sampler({Filter::Linear,AddressMode::Repeat});clamp_=resources_.sampler({Filter::Linear,AddressMode::ClampToEdge});nearest_=resources_.sampler({Filter::Nearest,AddressMode::ClampToEdge});
    for(auto* v:{&source_,&direction_})*v=resources_.view(resources_.texture({257,257,Format::RGBA32Float,TextureUsage::Storage|TextureUsage::Sampled,"Water volume solar photons"}));
    accumulation_=resources_.view(resources_.texture({2048,256,Format::RGBA16Float,TextureUsage::ColorAttachment|TextureUsage::Sampled,"Water volume solar flux accumulation"}));
    output_=resources_.texture({2048,256,Format::RGBA16Float,TextureUsage::Storage|TextureUsage::Sampled|TextureUsage::CopySource,"Water sun shaft depth slices"});outputView_=resources_.view(output_);
    rayLayouts_={{0,{{0,BindingType::UniformBuffer,ShaderStage::Compute,"SunShaftParameters",288}}},{1,{{0,BindingType::StorageTextureWrite,ShaderStage::Compute,"shaftSource",0},{1,BindingType::StorageTextureWrite,ShaderStage::Compute,"shaftDirection",0}}}};
    const char* names[]={"shaftLarge","shaftDetail","shaftLargeNormal","shaftDetailNormal","shaftShore","shaftMask"};
    for(uint32_t i=0;i<6;++i)rayLayouts_[0].entries.push_back({i+1,BindingType::SampledTexture,ShaderStage::Compute,names[i],0});
    rays_=resources_.computePipeline({shaftShader(dir,"water-shafts-rays.comp"),rayLayouts_,{8,8,1},"Water volume refracted solar rays"});
    projectionLayout_={0,{{0,BindingType::UniformBuffer,ShaderStage::Vertex,"SunShaftParameters",288},{1,BindingType::SampledTexture,ShaderStage::Vertex,"shaftSources",0},{2,BindingType::SampledTexture,ShaderStage::Vertex,"shaftDirections",0}}};
    GraphicsPipelineDesc p;p.vertex=shaftShader(dir,"water-shafts-project.vert");p.fragment=shaftShader(dir,"water-caustics-project.frag");p.vertexStride=16;p.attributes={{0,VertexFormat::Float4,0}};p.bindings={projectionLayout_};p.colorFormat=Format::RGBA16Float;p.blend=p.additiveBlend=true;p.label="Water volume depth-slice solar flux";project_=resources_.pipeline(p);
    filterLayouts_={{0,{{0,BindingType::SampledTexture,ShaderStage::Compute,"shaftAccumulation",0}}},{1,{{0,BindingType::StorageTextureWrite,ShaderStage::Compute,"shaftOutput",0,Format::RGBA16Float}}}};
    filter_=resources_.computePipeline({shaftShader(dir,"water-shafts-filter.comp"),filterLayouts_,{8,8,1},"Water sun shaft footprint filter"});
    std::vector<glm::vec4> vertices;vertices.reserve(256*256*6);auto triangle=[&](int a,int b,int c){for(int k=0;k<3;++k)vertices.push_back({float(a),float(b),float(c),float(k)});};
    for(int y=0;y<256;++y)for(int x=0;x<256;++x){int a=y*257+x;triangle(a,a+257,a+1);triangle(a+1,a+257,a+258);}
    vertices_=resources_.buffer({vertices.size()*16,BufferUsage::Vertex,"Volume photon source topology"},vertices.data());
}
void GpuWaterSunShafts::record(Resources& frame,rhi::CommandList& c,const OceanSurfaceSettings& s,glm::vec3 camera,glm::vec3 L,
    const std::array<rhi::TextureViewHandle,6>& fft,rhi::TextureViewHandle shore,glm::vec4 shorePatch,rhi::TextureViewHandle mask){
    using namespace rhi;auto ray=glm::refract(-L,glm::vec3(0,1,0),1.f/1.333f);projection_={0,0,s.seaLevel,1};
    if(ray.y<-.05f){projection_.x=ray.x/ray.y;projection_.y=ray.z/ray.y;}
    constexpr float length=48,margin=16,cell=(length+margin*2)/256;
    auto coordinate=glm::vec2(camera.x,camera.z)-glm::vec2(projection_)*(camera.y-s.seaLevel);auto center=glm::floor(coordinate/cell)*cell;
    patch_={center.x-length*.5f,center.y-length*.5f,length,length/256};
    Parameters p{glm::inverse(s.bathymetryModel),s.bathymetryModel,shorePatch,shorePatch,{0,0,s.shore.enabled?1.f:0.f,0},{0,0,0,0},patch_,{s.seaLevel,s.spectrum.length,32,s.detailWaves?1.f:0.f},glm::vec4(L,0),projection_,{(s.surfaceLength>0?s.surfaceLength:s.spectrum.length)*.5f,margin,cell,0},{257,256,0,0}};
    auto buffer=frame.buffer({sizeof(p),BufferUsage::Uniform,"Sun shaft photon settings"},&p);
    const TextureViewHandle textures[]={fft[0],fft[3],fft[1],fft[4],shore,mask};std::vector<BindingEntry> entries={{0,buffer,0,sizeof(p),{},{}}};
    for(uint32_t i=0;i<6;++i)entries.push_back({i+1,{},0,0,textures[i],i<4?repeat_:clamp_});
    c.setLabel("water/sun-shaft-rays");c.dispatch(rays_,{frame.bindings({rayLayouts_[0],entries}),frame.bindings({rayLayouts_[1],{{0,{},0,0,source_,{}},{1,{},0,0,direction_,{}}}})},{33,33,1});
    for(uint32_t slice=0;slice<8;++slice){p.grid.z=int(slice);buffer=frame.buffer({sizeof(p),BufferUsage::Uniform,"Sun shaft depth plane"},&p);
        auto binding=frame.bindings({projectionLayout_,{{0,buffer,0,sizeof(p),{},{}},{1,{},0,0,source_,nearest_},{2,{},0,0,direction_,nearest_}}});
        RenderPassDesc pass;pass.color=accumulation_;pass.clearColor={0,0,0,0};pass.viewport={slice*256,0,256,256};if(slice)pass.colorLoad=LoadOp::Load;
        c.setLabel("water/sun-shaft-slice");c.beginRenderPass(pass);c.bindPipeline(project_);c.bindBindingSet(binding);c.bindVertexBuffer(vertices_);c.draw(256*256*6);c.endRenderPass();}
    c.setLabel("water/sun-shaft-filter");c.dispatch(filter_,{frame.bindings({filterLayouts_[0],{{0,{},0,0,accumulation_,nearest_}}}),frame.bindings({filterLayouts_[1],{{0,{},0,0,outputView_,{}}}})},{256,32,1});
}
}
