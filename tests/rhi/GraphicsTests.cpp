#include "rhi/GraphicsDevice.h"
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <future>

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
        return ((f == rhi::Format::RGBA8UNorm || f==rhi::Format::RGBA16Float || f==rhi::Format::RGBA32Float) && !rhi::hasUsage(u, rhi::TextureUsage::DepthAttachment) && uint32_t(u) && !(uint32_t(u) & ~31u)) ||
            (f == rhi::Format::Depth32Float && u == rhi::TextureUsage::DepthAttachment);
    }
    unsigned submissions = 0;
    unsigned pipelineAttempts=0,computeAttempts=0;
    bool failPipeline=false,failCompute=false;
    bool failTexture=false;unsigned textureAttempts=0;
    std::vector<rhi::RecordedPass> recorded;
    rhi::ComputeLimits computeLimits() const override { return {true,{128,128,128},{32,32,32},256,16,4096}; }
    size_t allocations() const { return buffers.size() + graphics.size(); }
protected:
    NativeObject allocate() { const auto id = next++;graphics.insert(id);return id; }
    NativeObject createTextureImpl(const rhi::TextureDesc&) override {++textureAttempts;if(failTexture)throw std::runtime_error("native texture failure");return allocate(); }
    NativeObject createTextureViewImpl(NativeObject, const rhi::TextureDesc&) override { return allocate(); }
    NativeObject createSamplerImpl(const rhi::SamplerDesc&) override { return allocate(); }
    NativeObject createComputePipelineImpl(const rhi::ComputePipelineDesc&) override { ++computeAttempts;if(failCompute)throw std::runtime_error("compute creation failure");return allocate(); }
    void destroyComputePipelineImpl(NativeObject id) noexcept override { graphics.erase(id); }
    NativeObject createPipelineImpl(const rhi::GraphicsPipelineDesc&) override { ++pipelineAttempts;if(failPipeline)throw std::runtime_error("graphics creation failure");return allocate(); }
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
        {
            TestDevice budget;budget.setResourceBudget(128);
            auto b=budget.createBuffer({64,BufferUsage::Vertex,"mixed buffer"});
            auto t=budget.createTexture({2,2,Format::RGBA16Float,TextureUsage::Sampled,"half float"});
            auto f=budget.createTexture({1,1,Format::RGBA32Float,TextureUsage::Sampled,"float"});
            auto z=budget.createTexture({1,1,Format::Depth32Float,TextureUsage::DepthAttachment,"depth"});
            auto usage=budget.resourceMemory();
            check(usage.bufferBytes==64 && usage.textureBytes==52 && usage.usedBytes()==116,
                  "Mixed-format texture and buffer budget counted wrong bytes");
            const auto attempts=budget.textureAttempts;
            bool rejected=false;try{budget.createTexture({2,2,Format::RGBA8UNorm,TextureUsage::Sampled,"over quota"});}
            catch(const ResourceBudgetExceeded&){rejected=true;}
            check(rejected && budget.textureAttempts==attempts && budget.resourceMemory().usedBytes()==116,
                  "Texture allocation bypassed shared device quota");
            budget.failTexture=true;rejected=false;
            try{budget.createTexture({1,1,Format::RGBA8UNorm,TextureUsage::Sampled,"native failure"});}
            catch(const std::runtime_error&){rejected=true;}
            check(rejected && budget.resourceMemory().usedBytes()==116 && budget.resourceMemory().peakBytes==116,
                  "Native texture failure polluted quota");
            budget.failTexture=false;budget.destroyTexture(t);budget.destroyTexture(t);
            check(budget.resourceMemory().textureBytes==20,"Texture double destruction released quota twice");
            budget.destroyBuffer(b);budget.destroyTexture(f);budget.destroyTexture(z);budget.close();
            check(budget.resourceMemory().usedBytes()==0,"Graphics close retained allocation bytes");
        }
        {
            auto cached=std::make_shared<TestDevice>();
            GraphicsPipelineDesc desc;
            desc.vertexStride=8;desc.attributes={{0,VertexFormat::Float2,0}};
            auto first=cached->createGraphicsPipeline(desc);
            desc.label="another caller";
            auto second=cached->createGraphicsPipeline(desc);
            check(first.value!=second.value && cached->pipelineAttempts==1 && cached->pipelineCacheStats().hits==1,
                  "Pipeline cache did not share native state through independent handles");
            cached->destroyPipeline(first);cached->destroyPipeline(first);
            check(cached->pipelineCacheStats().liveHandles==1 && cached->pipelineCacheStats().idleEntries==0,
                  "Double release corrupted another pipeline lease");
            cached->destroyPipeline(second);
            auto reused=cached->createGraphicsPipeline(desc);
            check(cached->pipelineAttempts==1 && reused.value!=second.value,
                  "Idle native pipeline was recompiled or stale handle reused");
            desc.blend=true;
            auto different=cached->createGraphicsPipeline(desc);
            check(cached->pipelineAttempts==2,"Blend state was omitted from pipeline key");
            auto layout=desc;layout.bindings={{0,{{0,BindingType::UniformBuffer,ShaderStage::Vertex,"changed layout",16}}}};
            auto layoutHandle=cached->createGraphicsPipeline(layout);
            check(cached->pipelineAttempts==3,"Binding ABI was omitted from pipeline key");
            ComputePipelineDesc compute;
            compute.threads={2,1,1};
            auto a=cached->createComputePipeline(compute),b=cached->createComputePipeline(compute);
            compute.threads={4,1,1};
            auto c=cached->createComputePipeline(compute);
            check(a.value!=b.value && cached->computeAttempts==2,"Compute threads or independent leases lost");
            cached->setPipelineCacheIdleLimit(1);
            cached->destroyPipeline(reused);
            cached->destroyPipeline(different);
            cached->destroyPipeline(layoutHandle);
            cached->destroyComputePipeline(a);cached->destroyComputePipeline(b);cached->destroyComputePipeline(c);
            check(cached->pipelineCacheStats().idleEntries==1 && cached->pipelineCacheStats().evictions>=4,
                  "Combined pipeline idle LRU failed to bound cached native state");
            const auto builds=cached->computeAttempts;
            auto recent=cached->createComputePipeline(compute);
            check(cached->computeAttempts==builds,"Pipeline LRU evicted the most recently used idle entry");
            cached->setPipelineCacheIdleLimit(0);
            check(cached->pipelineCacheStats().idleEntries==0 && cached->pipelineCacheStats().liveHandles==1,
                  "Trimming evicted a live pipeline");
            cached->destroyComputePipeline(recent);
            check(cached->allocations()==0,"Zero idle limit did not release all native pipelines");
            cached->failPipeline=true;
            bool failed=false;try {cached->createGraphicsPipeline(desc);}catch(const std::runtime_error&){failed=true;}
            check(failed && cached->pipelineCacheStats().nativeEntries==0,"Failed graphics pipeline poisoned cache");
            cached->failPipeline=false;
            auto retry=cached->createGraphicsPipeline(desc);cached->destroyPipeline(retry);
            cached->failCompute=true;failed=false;
            try {cached->createComputePipeline(compute);}catch(const std::runtime_error&){failed=true;}
            check(failed && cached->pipelineCacheStats().nativeEntries==0,"Failed compute pipeline poisoned cache");
            cached->failCompute=false;
            auto retried=cached->createComputePipeline(compute);cached->destroyComputePipeline(retried);
            const auto path=std::filesystem::temp_directory_path()/
                ("pipeline-key-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".glsl");
            {std::ofstream file(path);file<<"aaaa";}
            desc.vertex.glslPath=path.string();
            auto oldShader=cached->createGraphicsPipeline(desc);
            const auto attempts=cached->pipelineAttempts;
            const auto timestamp=std::filesystem::last_write_time(path);
            {std::ofstream file(path);file<<"bbbb";}
            std::filesystem::last_write_time(path,timestamp);
            auto newShader=cached->createGraphicsPipeline(desc);
            check(cached->pipelineAttempts==attempts+1,"Same-size/same-mtime shader edit hit a stale pipeline");
            std::filesystem::remove(path);
            auto wrongThread=std::async(std::launch::async,[&] {
                try {cached->pipelineCacheStats();}catch(const std::logic_error&){return true;}return false;
            });
            check(wrongThread.get(),"Pipeline cache accepted foreign-thread access");
            cached->close();
            check(cached->allocations()==0 && cached->pipelineCacheStats().nativeEntries==0,
                  "Close leaked active or idle native pipelines");
        }
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
        rejects([&] {d->writeTextureRegion(target,{0,0,1,1},pixel,4);});
        rejects([&] {d->writeTextureRegion(sampled,{0,0,0,1},pixel,0);});
        rejects([&] {d->writeTextureRegion(sampled,{31,0,2,1},pixel,8);});
        rejects([&] {d->writeTextureRegion(sampled,{std::numeric_limits<uint32_t>::max(),0,2,1},pixel,8);});
        rejects([&] {d->writeTextureRegion(sampled,{0,0,1,1},nullptr,4);});
        rejects([&] {d->writeTextureRegion(sampled,{0,0,1,1},pixel,3);});
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
