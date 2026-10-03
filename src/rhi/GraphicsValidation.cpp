#include "rhi/Validation.h"
#include "rhi/GraphicsDevice.h"
#include <cmath>
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
