#include "rhi/Validation.h"
#include "rhi/GraphicsDevice.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

#ifndef SR_RHI_SHADER_DIR
#define SR_RHI_SHADER_DIR "build/rhi/shaders"
#endif

namespace rhi {
namespace {
ShaderAsset shader(const std::string& stage) {
    const auto path = std::string(SR_RHI_SHADER_DIR) + "/textured." + stage;
    return {path + ".glsl", path + ".metallib", path + ".spv", path + ".json", "main0"};
}
void checkPixel(const std::vector<uint8_t>& pixels, unsigned x, unsigned y, std::array<int, 4> expected) {
    for (unsigned c = 0; c < 4; ++c) if (std::abs(int(pixels[(y * 64 + x) * 4 + c]) - expected[c]) > 2)
        throw std::runtime_error("RHI: textured rendering pixel mismatch at " + std::to_string(x) + "," + std::to_string(y));
}
}
void validateTexturedRendering(GraphicsDevice& device) {
    // Single-component attachments, padded half rows, upload and mip/layer copies.
    for(auto format:{Format::R16Float,Format::R32Float}) {
        const auto usage=TextureUsage::ColorAttachment|TextureUsage::Sampled|TextureUsage::CopySource|TextureUsage::CopyDestination;
        auto texture=device.createTexture({7,5,format,usage,"Scalar RHI validation",device.supportsTextureSubresources()?3u:1u});
        auto view=device.createTextureView({texture});auto commands=device.createCommandList();RenderPassDesc pass;
        pass.color=view;pass.clearColor={.375f,.75f,1,1};commands.beginRenderPass(pass);commands.endRenderPass();device.submit(commands);
        const auto values=device.readTextureFloat(texture);
        if(values.size()!=35)throw std::runtime_error("RHI scalar float readback component count differs");
        for(float v:values)if(v!=.375f)throw std::runtime_error("RHI scalar attachment clear or half-row readback differs");
        if(format==Format::R32Float) {const float patch[]={1.25f,2.5f};device.writeTextureRegion(texture,{5,4,2,1},patch,sizeof(patch));
            const auto uploaded=device.readTextureFloat(texture);if(uploaded[33]!=1.25f || uploaded[34]!=2.5f)throw std::runtime_error("RHI scalar float region upload differs");
        } else {const uint16_t patch[]={0x3c00,0x4000,0x4200};device.writeTextureRegion(texture,{4,4,3,1},patch,sizeof(patch));
            const auto uploaded=device.readTextureFloat(texture);if(uploaded[32]!=1.f || uploaded[33]!=2.f || uploaded[34]!=3.f)throw std::runtime_error("RHI scalar half region upload differs");
        }
        if(device.supportsTextureSubresources()) {
            auto mip=device.createTextureView({texture,{1,1,0,1}});auto fill=device.createCommandList();pass.color=mip;pass.clearColor={.625f,0,0,0};fill.beginRenderPass(pass);fill.endRenderPass();device.submit(fill);
            const auto bytes=device.readTextureSubresource(texture,{1,0});
            if(bytes.size()!=6*formatPixelBytes(format))throw std::runtime_error("RHI scalar mip readback stride differs");
            std::vector<float> mipValues;
            if(format==Format::R16Float)mipValues=decodeHalfPixels(bytes);
            else {mipValues.resize(bytes.size()/4);std::memcpy(mipValues.data(),bytes.data(),bytes.size());}
            for(float v:mipValues)if(v!=.625f)throw std::runtime_error("RHI scalar mip attachment clear differs");
            if(format==Format::R32Float) {
                // A one-pixel occluder in the odd trailing corner must survive
                // every reduction; empty/background children must stay zero.
                std::vector<float> pattern(35,0);pattern.front()=10;pattern.back()=1;
                device.writeTextureFloat(texture,pattern.data(),pattern.size()*sizeof(float));
                auto asset=[](const char* name){const auto path=std::string(SR_RHI_SHADER_DIR)+"/"+name;return ShaderAsset{path+".glsl",path+".metallib",path+".spv",path+".json","main0"};};
                GraphicsPipelineDesc desc;desc.vertex=asset("tonemap.vert");desc.fragment=asset("ao-depth-reduce.frag");desc.colorFormat=Format::R32Float;
                desc.vertexStride=16;desc.attributes={{0,VertexFormat::Float2,0},{1,VertexFormat::Float2,8}};
                desc.bindings={{1,{{0,BindingType::SampledTexture,ShaderStage::Fragment,"aoDepthSource",0}}}};
                auto pipeline=device.createGraphicsPipeline(desc);auto sampler=device.createSampler({Filter::Nearest,AddressMode::ClampToEdge});
                const float quad[]={-1,-1,0,1,1,-1,1,1,1,1,1,0,-1,-1,0,1,1,1,1,0,-1,1,0,0};
                auto vertices=device.createBuffer({sizeof(quad),BufferUsage::Vertex,"AO odd-edge fixture"},quad);
                auto last=device.createTextureView({texture,{2,1,0,1}});
                auto firstBindings=device.createBindingSet({desc.bindings[0],{{0,{},0,0,view,sampler}}});
                auto secondBindings=device.createBindingSet({desc.bindings[0],{{0,{},0,0,mip,sampler}}});
                auto reduce=device.createCommandList();pass.color=mip;pass.clearColor={0,0,0,0};
                reduce.beginRenderPass(pass);reduce.bindPipeline(pipeline);reduce.bindBindingSet(firstBindings);reduce.bindVertexBuffer(vertices);reduce.draw(6);reduce.endRenderPass();
                pass.color=last;reduce.beginRenderPass(pass);reduce.bindPipeline(pipeline);reduce.bindBindingSet(secondBindings);reduce.bindVertexBuffer(vertices);reduce.draw(6);reduce.endRenderPass();device.submit(reduce);
                const auto firstBytes=device.readTextureSubresource(texture,{1,0}),lastBytes=device.readTextureSubresource(texture,{2,0});
                float firstValues[6]{},lastValue=0;std::memcpy(firstValues,firstBytes.data(),sizeof(firstValues));std::memcpy(&lastValue,lastBytes.data(),4);
                for(size_t i=0;i<6;++i)if(firstValues[i]!=(i==0?10.f:i==5?1.f:0.f))throw std::runtime_error("AO depth reduction lost odd-edge occluder or treated sky as depth");
                if(lastValue!=1 || device.readTextureFloat(texture)!=pattern)throw std::runtime_error("AO mip feedback/corner reduction corrupted geometry");
                device.destroyBindingSet(firstBindings);device.destroyBindingSet(secondBindings);device.destroyTextureView(last);
                device.destroyBuffer(vertices);device.destroyPipeline(pipeline);device.destroySampler(sampler);
            }
            device.destroyTextureView(mip);
        }
        device.destroyTextureView(view);device.destroyTexture(texture);
    }
    const uint8_t pixels[] = {255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
    auto image = device.createTexture({2, 2, Format::RGBA8UNorm, TextureUsage::Sampled | TextureUsage::CopyDestination, "RHI checker"});
    device.writeTexture(image, pixels, sizeof(pixels));
    auto imageView = device.createTextureView({image});auto sampler = device.createSampler({Filter::Nearest, AddressMode::ClampToEdge});
    auto target = device.createTexture({64, 64, Format::RGBA8UNorm, TextureUsage::ColorAttachment | TextureUsage::CopySource | TextureUsage::Sampled, "RHI triangle"});
    auto targetView = device.createTextureView({target});
    auto depth = device.createTexture({64, 64, Format::Depth32Float, TextureUsage::DepthAttachment, "RHI depth"});auto depthView = device.createTextureView({depth});
    auto copy = device.createTexture({64, 64, Format::RGBA8UNorm, TextureUsage::ColorAttachment | TextureUsage::CopySource, "RHI sampled render target"});auto copyView = device.createTextureView({copy});
    const float tint[] = {.5f, 1.f, .5f, 1.f}, identity[] = {1,1,1,1}, alpha[] = {1,1,1,.5f};
    auto frame = device.createBuffer({sizeof(tint), BufferUsage::Uniform, "RHI tint"}, tint);
    auto copyFrame = device.createBuffer({sizeof(identity), BufferUsage::Uniform, "RHI identity tint"}, identity);
    auto blendFrame = device.createBuffer({sizeof(alpha), BufferUsage::Uniform, "RHI blend tint"}, alpha);
    const float triangle[] = {-.75f,-.75f,.125f,.875f, .75f,-.75f,.875f,.875f, 0,.75f,.5f,.125f};
    const float quad[] = {-1,-1,0,1, 1,-1,1,1, 1,1,1,0, -1,1,0,0};
    const uint16_t indices[] = {0,1,2,0,2,3};
    auto vertices = device.createBuffer({sizeof(triangle), BufferUsage::Vertex, "RHI triangle vertices"}, triangle);
    auto quadVertices = device.createBuffer({sizeof(quad), BufferUsage::Vertex, "RHI fullscreen vertices"}, quad);
    auto indexBuffer = device.createBuffer({sizeof(indices), BufferUsage::Index, "RHI fullscreen indices"}, indices);
    BindingLayout frameLayout{0, {{0, BindingType::UniformBuffer, ShaderStage::Fragment, "Frame", 16}}};
    BindingLayout materialLayout{1, {{1, BindingType::SampledTexture, ShaderStage::Fragment, "albedo", 0}}};
    auto bindings = device.createBindingSet({frameLayout, {{0, frame, 0, 16, {}, {}}}});
    auto identityBindings = device.createBindingSet({frameLayout, {{0, copyFrame, 0, 16, {}, {}}}});
    auto blendBindings = device.createBindingSet({frameLayout, {{0, blendFrame, 0, 16, {}, {}}}});
    auto material = device.createBindingSet({materialLayout, {{1, {}, 0, 0, imageView, sampler}}});
    auto copyMaterial = device.createBindingSet({materialLayout, {{1, {}, 0, 0, targetView, sampler}}});
    GraphicsPipelineDesc desc;
    desc.vertex = shader("vert");desc.fragment = shader("frag");desc.vertexStride = 16;
    desc.attributes = {{0, VertexFormat::Float2, 0}, {1, VertexFormat::Float2, 8}};
    desc.bindings = {frameLayout, materialLayout};desc.depthAttachment = desc.depthTest = desc.depthWrite = true;desc.label = "RHI textured depth pipeline";
    auto pipeline = device.createGraphicsPipeline(desc);
    desc.depthAttachment = desc.depthTest = desc.depthWrite = false;desc.label = "RHI fullscreen pipeline";
    auto copyPipeline = device.createGraphicsPipeline(desc);
    desc.blend = true;desc.label = "RHI alpha blend pipeline";auto blendPipeline = device.createGraphicsPipeline(desc);
    auto commands = device.createCommandList();
    RenderPassDesc pass;pass.color = targetView;pass.depth = depthView;pass.clearColor = {.1f,.2f,.3f,1};
    commands.beginRenderPass(pass);commands.bindPipeline(pipeline);commands.bindBindingSet(bindings);commands.bindBindingSet(material);commands.bindVertexBuffer(vertices);commands.draw(3);
    commands.bindBindingSet(identityBindings);commands.draw(3);commands.endRenderPass();
    pass.color = copyView;pass.depth = {};pass.clearColor = {0,0,0,1};
    commands.beginRenderPass(pass);commands.bindPipeline(copyPipeline);commands.bindBindingSet(identityBindings);commands.bindBindingSet(copyMaterial);
    commands.bindVertexBuffer(quadVertices);commands.bindIndexBuffer(indexBuffer, IndexType::UInt16);commands.drawIndexed(6);commands.endRenderPass();
    device.submit(commands);
    const auto result = device.readTexture(target);const auto copied = device.readTexture(copy);
    checkPixel(result, 0, 0, {26,51,77,255});checkPixel(result, 31, 20, {128,0,0,255});checkPixel(result, 32, 20, {0,255,0,255});
    checkPixel(result, 22, 43, {0,0,128,255});checkPixel(result, 41, 43, {128,255,128,255});
    if (result.size() != copied.size()) throw std::runtime_error("RHI: render target readback size differs");
    for (size_t i = 0; i < result.size(); ++i) if (std::abs(int(result[i]) - int(copied[i])) > 1)
        throw std::runtime_error("RHI: attachment-to-sampled dependency or texture orientation failed");
    auto blend = device.createCommandList();pass.colorLoad = LoadOp::Load;
    blend.beginRenderPass(pass);blend.bindPipeline(blendPipeline);blend.bindBindingSet(blendBindings);blend.bindBindingSet(material);
    blend.bindVertexBuffer(vertices);blend.draw(3);blend.endRenderPass();device.submit(blend);
    const auto blended = device.readTexture(copy);checkPixel(blended, 0, 0, {26,51,77,255});
    checkPixel(blended, 31, 20, {192,0,0,255});checkPixel(blended, 41, 43, {192,255,192,255});
    auto preserve = device.createCommandList();pass.colorLoad = LoadOp::Load;preserve.beginRenderPass(pass);preserve.endRenderPass();device.submit(preserve);
    if (device.readTexture(copy) != blended) throw std::runtime_error("RHI: attachment Load/Store lost pixels");
    const char* name = device.backend() == Backend::Metal ? "metal" : device.backend() == Backend::OpenGL ? "opengl" : "vulkan";
    std::ofstream output(std::string(SR_RHI_SHADER_DIR) + "/../" + name + "-triangle.ppm", std::ios::binary);
    output << "P6\n64 64\n255\n";for (size_t i = 0; i < copied.size(); i += 4) output.write(reinterpret_cast<const char*>(copied.data() + i), 3);
    if (!output) throw std::runtime_error("RHI: failed to save validation image");
    for (auto set : {bindings, identityBindings, blendBindings, material, copyMaterial}) device.destroyBindingSet(set);device.destroyPipeline(pipeline);device.destroyPipeline(copyPipeline);device.destroyPipeline(blendPipeline);
    device.destroySampler(sampler);device.destroyTextureView(imageView);device.destroyTextureView(targetView);device.destroyTextureView(depthView);device.destroyTextureView(copyView);
    device.destroyTexture(image);device.destroyTexture(target);device.destroyTexture(depth);device.destroyTexture(copy);
    for (auto buffer : {frame, copyFrame, blendFrame, vertices, quadVertices, indexBuffer}) device.destroyBuffer(buffer);
    std::cout << "RHI " << name << " textured/indexed rendering, depth, blend, grouped bindings, orientation, sampling dependency and Load/Store passed\n";
}
}
