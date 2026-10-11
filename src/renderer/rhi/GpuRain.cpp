#include "renderer/rhi/GpuRain.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "rhi/ShaderAssets.h"
#include <json/json.hpp>
#include <algorithm>
#include <cmath>
namespace render {
namespace {
struct alignas(16) Parameters {glm::mat4 vp;glm::vec4 cameraTime,windRate,screen,colour;};
static_assert(sizeof(Parameters)==128,"Rain uniform ABI");
rhi::ShaderAsset asset(const std::string& dir,const std::string& name){auto p=dir+"/"+name;return {p+".glsl",p+".metallib",p+".spv",p+".json","main0"};}
}
GpuRain::GpuRain(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory):resources_(device){
    using namespace rhi;
    GraphicsPipelineDesc p;p.vertex=asset(directory,"rain.vert");p.fragment=asset(directory,"rain.frag");
    for(const auto& shader:{p.vertex,p.fragment}){
        auto reflection=nlohmann::json::parse(readShaderText(shader.reflectionPath));
        if(reflection.at("ubos").at(0).at("block_size")!=sizeof(Parameters))throw std::invalid_argument("Rain reflected ABI differs");
    }
    layout_={0,{{0,BindingType::UniformBuffer,ShaderStage::Vertex,"RainParameters",sizeof(Parameters)},
                {2,BindingType::UniformBuffer,ShaderStage::Fragment,"RainParameters",sizeof(Parameters)},
                {1,BindingType::SampledTexture,ShaderStage::Fragment,"rainSceneDepth",0}}};
    p.bindings={layout_};p.vertexStride=8;p.attributes={{0,VertexFormat::Float2,0}};p.colorFormat=Format::RGBA16Float;p.blend=true;p.label="Analytic world-space rain";
    pipeline_=resources_.pipeline(p);sampler_=resources_.sampler({Filter::Nearest,AddressMode::ClampToEdge});
    std::vector<glm::vec2> vertices;vertices.reserve(4096*6);
    for(int i=0;i<4096;++i)for(auto c:std::array<glm::vec2,6>{{{-1,0},{1,0},{1,1},{-1,0},{1,1},{-1,1}}})vertices.push_back(c);
    vertices_=resources_.buffer({vertices.size()*sizeof(glm::vec2),BufferUsage::Vertex,"Rain streak corners"},vertices.data());
}
void GpuRain::record(Resources& transient,rhi::CommandList& commands,const FrameData& f,rhi::TextureViewHandle hdr,rhi::TextureViewHandle depth){
    using namespace rhi;
    if(!std::isfinite(f.rainMetersPerSecond)||f.rainMetersPerSecond<0||f.rainMetersPerSecond>float(1000./3600000)||
       !std::isfinite(f.rainWind.x)||!std::isfinite(f.rainWind.y))throw std::invalid_argument("Invalid rain input");
    if(!f.rainVisible||f.rainMetersPerSecond==0)return;
    for(const auto& ocean:f.oceans)if(ocean.underwaterView&&f.cameraPosition.y<ocean.seaLevel)return;
    const float intensity=std::clamp(f.rainMetersPerSecond*3600000/50.f,0.f,1.f);
    const uint32_t drops=std::min(4096u,std::max(1u,uint32_t(std::ceil(intensity*4096))));
    Parameters params{f.viewProjection,glm::vec4(f.cameraPosition,float(std::fmod(double(f.timeSeconds)*9,24.)/9)),
        glm::vec4(f.rainWind,f.rainMetersPerSecond,intensity),{float(f.viewportWidth),float(f.viewportHeight),.65f,float(std::fmod(double(f.timeSeconds)*f.rainWind.x*.2,40.))},{.6f,.72f,.9f,float(std::fmod(double(f.timeSeconds)*f.rainWind.y*.2,40.))}};
    auto buffer=transient.buffer({sizeof(params),BufferUsage::Uniform,"Rain frame parameters"},&params);
    auto bindings=transient.bindings({layout_,{{0,buffer,0,sizeof(params),{},{}},{2,buffer,0,sizeof(params),{},{}},{1,{},0,0,depth,sampler_}}});
    RenderPassDesc pass;pass.color=hdr;pass.colorLoad=LoadOp::Load;commands.beginRenderPass(pass);
    commands.bindPipeline(pipeline_);commands.bindBindingSet(bindings);commands.bindVertexBuffer(vertices_);commands.draw(drops*6);commands.endRenderPass();
}
}
