#include "rhi/GraphicsDevice.h"
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace {
void check(bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); }
template<class F> void rejects(F action) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("expected invalid graphics operation to be rejected");
}
class TestDevice final : public rhi::GraphicsDevice {
public:
    TestDevice() : GraphicsDevice({4096, 1024, 256, 16}, {128, 16, 2, 8}) {}
    rhi::Backend backend() const override { return rhi::Backend::OpenGL; }
    bool supportsTexture(rhi::Format f, rhi::TextureUsage u) const override {
        return (f == rhi::Format::RGBA8UNorm && !rhi::hasUsage(u, rhi::TextureUsage::DepthAttachment) && uint32_t(u) && !(uint32_t(u) & ~31u)) ||
            (f == rhi::Format::Depth32Float && u == rhi::TextureUsage::DepthAttachment);
    }
    unsigned submissions = 0;
    std::vector<rhi::RecordedPass> recorded;
    rhi::ComputeLimits computeLimits() const override { return {true,{128,128,128},{32,32,32},256,16,4096}; }
    size_t allocations() const { return buffers.size() + graphics.size(); }
protected:
    NativeObject allocate() { const auto id = next++;graphics.insert(id);return id; }
    NativeObject createTextureImpl(const rhi::TextureDesc&) override { return allocate(); }
    NativeObject createTextureViewImpl(NativeObject, const rhi::TextureDesc&) override { return allocate(); }
    NativeObject createSamplerImpl(const rhi::SamplerDesc&) override { return allocate(); }
    NativeObject createComputePipelineImpl(const rhi::ComputePipelineDesc&) override { return allocate(); }
    void destroyComputePipelineImpl(NativeObject id) noexcept override { graphics.erase(id); }
    NativeObject createPipelineImpl(const rhi::GraphicsPipelineDesc&) override { return allocate(); }
    void destroyTextureImpl(NativeObject id) noexcept override { graphics.erase(id); }
    void destroyTextureViewImpl(NativeObject id) noexcept override { graphics.erase(id); }
    void destroySamplerImpl(NativeObject id) noexcept override { graphics.erase(id); }
    void destroyPipelineImpl(NativeObject id) noexcept override { graphics.erase(id); }
    void writeTextureImpl(NativeObject, const rhi::TextureDesc&, const void*, size_t) override {}
    std::vector<uint8_t> readTextureImpl(NativeObject, const rhi::TextureDesc& d) override { return std::vector<uint8_t>(size_t(d.width) * d.height * 4); }
    void submitGraphicsImpl(const std::vector<rhi::RecordedPass>& passes) override { ++submissions;recorded=passes; }
    NativeBuffer createBufferImpl(const rhi::BufferDesc& d, const void* data) override {
        auto id = next++;auto& bytes = buffers[id];bytes.resize(d.size);if (data) std::memcpy(bytes.data(), data, d.size);return id;
    }
    void destroyBufferImpl(NativeBuffer id) noexcept override { buffers.erase(id); }
    void writeBufferImpl(NativeBuffer id, size_t offset, size_t size, const void* data) override { std::memcpy(buffers.at(id).data() + offset, data, size); }
    void readBufferImpl(NativeBuffer id, size_t offset, size_t size, void* data) override { std::memcpy(data, buffers.at(id).data() + offset, size); }
    void bindUniformBufferImpl(uint32_t, NativeBuffer, size_t, size_t) override {}
    void beginFrameImpl() override {}
    void presentImpl() override {}
    void waitIdleImpl() override {}
    void closeImpl() override {}
private:
    uint64_t next = 1;
    std::unordered_set<uint64_t> graphics;
    std::unordered_map<uint64_t, std::vector<uint8_t>> buffers;
};
}
int main() {
    using namespace rhi;
    try {
        auto d = std::make_shared<TestDevice>(), foreign = std::make_shared<TestDevice>();
        const auto usage = TextureUsage::ColorAttachment | TextureUsage::Sampled | TextureUsage::CopySource;
        auto target = d->createTexture({32, 32, Format::RGBA8UNorm, usage});auto view = d->createTextureView({target});
        auto sampled = d->createTexture({32, 32, Format::RGBA8UNorm, TextureUsage::Sampled | TextureUsage::CopyDestination});auto sampledView = d->createTextureView({sampled});
        auto sampler = d->createSampler({});
        rejects([&] { d->createTexture({0, 32, Format::RGBA8UNorm, usage}); });
        rejects([&] { d->createTexture({129, 32, Format::RGBA8UNorm, usage}); });
        rejects([&] { foreign->createTextureView({target}); });
        rejects([&] { d->destroyTexture(target); });
        rejects([&] { d->readTexture(sampled); });
        uint8_t pixel[4]{};rejects([&] { d->writeTexture(sampled, pixel, 4); });
        BindingLayout layout{1, {{1, BindingType::SampledTexture, ShaderStage::Fragment, "albedo", 0}}};
        auto set = d->createBindingSet({layout, {{1, {}, 0, 0, sampledView, sampler}}});
        rejects([&] { d->destroyTextureView(sampledView); });rejects([&] { d->destroySampler(sampler); });
        auto duplicate = layout;duplicate.entries.push_back(layout.entries[0]);rejects([&] { d->createBindingSet({duplicate, {}}); });
        auto invalidGroup = layout;invalidGroup.group = 2;rejects([&] { d->createBindingSet({invalidGroup, {}}); });
        auto uniform = d->createBuffer({512, BufferUsage::Uniform});
        BindingLayout frame{0, {{0, BindingType::UniformBuffer, ShaderStage::Vertex, "Frame", 16}}};
        rejects([&] { d->createBindingSet({frame, {{0, uniform, 4, 16}}}); });
        rejects([&] { d->createBindingSet({frame, {{0, uniform, 0, 8}}}); });
        auto vertices = d->createBuffer({24, BufferUsage::Vertex});auto indices = d->createBuffer({6, BufferUsage::Index});
        GraphicsPipelineDesc p;p.vertexStride = 8;p.attributes = {{0, VertexFormat::Float2, 0}};p.bindings = {layout};
        auto pipeline = d->createGraphicsPipeline(p);auto invalid = p;invalid.depthWrite = true;rejects([&] { d->createGraphicsPipeline(invalid); });
        invalid = p;invalid.attributes[0].offset = 4;rejects([&] { d->createGraphicsPipeline(invalid); });
        RenderPassDesc pass;pass.color = view;
        auto record = [&](BindingSetHandle binding, bool indexed = false, uint32_t count = 3) {
            auto list = d->createCommandList();list.beginRenderPass(pass);list.bindPipeline(pipeline);list.bindVertexBuffer(vertices);list.bindBindingSet(binding);
            if (indexed) { list.bindIndexBuffer(indices, IndexType::UInt16);list.drawIndexed(count); } else list.draw(count);
            list.endRenderPass();return list;
        };
        auto list = record(set);rejects([&] { foreign->submit(list); });d->submit(list);rejects([&] { d->submit(list); });
        auto overflow = record(set, false, 4);rejects([&] { d->submit(overflow); });
        auto indexOverflow = record(set, true, 4);rejects([&] { d->submit(indexOverflow); });
        auto validIndex = record(set, true);d->submit(validIndex);
        auto feedbackSet = d->createBindingSet({layout, {{1, {}, 0, 0, view, sampler}}});auto feedback = record(feedbackSet);rejects([&] { d->submit(feedback); });
        auto empty = d->createCommandList();rejects([&] { empty.draw(3); });rejects([&] { d->submit(empty); });
        empty.beginRenderPass(pass);rejects([&] { empty.beginRenderPass(pass); });rejects([&] { d->submit(empty); });
        empty.endRenderPass();d->submit(empty);
        auto stale = record(set);d->destroyBindingSet(set);rejects([&] { d->submit(stale); });
        auto fresh = d->createBindingSet({layout, {{1, {}, 0, 0, sampledView, sampler}}});auto staleBuffer = record(fresh);d->destroyBuffer(vertices);rejects([&] { d->submit(staleBuffer); });
        auto badPass = d->createCommandList();pass.colorLoad = LoadOp(999);badPass.beginRenderPass(pass);badPass.endRenderPass();rejects([&] { d->submit(badPass); });
        check(d->submissions == 3, "invalid commands reached native execution");
        // Validate all MRT attachments even when a pass contains no draws.
        RenderPassDesc mrt;mrt.color = view;
        auto second = d->createTexture({32,32,Format::RGBA8UNorm,usage});auto secondView = d->createTextureView({second});
        auto aliasView = d->createTextureView({target});
        auto validateMrt = [&](const RenderPassDesc& desc) { auto commands=d->createCommandList();commands.beginRenderPass(desc);commands.endRenderPass();d->submit(commands); };
        mrt.additionalColors = {{aliasView}};rejects([&] { validateMrt(mrt); });
        auto wrongSize = d->createTexture({16,32,Format::RGBA8UNorm,usage});auto wrongView = d->createTextureView({wrongSize});
        mrt.additionalColors = {{wrongView}};rejects([&] { validateMrt(mrt); });
        mrt.additionalColors = {{secondView,LoadOp(999)}};rejects([&] { validateMrt(mrt); });
        mrt.additionalColors = {{secondView,LoadOp::Clear,StoreOp::Store,{0,0,0,std::numeric_limits<float>::infinity()}}};rejects([&] { validateMrt(mrt); });
        mrt.additionalColors = {{secondView}};
        auto newVertices = d->createBuffer({24,BufferUsage::Vertex});
        auto mismatch=d->createCommandList();mismatch.beginRenderPass(mrt);mismatch.bindPipeline(pipeline);mismatch.bindBindingSet(fresh);mismatch.bindVertexBuffer(newVertices);mismatch.draw(3);mismatch.endRenderPass();rejects([&] { d->submit(mismatch); });
        auto tooMany=p;tooMany.additionalColorFormats.resize(d->graphicsLimits().maxColorAttachments,Format::RGBA8UNorm);rejects([&] { d->createGraphicsPipeline(tooMany); });
        auto mrtPipelineDesc=p;mrtPipelineDesc.additionalColorFormats={Format::RGBA8UNorm};auto mrtPipeline=d->createGraphicsPipeline(mrtPipelineDesc);
        auto extraFeedback=d->createBindingSet({layout,{{1,{},0,0,secondView,sampler}}});
        auto feedbackMrt=d->createCommandList();feedbackMrt.beginRenderPass(mrt);feedbackMrt.bindPipeline(mrtPipeline);feedbackMrt.bindBindingSet(extraFeedback);feedbackMrt.bindVertexBuffer(newVertices);feedbackMrt.draw(3);feedbackMrt.endRenderPass();rejects([&] { d->submit(feedbackMrt); });
        check(d->submissions == 3,"invalid MRT commands reached native execution");validateMrt(mrt);
        ComputePipelineDesc compute;compute.bindings={{0,{{0,BindingType::StorageWrite,ShaderStage::Compute,"Output",16}}}};compute.threads={16,1,1};
        auto computePipeline=d->createComputePipeline(compute);
        auto invalidCompute=compute;invalidCompute.threads={0,1,1};rejects([&] { d->createComputePipeline(invalidCompute); });
        invalidCompute.threads={32,32,1};rejects([&] { d->createComputePipeline(invalidCompute); });
        invalidCompute=compute;invalidCompute.bindings[0].entries[0].stage=ShaderStage::Fragment;rejects([&] { d->createComputePipeline(invalidCompute); });
        auto generatedBuffer=d->createBuffer({64,BufferUsage::Storage|BufferUsage::Vertex|BufferUsage::Indirect});
        auto computeSet=d->createBindingSet({compute.bindings[0],{{0,generatedBuffer,0,64,{},{}}}});
        rejects([&] { d->createBindingSet({compute.bindings[0],{{0,generatedBuffer,4,16,{},{}}}}); });
        auto dispatch=d->createCommandList();dispatch.dispatch(computePipeline,{computeSet},{0,1,1});rejects([&] { d->submit(dispatch); });
        auto foreignCompute=foreign->createComputePipeline(compute);auto foreignDispatch=d->createCommandList();foreignDispatch.dispatch(foreignCompute,{computeSet},{1,1,1});rejects([&] { d->submit(foreignDispatch); });
        auto staleCompute=d->createComputePipeline(compute);auto staleDispatch=d->createCommandList();staleDispatch.dispatch(staleCompute,{computeSet},{1,1,1});d->destroyComputePipeline(staleCompute);rejects([&] { d->submit(staleDispatch); });
        auto alias=compute;alias.bindings[0].entries.push_back({1,BindingType::StorageRead,ShaderStage::Compute,"Input",16});auto aliasPipeline=d->createComputePipeline(alias);
        auto aliasSet=d->createBindingSet({alias.bindings[0],{{0,generatedBuffer,0,16,{},{}},{1,generatedBuffer,16,16,{},{}}}});auto aliasDispatch=d->createCommandList();aliasDispatch.dispatch(aliasPipeline,{aliasSet},{1,1,1});rejects([&] { d->submit(aliasDispatch); });
        auto indirectCommands=[&](BufferHandle args,size_t offset) {
            auto commands=d->createCommandList();commands.beginRenderPass({view});commands.bindPipeline(pipeline);commands.bindVertexBuffer(newVertices);commands.bindBindingSet(fresh);commands.drawIndirect(args,offset);commands.endRenderPass();return commands;
        };
        auto badIndirect=indirectCommands(generatedBuffer,2);rejects([&] { d->submit(badIndirect); });
        auto overflowIndirect=indirectCommands(generatedBuffer,52);rejects([&] { d->submit(overflowIndirect); });
        auto wrongUsage=indirectCommands(newVertices,0);rejects([&] { d->submit(wrongUsage); });
        auto dependencies=d->createCommandList();dependencies.dispatch(computePipeline,{computeSet},{1,1,1});dependencies.dispatch(computePipeline,{computeSet},{1,1,1});
        dependencies.beginRenderPass({view});dependencies.bindPipeline(pipeline);dependencies.bindVertexBuffer(generatedBuffer);dependencies.bindBindingSet(fresh);dependencies.drawIndirect(generatedBuffer);dependencies.endRenderPass();d->submit(dependencies);
        check(d->recorded[1].dependencies.size()==1 && d->recorded[1].dependencies[0].before==ResourceAccess::ShaderWrite,"compute -> compute dependency missing");
        check(d->recorded[2].dependencies.size()==1 && d->recorded[2].dependencies[0].after==(ResourceAccess::VertexRead|ResourceAccess::IndirectRead),"compute -> vertex/indirect dependency missing");
        check(d->recorded[2].dependencies[0].beforeStages==ResourceStage::Compute && d->recorded[2].dependencies[0].afterStages==(ResourceStage::VertexInput|ResourceStage::DrawIndirect),"compute -> vertex/indirect stages missing");
        auto sampledTarget=d->createBindingSet({layout,{{1,{},0,0,view,sampler}}});auto textureDependencies=d->createCommandList();textureDependencies.beginRenderPass({view});textureDependencies.endRenderPass();textureDependencies.beginRenderPass({secondView});textureDependencies.bindPipeline(pipeline);textureDependencies.bindVertexBuffer(newVertices);textureDependencies.bindBindingSet(sampledTarget);textureDependencies.draw(3);textureDependencies.endRenderPass();d->submit(textureDependencies);
        check(d->recorded[1].dependencies.size()==1 && d->recorded[1].dependencies[0].texture.value==target.value && d->recorded[1].dependencies[0].after==ResourceAccess::ShaderRead,"attachment -> sampled dependency missing");


        d->close();foreign->close();check(d->allocations() == 0, "graphics close leaked native resources");
        d->destroyTexture(target);d->destroyTextureView(view);d->destroySampler(sampler);d->destroyPipeline(pipeline);
        try { d->submit(stale);throw std::runtime_error("closed device accepted commands"); } catch (const std::logic_error&) {}
        std::cout << "RHI graphics ownership, layouts, dependencies, draw ranges and submission passed\n";return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n';return 1; }
}
