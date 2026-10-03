#include "rhi/Validation.h"
#include "rhi/ShaderAssets.h"
#include "renderer/rhi/Resources.h"
#include <array>
#include <cstring>
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace rhi {
namespace {
void check(bool value, const char* reason) { if(!value) throw std::runtime_error(reason); }
ShaderAsset asset(const std::string& name) {
    const auto p=defaultShaderDirectory()+"/"+name;return {p+".glsl",p+".metallib",p+".spv",p+".json","main0"};
}
}
void validateComputeAndIndirect(GraphicsDevice& device) {
    render::Resources resources(device.shared_from_this());
    const float triangle[]={-.75f,-.75f,0,1, .75f,-.75f,1,1, 0,.75f,.5f,0};
    const DrawIndirectArguments arguments{3,1,0,0};const DrawIndexedIndirectArguments indexedArguments{3,1,0,0,0};
    auto vertices=resources.buffer({sizeof(triangle),BufferUsage::Vertex|BufferUsage::Storage|BufferUsage::CopySource|BufferUsage::CopyDestination,"Generated vertices"},triangle);
    auto indirect=resources.buffer({sizeof(arguments),BufferUsage::Indirect|BufferUsage::Storage|BufferUsage::CopySource|BufferUsage::CopyDestination,"Generated draw arguments"},&arguments);
    auto indexedIndirect=resources.buffer({sizeof(indexedArguments),BufferUsage::Indirect,"Indexed draw arguments"},&indexedArguments);
    const uint16_t indexValues[]={0,1,2};auto indices=resources.buffer({sizeof(indexValues),BufferUsage::Index,"Triangle indices"},indexValues);
    const uint8_t red[]={255,0,0,255};auto image=resources.texture({1,1,Format::RGBA8UNorm,TextureUsage::Sampled|TextureUsage::CopyDestination,"Red"});device.writeTexture(image,red,4);
    auto view=resources.view(image);auto sampler=resources.sampler({});
    const float white[]={1,1,1,1};auto uniform=resources.buffer({16,BufferUsage::Uniform,"Tint"},white);
    BindingLayout frame{0,{{0,BindingType::UniformBuffer,ShaderStage::Fragment,"Frame",16}}}, material{1,{{1,BindingType::SampledTexture,ShaderStage::Fragment,"albedo",0}}};
    auto frameSet=resources.bindings({frame,{{0,uniform,0,16,{},{}}}}), materialSet=resources.bindings({material,{{1,{},0,0,view,sampler}}});
    GraphicsPipelineDesc p;p.vertex=asset("textured.vert");p.fragment=asset("textured.frag");p.vertexStride=16;p.attributes={{0,VertexFormat::Float2,0},{1,VertexFormat::Float2,8}};p.bindings={frame,material};
    auto pipeline=resources.pipeline(p);
    auto output=resources.texture({64,64,Format::RGBA8UNorm,TextureUsage::ColorAttachment|TextureUsage::CopySource,"Indirect output"});auto outputView=resources.view(output);
    auto draw = [&](CommandList& list, bool indexed) {
        RenderPassDesc pass;pass.color=outputView;list.beginRenderPass(pass);list.bindPipeline(pipeline);list.bindBindingSet(frameSet);list.bindBindingSet(materialSet);list.bindVertexBuffer(vertices);
        if(indexed) { list.bindIndexBuffer(indices,IndexType::UInt16);list.drawIndexedIndirect(indexedIndirect); } else list.drawIndirect(indirect);
        list.endRenderPass();
    };
    auto verifyPixels = [&] {
        const auto pixels=device.readTexture(output);const size_t center=(32*64+32)*4;
        check(pixels[center]==255 && pixels[center+1]==0 && pixels[center+2]==0 && pixels[0]==0,"Indirect triangle pixels failed");return pixels;
    };
    auto cpu=device.createCommandList();draw(cpu,false);device.submit(cpu);const auto expected=verifyPixels();
    auto indexed=device.createCommandList();draw(indexed,true);device.submit(indexed);check(verifyPixels()==expected,"Indexed indirect differs from non-indexed indirect");
    if(!device.computeLimits().supported) {
        try { device.createComputePipeline({});throw std::runtime_error("Unsupported compute was accepted"); } catch(const std::invalid_argument&) {}
        std::cout << "RHI CPU indirect/indexed indirect passed; compute unavailable on this context\n";return;
    }
    auto seeds=resources.buffer({256,BufferUsage::Storage|BufferUsage::CopySource,"Compute seeds"});
    ComputePipelineDesc seed;seed.shader=asset("seed.comp");seed.threads={16,1,1};seed.bindings={{0,{{0,BindingType::StorageWrite,ShaderStage::Compute,"Seeds",256}}}};
    auto seedPipeline=resources.computePipeline(seed);auto seedSet=resources.bindings({seed.bindings[0],{{0,seeds,0,256,{},{}}}});
    ComputePipelineDesc generate;generate.shader=asset("generate.comp");generate.bindings={{0,{{0,BindingType::StorageRead,ShaderStage::Compute,"Seeds",256},{1,BindingType::StorageWrite,ShaderStage::Compute,"Vertices",48},{2,BindingType::StorageWrite,ShaderStage::Compute,"Arguments",16}}}};
    auto generatePipeline=resources.computePipeline(generate);auto generateSet=resources.bindings({generate.bindings[0],{{0,seeds,0,256,{},{}},{1,vertices,0,48,{},{}},{2,indirect,0,16,{},{}}}});
    auto invalid=seed;invalid.threads={8,1,1};try { resources.computePipeline(invalid);throw std::runtime_error("Mismatched compute workgroup accepted"); } catch(const std::invalid_argument&) {}
    const std::array<float,12> zeroVertices{};const DrawIndirectArguments zeroArguments{};
    device.writeBuffer(vertices,0,sizeof(zeroVertices),zeroVertices.data());device.writeBuffer(indirect,0,sizeof(zeroArguments),&zeroArguments);
    auto generated=device.createCommandList();generated.dispatch(seedPipeline,{seedSet},{4,1,1});generated.dispatch(generatePipeline,{generateSet},{1,1,1});draw(generated,false);device.submit(generated);
    check(verifyPixels()==expected,"Compute-generated indirect output differs from CPU draw");
    std::array<uint32_t,64> values{};device.readBuffer(seeds,0,sizeof(values),values.data());for(size_t i=0;i<64;++i)check(values[i]==i*3+1,"Compute -> compute seed dependency failed");
    DrawIndirectArguments read{};device.readBuffer(indirect,0,sizeof(read),&read);check(std::memcmp(&read,&arguments,sizeof(read))==0,"Compute-generated indirect arguments failed");
    std::array<float,12> gpuVertices{};device.readBuffer(vertices,0,sizeof(gpuVertices),gpuVertices.data());check(std::memcmp(gpuVertices.data(),triangle,sizeof(triangle))==0,"Compute-generated vertex buffer failed");
    // A second submission verifies persistence and the cross-submission dependency boundary.
    auto next=device.createCommandList();next.dispatch(generatePipeline,{generateSet},{1,1,1});draw(next,false);device.submit(next);check(verifyPixels()==expected,"Cross-submission compute dependency failed");
    std::cout << "RHI compute -> compute -> vertex/indirect draw, readback and cross-submission dependencies passed\n";
    // Three radix-2 butterfly passes exercise ping-pong read/write dependencies.
    const std::array<float,16> input{{1,.25f,2,-.5f,3,.75f,4,-1,5,1.25f,6,-1.5f,7,1.75f,8,-2}};
    auto fftInput=resources.buffer({sizeof(input),BufferUsage::Storage,"FFT input"},input.data());
    auto ping=resources.buffer({sizeof(input),BufferUsage::Storage|BufferUsage::CopySource,"FFT ping"});
    auto pong=resources.buffer({sizeof(input),BufferUsage::Storage,"FFT pong"});
    ComputePipelineDesc fft;fft.shader=asset("fft.comp");fft.threads={8,1,1};
    fft.bindings={{0,{{0,BindingType::StorageRead,ShaderStage::Compute,"InputData",64},{1,BindingType::StorageWrite,ShaderStage::Compute,"OutputData",64},{2,BindingType::UniformBuffer,ShaderStage::Compute,"FFTParameters",16}}}};
    auto fftPipeline=resources.computePipeline(fft);auto butterflies=device.createCommandList();
    for(int stage=0;stage<3;++stage) {
        const std::array<int32_t,4> parameters{{stage,0,0,0}};auto data=resources.buffer({16,BufferUsage::Uniform,"FFT stage"},parameters.data());
        auto source=stage==0?fftInput:stage==1?ping:pong, destination=stage==1?pong:ping;
        auto bindings=resources.bindings({fft.bindings[0],{{0,source,0,64,{},{}},{1,destination,0,64,{},{}},{2,data,0,16,{},{}}}});
        butterflies.dispatch(fftPipeline,{bindings},{1,1,1});
    }
    device.submit(butterflies);std::array<float,16> transformed{};device.readBuffer(ping,0,sizeof(transformed),transformed.data());
    for(int k=0;k<8;++k) {
        double real=0,imaginary=0;
        for(int n=0;n<8;++n) { const double angle=-6.283185307179586*k*n/8;real+=input[n*2]*std::cos(angle)-input[n*2+1]*std::sin(angle);imaginary+=input[n*2]*std::sin(angle)+input[n*2+1]*std::cos(angle); }
        check(std::abs(transformed[k*2]-real)<.0001 && std::abs(transformed[k*2+1]-imaginary)<.0001,"GPU FFT differs from CPU DFT reference");
    }
    std::cout << "RHI 8-point complex FFT with three ping-pong dispatches passed CPU DFT comparison\n";
}
}
