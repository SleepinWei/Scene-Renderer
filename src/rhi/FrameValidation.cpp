#include "rhi/Validation.h"
#include "rhi/GraphicsDevice.h"
#include "rhi/ShaderAssets.h"
#include "renderer/rhi/Resources.h"
#include <iostream>
#include <cmath>
namespace rhi {
void validateFrameLifecycle(std::shared_ptr<GraphicsDevice> device){
    if(device->backend()==Backend::OpenGL){std::cout<<"RHI multi-frame validation deferred on OpenGL compatibility backend\n";return;}
    const auto extent=device->presentationExtent();const uint32_t width=extent[0]?extent[0]:32,height=extent[1]?extent[1]:32;
    using render::Resources;Resources persistent(device);auto asset=[](const char* name){auto path=defaultShaderDirectory()+"/"+name;return ShaderAsset{path+".glsl",path+".metallib",path+".spv",path+".json","main0"};};
    auto image=persistent.texture({1,1,Format::RGBA8UNorm,TextureUsage::Sampled|TextureUsage::CopyDestination,"Frame white texture"});const uint8_t white[]={255,255,255,255};device->writeTexture(image,white,4);auto imageView=persistent.view(image);auto sampler=persistent.sampler({});auto target=persistent.texture({width,height,Format::RGBA8UNorm,TextureUsage::ColorAttachment|TextureUsage::CopySource|TextureUsage::Sampled,"Frame reuse target"});auto targetView=persistent.view(target);
    BindingLayout frameLayout{0,{{0,BindingType::UniformBuffer,ShaderStage::Fragment,"Frame",16}}},materialLayout{1,{{1,BindingType::SampledTexture,ShaderStage::Fragment,"albedo",0}}};auto material=persistent.bindings({materialLayout,{{1,{},0,0,imageView,sampler}}});auto data=persistent.buffer({16,BufferUsage::Uniform|BufferUsage::CopyDestination,"Reused frame uniform"});auto frameBindings=persistent.bindings({frameLayout,{{0,data,0,16,{},{}}}});
    GraphicsPipelineDesc p;p.vertex=asset("textured.vert");p.fragment=asset("textured.frag");p.vertexStride=16;p.attributes={{0,VertexFormat::Float2,0},{1,VertexFormat::Float2,8}};p.bindings={frameLayout,materialLayout};auto pipeline=persistent.pipeline(p);
    const float quad[]={-1,-1,0,1,1,-1,1,1,1,1,1,0,-1,-1,0,1,1,1,1,0,-1,1,0,0};std::vector<ReadbackTicket> tickets;CompletionToken last;
    for(int i=0;i<20;++i){device->beginFrame();const float tint[]={float(i+1)/20,float(20-i)/20,.25f,1};device->writeBuffer(data,0,16,tint);
        {Resources temporary(device);auto vertices=temporary.buffer({sizeof(quad),BufferUsage::Vertex,"Retired frame vertices"},quad);auto commands=device->createCommandList();RenderPassDesc pass;pass.color=targetView;commands.beginRenderPass(pass);commands.bindPipeline(pipeline);commands.bindVertexBuffer(vertices);commands.bindBindingSet(frameBindings);commands.bindBindingSet(material);commands.draw(6);commands.endRenderPass();device->submit(commands);}
        tickets.push_back(device->requestTextureReadback(target));if(device->supportsPresentation()){device->copyToBackbuffer(target);device->present();last=tickets.back().completion;}else last=device->endFrame();if(device->framesInFlight()>3)throw std::runtime_error("RHI exceeded bounded in-flight frame contexts");
    }
    device->wait(last);device->waitIdle();for(size_t i=0;i<tickets.size();++i){if(!device->isComplete(tickets[i].completion) || tickets[i].bytes->size()!=size_t(width)*height*4)throw std::runtime_error("RHI readback completion missing");const auto& pixels=*tickets[i].bytes;const int expected[]={int(std::lround(float(i+1)*255/20)),int(std::lround(float(20-i)*255/20)),64,255};for(size_t at=0;at<pixels.size();++at)if(std::abs(int(pixels[at])-expected[at%4])>1)throw std::runtime_error("RHI asynchronous upload/readback reused in-flight data");}
    std::cout<<"RHI 20 frames, bounded completion ring, ordered staging updates, async readbacks and deferred transient resource release passed\n";
}
}
