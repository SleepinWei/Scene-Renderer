#include "rhi/GraphicsDevice.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <filesystem>
#include <fstream>

namespace rhi {
namespace {
std::atomic<uint64_t> nextObject{1};
struct PipelineKey {
    std::string bytes;
    void number(uint64_t value) { bytes.append(reinterpret_cast<const char*>(&value),sizeof(value)); }
    void text(const std::string& value) { number(value.size());bytes.append(value); }
    void file(const std::string& path) {
        text(path);
        if(path.empty() || !std::filesystem::exists(path)) {number(0);return;}
        number(1);
        std::ifstream input(path,std::ios::binary);
        if(!input)throw std::runtime_error("Cannot read pipeline cache shader: "+path);
        std::string content{std::istreambuf_iterator<char>(input),{}};
        if(input.bad())throw std::runtime_error("Incomplete pipeline cache shader: "+path);
        text(content); // Exact bytes distinguish same-size edits and hash collisions.
    }
    void shader(const ShaderAsset& shader, Backend backend) {
        text(shader.glslPath);text(shader.metallibPath);text(shader.spirvPath);
        text(shader.entryPoint);text(shader.spirvEntryPoint);
        file(backend==Backend::Metal?shader.metallibPath:backend==Backend::Vulkan?shader.spirvPath:shader.glslPath);
        file(shader.reflectionPath);
    }
    void layouts(const std::vector<BindingLayout>& layouts) {
        number(layouts.size());
        for(const auto& layout:layouts) {
            number(layout.group);number(layout.entries.size());
            for(const auto& entry:layout.entries) {
                number(entry.binding);number(uint32_t(entry.type));number(uint32_t(entry.stage));
                text(entry.name);number(entry.minimumSize);number(uint32_t(entry.imageFormat));
            }
        }
    }
};
std::string pipelineKey(const GraphicsPipelineDesc& desc,Backend backend) {
    PipelineKey key;
    key.shader(desc.vertex,backend);key.shader(desc.fragment,backend);
    key.number(desc.vertexStride);key.number(desc.attributes.size());
    for(const auto& attribute:desc.attributes) {
        key.number(attribute.location);key.number(uint32_t(attribute.format));key.number(attribute.offset);
    }
    key.layouts(desc.bindings);
    key.number(uint32_t(desc.colorFormat));key.number(desc.colorAttachment);
    key.number(desc.additionalColorFormats.size());
    for(auto format:desc.additionalColorFormats)key.number(uint32_t(format));
    key.number(desc.depthAttachment);key.number(desc.depthTest);key.number(desc.depthWrite);
    key.number(uint32_t(desc.depthCompare));key.number(uint32_t(desc.cull));
    key.number(desc.attachmentBlend.size());for(bool blend:desc.attachmentBlend)key.number(blend);
    key.number(desc.wireframe);key.number(desc.blend);
    return std::move(key.bytes);
}
std::string pipelineKey(const ComputePipelineDesc& desc,Backend backend) {
    PipelineKey key;key.shader(desc.shader,backend);key.layouts(desc.bindings);
    for(auto threads:desc.threads)key.number(threads);
    return std::move(key.bytes);
}
void require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(message);
}
size_t texturePayloadBytes(const TextureDesc& desc) {
    size_t stride=0;
    switch(desc.format) {
        case Format::RGBA8UNorm:case Format::Depth32Float:stride=4;break;
        case Format::RGBA16Float:stride=8;break;
        case Format::RGBA32Float:stride=16;break;
        default:throw std::invalid_argument("RHI: unknown texture format");
    }
    require(desc.height && desc.width<=std::numeric_limits<size_t>::max()/desc.height,
            "RHI: texture dimensions overflow payload size");
    const size_t pixels=size_t(desc.width)*desc.height;
    require(pixels<=std::numeric_limits<size_t>::max()/stride,"RHI: texture bytes overflow payload size");
    return pixels*stride;
}
uint32_t attributeBytes(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float2: return 8;
    case VertexFormat::Float3: return 12;
    case VertexFormat::Float4: return 16;
    }
    throw std::invalid_argument("RHI: invalid vertex format");
}
bool sameLayout(const BindingLayout& a, const BindingLayout& b) {
    if (a.group != b.group || a.entries.size() != b.entries.size()) return false;
    for (const auto& x : a.entries) {
        auto y = std::find_if(b.entries.begin(), b.entries.end(), [&](const auto& v) { return v.binding == x.binding; });
        if (y == b.entries.end() || x.type != y->type || x.stage != y->stage || x.name != y->name || x.minimumSize != y->minimumSize || x.imageFormat != y->imageFormat) return false;
    }
    return true;
}
}
std::vector<Format> colorFormats(const GraphicsPipelineDesc& desc) {
    if(!desc.colorAttachment)return {};
    std::vector<Format> result{desc.colorFormat};result.insert(result.end(), desc.additionalColorFormats.begin(), desc.additionalColorFormats.end());return result;
}
std::vector<ColorAttachment> colorAttachments(const RenderPassDesc& desc) {
    if(!desc.color)return {};
    std::vector<ColorAttachment> result{{desc.color, desc.colorLoad, desc.colorStore, desc.clearColor}};
    result.insert(result.end(), desc.additionalColors.begin(), desc.additionalColors.end());return result;
}
GraphicsDevice::GraphicsDevice(BufferLimits b, GraphicsLimits g) : Device(b), graphicsLimits_(g) {
    require(g.maxTextureDimension2D && g.maxVertexAttributes && g.maxBindingGroups && g.maxBindingsPerGroup,
            "RHI: invalid graphics limits");
}
void GraphicsDevice::checkOpen() const { checkThread();if (!isOpen()) throw std::logic_error("RHI: device is closed"); }
const GraphicsDevice::TextureRecord& GraphicsDevice::texture(TextureHandle h) const {
    checkOpen();auto it = textures_.find(h.value);
    require(it != textures_.end(), "RHI: stale or foreign texture");return it->second;
}
const GraphicsDevice::ViewRecord& GraphicsDevice::view(TextureViewHandle h) const {
    checkOpen();auto it = views_.find(h.value);
    require(it != views_.end(), "RHI: stale or foreign texture view");return it->second;
}
const GraphicsDevice::PipelineRecord& GraphicsDevice::pipeline(PipelineHandle h) const {
    checkOpen();auto it = pipelines_.find(h.value);
    require(it != pipelines_.end(), "RHI: stale or foreign pipeline");return it->second;
}
const GraphicsDevice::SamplerRecord& GraphicsDevice::sampler(SamplerHandle h) const {
    checkOpen();auto it = samplers_.find(h.value);
    require(it != samplers_.end(), "RHI: stale or foreign sampler");return it->second;
}
const BindingSetDesc& GraphicsDevice::bindingSet(BindingSetHandle h) const {
    checkOpen();auto it = bindingSets_.find(h.value);
    require(it != bindingSets_.end(), "RHI: stale or foreign binding set");return it->second;
}
TextureHandle GraphicsDevice::createTexture(const TextureDesc& desc) {
    checkOpen();
    require(desc.width && desc.height && desc.width <= graphicsLimits_.maxTextureDimension2D &&
            desc.height <= graphicsLimits_.maxTextureDimension2D && supportsTexture(desc.format, desc.usage),
            "RHI: unsupported texture descriptor");
    const auto bytes=texturePayloadBytes(desc);checkResourceAllocation(bytes,desc.label);
    auto native = createTextureImpl(desc);TextureHandle h{nextObject++};
    try { textures_.emplace(h.value, TextureRecord{desc, native}); }
    catch (...) { destroyTextureImpl(native);throw; }accountResourceAllocation(bytes,true);return h;
}
TextureViewHandle GraphicsDevice::createTextureView(const TextureViewDesc& desc) {
    const auto& t = texture(desc.texture);
    require(hasUsage(t.desc.usage, TextureUsage::Sampled) || hasUsage(t.desc.usage, TextureUsage::ColorAttachment) ||
        (hasUsage(t.desc.usage, TextureUsage::DepthAttachment) || hasUsage(t.desc.usage,TextureUsage::Storage)), "RHI: copy-only textures do not support views");
    auto native = createTextureViewImpl(t.native, t.desc);TextureViewHandle h{nextObject++};
    try { views_.emplace(h.value, ViewRecord{desc, native}); }
    catch (...) { destroyTextureViewImpl(native);throw; }return h;
}
SamplerHandle GraphicsDevice::createSampler(const SamplerDesc& desc) {
    checkOpen();require((desc.filter == Filter::Nearest || desc.filter == Filter::Linear) &&
        (desc.address == AddressMode::Repeat || desc.address == AddressMode::ClampToEdge), "RHI: invalid sampler");
    auto native = createSamplerImpl(desc);SamplerHandle h{nextObject++};
    try { samplers_.emplace(h.value, SamplerRecord{desc, native}); }
    catch (...) { destroySamplerImpl(native);throw; }return h;
}
void GraphicsDevice::validateLayout(const BindingLayout& layout) const {
    require(layout.group < graphicsLimits_.maxBindingGroups, "RHI: binding group exceeds limit");
    std::unordered_set<uint32_t> seen;
    for (const auto& e : layout.entries) {
        require(e.binding < graphicsLimits_.maxBindingsPerGroup && seen.insert(e.binding).second && !e.name.empty(),
                "RHI: duplicate, unnamed or unsupported binding");
        require(e.stage == ShaderStage::Vertex || e.stage == ShaderStage::Fragment || e.stage == ShaderStage::Compute, "RHI: invalid shader stage");
        require(e.type == BindingType::UniformBuffer || e.type == BindingType::SampledTexture || isStorage(e.type) || isStorageTexture(e.type), "RHI: invalid binding type");
    }
}
PipelineHandle GraphicsDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    checkOpen();
    require(!desc.wireframe || supportsWireframe(),"RHI: wireframe rasterization unavailable");
    require(desc.vertexStride && desc.vertexStride <= 2048 && !(desc.vertexStride % 4) && !desc.attributes.empty(), "RHI: invalid vertex stride");
    require(desc.colorAttachment || desc.additionalColorFormats.empty(),"RHI: colors specified on a depth-only pipeline");
    require(desc.colorAttachment || desc.depthAttachment,"RHI: pipeline needs an attachment");
    require(desc.attachmentBlend.empty() || desc.attachmentBlend.size()==colorFormats(desc).size(),"RHI: independent blend count differs from color targets");
    require(colorFormats(desc).size() <= graphicsLimits_.maxColorAttachments, "RHI: color attachment count exceeds limit");
    for (auto format : colorFormats(desc)) require(supportsTexture(format, TextureUsage::ColorAttachment), "RHI: unsupported color format");
    require(uint32_t(desc.depthCompare)<=uint32_t(DepthCompare::Always) && uint32_t(desc.cull)<=uint32_t(CullMode::Back),"RHI: invalid depth compare/cull mode");
    require(!desc.depthWrite || desc.depthTest, "RHI: depth write requires depth test");
    require(!desc.depthTest || desc.depthAttachment, "RHI: depth test requires a depth attachment format");
    require(!desc.depthAttachment || supportsTexture(Format::Depth32Float, TextureUsage::DepthAttachment), "RHI: unsupported depth format");
    std::unordered_set<uint32_t> seen;
    for (const auto& a : desc.attributes) {
        const auto bytes = attributeBytes(a.format);
        require(a.location < graphicsLimits_.maxVertexAttributes && seen.insert(a.location).second &&
                !(a.offset % 4) && a.offset <= desc.vertexStride && bytes <= desc.vertexStride - a.offset, "RHI: invalid vertex attribute");
    }
    seen.clear();for (const auto& l : desc.bindings) { validateLayout(l);require(seen.insert(l.group).second, "RHI: duplicate binding group"); }
    for (const auto& l : desc.bindings) for (const auto& e : l.entries) require(e.stage != ShaderStage::Compute && (!isStorage(e.type) || (e.type==BindingType::StorageRead && e.stage==ShaderStage::Vertex && computeLimits().supported)) && !isStorageTexture(e.type), "RHI: graphics storage bindings are not implemented");
    auto key = pipelineKey(desc,backend());
    auto cached = graphicsPipelineCache_.find(key);
    const bool created = cached == graphicsPipelineCache_.end();
    if(created) {
        const auto native = createPipelineImpl(desc);
        try {cached=graphicsPipelineCache_.emplace(std::move(key),CachedPipeline{native,0,++pipelineClock_}).first;}
        catch (...) {destroyPipelineImpl(native);throw;}
        ++pipelineStats_.graphicsBuilds;
    }
    PipelineHandle handle{nextObject++};
    try {pipelines_.emplace(handle.value,PipelineRecord{desc,cached->second.native,&cached->second});}
    catch (...) {
        if(created){destroyPipelineImpl(cached->second.native);graphicsPipelineCache_.erase(cached);}
        throw;
    }
    ++cached->second.leases;cached->second.touched=++pipelineClock_;
    if(!created)++pipelineStats_.hits;
    return handle;
}
void GraphicsDevice::validateBindings(const BindingSetDesc& desc) const {
    validateLayout(desc.layout);
    require(desc.entries.size() == desc.layout.entries.size(), "RHI: binding set does not cover layout");
    std::unordered_set<uint32_t> seen;
    for (const auto& b : desc.entries) {
        require(seen.insert(b.binding).second, "RHI: duplicate binding resource");
        auto e = std::find_if(desc.layout.entries.begin(), desc.layout.entries.end(), [&](const auto& v) { return v.binding == b.binding; });
        require(e != desc.layout.entries.end(), "RHI: resource absent from layout");
        if (e->type == BindingType::UniformBuffer || isStorage(e->type)) {
            const bool storage = isStorage(e->type);const auto limits = computeLimits();
            nativeBuffer(b.buffer, storage ? BufferUsage::Storage : BufferUsage::Uniform);const auto& d = bufferDesc(b.buffer);
            require(!storage || (limits.supported && (e->stage == ShaderStage::Compute || (e->stage==ShaderStage::Vertex && e->type==BindingType::StorageRead)) && b.size <= limits.maxStorageRange), "RHI: unsupported storage binding");
            require(!b.texture && !b.sampler && b.size && b.size >= e->minimumSize && b.offset <= d.size && b.size <= d.size - b.offset &&
                !(b.offset % (storage ? limits.storageOffsetAlignment : bufferLimits().uniformOffsetAlignment)), "RHI: invalid buffer binding range");
        } else if (isStorageTexture(e->type)) {
            const auto& texture = viewTextureDesc(b.texture);
            require(e->stage == ShaderStage::Compute && computeLimits().maxStorageImages && !b.buffer && !b.offset && !b.size && !b.sampler &&
                hasUsage(texture.usage, TextureUsage::Storage) && texture.format == e->imageFormat, "RHI: invalid storage texture binding");
        } else {
            require(!b.buffer && !b.offset && !b.size && hasUsage(viewTextureDesc(b.texture).usage, TextureUsage::Sampled),
                    "RHI: invalid sampled texture binding");

            sampler(b.sampler);
        }
    }
}
BindingSetHandle GraphicsDevice::createBindingSet(const BindingSetDesc& desc) {
    checkOpen();validateBindings(desc);BindingSetHandle h{nextObject++};bindingSets_.emplace(h.value, desc);return h;
}
void GraphicsDevice::destroyBindingSet(BindingSetHandle h) { checkThread(); bindingSets_.erase(h.value); }
void GraphicsDevice::destroyPipeline(PipelineHandle h) { checkThread();
    auto it = pipelines_.find(h.value);if (it == pipelines_.end()) return;
    --it->second.cached->leases;it->second.cached->touched=++pipelineClock_;
    pipelines_.erase(it);trimPipelineCache();
}
PipelineCacheStats GraphicsDevice::pipelineCacheStats() const {
    checkThread();
    auto stats=pipelineStats_;
    stats.nativeEntries=graphicsPipelineCache_.size()+computePipelineCache_.size();
    stats.liveHandles=pipelines_.size()+computePipelines_.size();
    for(const auto& item:graphicsPipelineCache_)if(!item.second.leases)++stats.idleEntries;
    for(const auto& item:computePipelineCache_)if(!item.second.leases)++stats.idleEntries;
    return stats;
}
void GraphicsDevice::setPipelineCacheIdleLimit(size_t count) {
    checkOpen();pipelineIdleLimit_=count;trimPipelineCache();
}
void GraphicsDevice::trimPipelineCache() {
    checkThread();
    if(trimmingPipelineCache_ || pipelineCacheStats().idleEntries<=pipelineIdleLimit_)return;
    trimmingPipelineCache_=true;
    try {
        waitForResourceRelease();
        while(pipelineCacheStats().idleEntries>pipelineIdleLimit_) {
            auto graphics=graphicsPipelineCache_.end(),compute=computePipelineCache_.end();
            for(auto it=graphicsPipelineCache_.begin();it!=graphicsPipelineCache_.end();++it)
                if(!it->second.leases && (graphics==graphicsPipelineCache_.end() || it->second.touched<graphics->second.touched))graphics=it;
            for(auto it=computePipelineCache_.begin();it!=computePipelineCache_.end();++it)
                if(!it->second.leases && (compute==computePipelineCache_.end() || it->second.touched<compute->second.touched))compute=it;
            if(graphics!=graphicsPipelineCache_.end() && (compute==computePipelineCache_.end() || graphics->second.touched<compute->second.touched)) {
                destroyPipelineImpl(graphics->second.native);graphicsPipelineCache_.erase(graphics);
            } else if(compute!=computePipelineCache_.end()) {
                destroyComputePipelineImpl(compute->second.native);computePipelineCache_.erase(compute);
            } else break;
            ++pipelineStats_.evictions;
        }
        trimmingPipelineCache_=false;
    } catch (...) {trimmingPipelineCache_=false;throw;}
}
void GraphicsDevice::destroyTexture(TextureHandle h) { checkThread();
    auto it = textures_.find(h.value);if (it == textures_.end()) return;
    for (const auto& v : views_) require(v.second.desc.texture.value != h.value, "RHI: texture still has live views");
    waitForResourceRelease();destroyTextureImpl(it->second.native);
    accountResourceRelease(texturePayloadBytes(it->second.desc),true);textures_.erase(it);
}
void GraphicsDevice::destroyTextureView(TextureViewHandle h) { checkThread();
    auto it = views_.find(h.value);if (it == views_.end()) return;
    for (const auto& s : bindingSets_) for (const auto& b : s.second.entries)
        require(b.texture.value != h.value, "RHI: texture view still has live binding sets");
    waitForResourceRelease();destroyTextureViewImpl(it->second.native);views_.erase(it);
}
void GraphicsDevice::destroySampler(SamplerHandle h) { checkThread();
    auto it = samplers_.find(h.value);if (it == samplers_.end()) return;
    for (const auto& s : bindingSets_) for (const auto& b : s.second.entries)
        require(b.sampler.value != h.value, "RHI: sampler still has live binding sets");
    waitForResourceRelease();destroySamplerImpl(it->second.native);samplers_.erase(it);
}
void GraphicsDevice::writeTextureRegion(TextureHandle h, TextureRegion r, const void* data, size_t bytes) {
    const auto& t=texture(h);const uint32_t bpp=t.desc.format==Format::RGBA8UNorm?4:t.desc.format==Format::RGBA32Float?16:0;
    require(bpp && hasUsage(t.desc.usage,TextureUsage::CopyDestination) && data && r.width && r.height &&
        uint64_t(r.x)+r.width<=t.desc.width && uint64_t(r.y)+r.height<=t.desc.height &&
        uint64_t(r.width)*r.height*bpp==bytes,"RHI: invalid texture region upload");
    writeTextureRegionImpl(t.native,t.desc,r,data,bytes);
}
void GraphicsDevice::writeTextureRegionImpl(NativeObject,const TextureDesc&,TextureRegion,const void*,size_t){throw std::invalid_argument("RHI: texture region upload unsupported");}
void GraphicsDevice::writeTexture(TextureHandle h, const void* rgba, size_t bytes) {
    const auto& t = texture(h);
    require(t.desc.format == Format::RGBA8UNorm && hasUsage(t.desc.usage, TextureUsage::CopyDestination) && rgba &&
        uint64_t(t.desc.width) * t.desc.height * 4 == bytes, "RHI: invalid RGBA8 texture upload");
    writeTextureImpl(t.native, t.desc, rgba, bytes);
}
ReadbackTicket GraphicsDevice::requestTextureReadback(TextureHandle handle){
    const auto& record=texture(handle);require(hasUsage(record.desc.usage,TextureUsage::CopySource),"RHI: readback requires CopySource");auto bytes=std::make_shared<std::vector<uint8_t>>();auto callback=queueTextureReadbackImpl(record.native,record.desc,bytes);return {checkpoint(std::move(callback)),bytes};
}
std::function<void()> GraphicsDevice::queueTextureReadbackImpl(NativeObject id,const TextureDesc& desc,std::shared_ptr<std::vector<uint8_t>> output){
    require(desc.format==Format::RGBA8UNorm,"RHI: async float readback unavailable on this backend");*output=readTextureImpl(id,desc);return {};
}
std::vector<uint8_t> GraphicsDevice::readTexture(TextureHandle h) {
    const auto& t = texture(h);
    require(t.desc.format == Format::RGBA8UNorm && hasUsage(t.desc.usage, TextureUsage::CopySource), "RHI: texture cannot be read as RGBA8");
    return readTextureImpl(t.native, t.desc);
}
std::vector<float> GraphicsDevice::readTextureFloat(TextureHandle h) {
    const auto& t = texture(h);
    require((t.desc.format == Format::RGBA16Float || t.desc.format == Format::RGBA32Float || t.desc.format == Format::Depth32Float) && hasUsage(t.desc.usage, TextureUsage::CopySource), "RHI: texture cannot be read as float RGBA");
    return readTextureFloatImpl(t.native, t.desc);
}
void GraphicsDevice::copyToBackbuffer(TextureHandle h) {
    const auto& t = texture(h);
    require(supportsPresentation() && t.desc.format == Format::RGBA8UNorm && hasUsage(t.desc.usage, TextureUsage::CopySource) &&
        hasUsage(t.desc.usage, TextureUsage::Sampled), "RHI: unsupported backbuffer copy");
    copyToBackbufferImpl(t.native, t.desc);
}
void GraphicsDevice::writeTextureFloat(TextureHandle handle, const float* rgba, size_t bytes) {
    const auto& t = texture(handle);
    require(t.desc.format == Format::RGBA32Float && hasUsage(t.desc.usage, TextureUsage::CopyDestination) && rgba && uint64_t(t.desc.width)*t.desc.height*16 == bytes,"RHI: invalid RGBA32F upload");
    writeTextureFloatImpl(t.native,t.desc,rgba,bytes);
}
void GraphicsDevice::writeTextureFloatImpl(NativeObject, const TextureDesc&, const float*, size_t) { throw std::invalid_argument("RHI: float texture upload unsupported"); }
std::vector<float> GraphicsDevice::readTextureFloatImpl(NativeObject, const TextureDesc&) { throw std::logic_error("RHI: float readback is unsupported"); }
void GraphicsDevice::copyToBackbufferImpl(NativeObject, const TextureDesc&) { throw std::logic_error("RHI: presentation is unsupported"); }
std::vector<float> decodeHalfPixels(const std::vector<uint8_t>& bytes) {
    require(!(bytes.size() % 2), "RHI: invalid half pixel data");std::vector<float> values(bytes.size() / 2);
    for (size_t i = 0; i < values.size(); ++i) {
        uint16_t h;std::memcpy(&h, bytes.data() + i * 2, 2);
        const int exponent = (h >> 10) & 31, mantissa = h & 1023;
        const float magnitude = exponent == 0 ? std::ldexp(float(mantissa), -24) : exponent == 31 ?
            (mantissa ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity()) : std::ldexp(1.f + float(mantissa) / 1024.f, exponent - 15);
        values[i] = std::copysign(magnitude, h & 32768 ? -1.f : 1.f);
    }return values;
}
GraphicsDevice::NativeObject GraphicsDevice::textureObject(TextureHandle h) const { return texture(h).native; }
const TextureDesc& GraphicsDevice::textureDesc(TextureHandle h) const { return texture(h).desc; }
GraphicsDevice::NativeObject GraphicsDevice::textureViewObject(TextureViewHandle h) const { return view(h).native; }
const TextureDesc& GraphicsDevice::viewTextureDesc(TextureViewHandle h) const { return texture(view(h).desc.texture).desc; }
GraphicsDevice::NativeObject GraphicsDevice::pipelineObject(PipelineHandle h) const { return pipeline(h).native; }
const GraphicsPipelineDesc& GraphicsDevice::pipelineDesc(PipelineHandle h) const { return pipeline(h).desc; }
std::vector<GraphicsDevice::NativeBinding> GraphicsDevice::resolvedBindings(BindingSetHandle h) const {
    const auto& d = bindingSet(h);validateBindings(d);std::vector<NativeBinding> result;
    for (const auto& e : d.layout.entries) {
        const auto& b = *std::find_if(d.entries.begin(), d.entries.end(), [&](const auto& v) { return v.binding == e.binding; });
        NativeBinding n{e};
        if (e.type == BindingType::UniformBuffer || isStorage(e.type)) { n.buffer = nativeBuffer(b.buffer, isStorage(e.type) ? BufferUsage::Storage : BufferUsage::Uniform);n.offset = b.offset;n.size = b.size; }
        else { n.textureView = view(b.texture).native;if (b.sampler) n.sampler = sampler(b.sampler).native; }
        // Flatten two logical groups for GL/Metal; Vulkan retains the group in its layout.
        n.layout.binding += d.layout.group * graphicsLimits_.maxBindingsPerGroup;
        result.push_back(n);
    }
    return result;
}
GraphicsDevice::NativeObject GraphicsDevice::createComputePipelineImpl(const ComputePipelineDesc&) { throw std::invalid_argument("RHI: compute is unsupported"); }
ComputePipelineHandle GraphicsDevice::createComputePipeline(const ComputePipelineDesc& desc) {
    checkOpen();const auto limits = computeLimits();require(limits.supported,"RHI: compute is unsupported by this device");
    uint64_t threads = 1;
    for (size_t i=0;i<3;++i) { require(desc.threads[i] && desc.threads[i] <= limits.maxThreads[i],"RHI: compute thread dimension exceeds limit");threads *= desc.threads[i]; }
    require(threads <= limits.maxInvocations,"RHI: compute workgroup exceeds limit");
    std::unordered_set<uint32_t> seen;uint32_t storageCount=0, uniformCount=0, imageCount=0, sampledCount=0;
    for (const auto& layout : desc.bindings) {
        validateLayout(layout);require(seen.insert(layout.group).second,"RHI: duplicate compute binding group");
        for (const auto& e : layout.entries) { require(e.stage == ShaderStage::Compute && (isStorage(e.type) || isStorageTexture(e.type) || e.type == BindingType::SampledTexture || e.type == BindingType::UniformBuffer),"RHI: unsupported compute binding");if(isStorage(e.type))++storageCount;else if(isStorageTexture(e.type))++imageCount;else if(e.type == BindingType::SampledTexture)++sampledCount;else ++uniformCount; }
    }
    require(storageCount <= limits.maxStorageBindings && uniformCount <= limits.maxUniformBindings && imageCount <= limits.maxStorageImages && sampledCount <= limits.maxSampledTextures,"RHI: compute binding count exceeds limit");
    auto key=pipelineKey(desc,backend());
    auto cached=computePipelineCache_.find(key);
    const bool created=cached==computePipelineCache_.end();
    if(created) {
        const auto native=createComputePipelineImpl(desc);
        try {cached=computePipelineCache_.emplace(std::move(key),CachedPipeline{native,0,++pipelineClock_}).first;}
        catch (...) {destroyComputePipelineImpl(native);throw;}
        ++pipelineStats_.computeBuilds;
    }
    ComputePipelineHandle handle{nextObject++};
    try {computePipelines_.emplace(handle.value,ComputeRecord{desc,cached->second.native,&cached->second});}
    catch (...) {
        if(created){destroyComputePipelineImpl(cached->second.native);computePipelineCache_.erase(cached);}
        throw;
    }
    ++cached->second.leases;cached->second.touched=++pipelineClock_;
    if(!created)++pipelineStats_.hits;
    return handle;
}
void GraphicsDevice::destroyComputePipeline(ComputePipelineHandle handle) { checkThread();
    const auto it = computePipelines_.find(handle.value);if (it == computePipelines_.end()) return;
    --it->second.cached->leases;it->second.cached->touched=++pipelineClock_;
    computePipelines_.erase(it);trimPipelineCache();
}
GraphicsDevice::NativeObject GraphicsDevice::computePipelineObject(ComputePipelineHandle h) const {
    computePipelineDesc(h);return computePipelines_.at(h.value).native;
}
const ComputePipelineDesc& GraphicsDevice::computePipelineDesc(ComputePipelineHandle h) const {
    checkOpen();const auto it = computePipelines_.find(h.value);require(it != computePipelines_.end(),"RHI: stale or foreign compute pipeline");return it->second.desc;
}
void GraphicsDevice::validatePass(const RecordedPass& pass) const {
    if(pass.copy) {
        const auto& source=texture(pass.copySource).desc;const auto& destination=texture(pass.copyDestination).desc;
        require(pass.copySource.value!=pass.copyDestination.value && source.format!=Format::Depth32Float && source.format==destination.format && source.width==destination.width && source.height==destination.height && hasUsage(source.usage,TextureUsage::CopySource) && hasUsage(destination.usage,TextureUsage::CopyDestination),"RHI: incompatible texture copy");return;
    }
    if (pass.compute) {
        const auto& dispatch = pass.dispatch;const auto& p = computePipelineDesc(dispatch.pipeline);const auto limits = computeLimits();
        if(dispatch.indirect){nativeBuffer(dispatch.indirect,BufferUsage::Indirect);const auto& args=bufferDesc(dispatch.indirect);require(!(dispatch.indirectOffset%4) && dispatch.indirectOffset<=args.size && 12<=args.size-dispatch.indirectOffset,"RHI: invalid indirect dispatch range");}
        else for (size_t i=0;i<3;++i) require(dispatch.groups[i] && dispatch.groups[i] <= limits.maxGroups[i],"RHI: compute dispatch exceeds limit");
        require(dispatch.bindings.size() == p.bindings.size(),"RHI: missing or extra compute binding groups");
        std::unordered_map<uint64_t,BindingType> uses, textureUses;
        for (const auto& l : p.bindings) {
            auto b = std::find_if(dispatch.bindings.begin(),dispatch.bindings.end(),[&](auto h) { return bindingSet(h).layout.group == l.group; });
            require(b != dispatch.bindings.end() && sameLayout(l,bindingSet(*b).layout),"RHI: incompatible compute binding layout");validateBindings(bindingSet(*b));
            for (const auto& entry : bindingSet(*b).entries) {
                const auto& layout = *std::find_if(l.entries.begin(),l.entries.end(),[&](const auto& e) { return e.binding == entry.binding; });
                const auto readOnly = [](BindingType t) { return t == BindingType::UniformBuffer || t == BindingType::StorageRead || t == BindingType::SampledTexture || t == BindingType::StorageTextureRead; };
                auto& resources = entry.texture ? textureUses : uses;
                const auto resource = entry.texture ? view(entry.texture).desc.texture.value : entry.buffer.value;
                auto previous = resources.find(resource);
                require(previous == resources.end() || (readOnly(previous->second) && readOnly(layout.type) && (!entry.texture || previous->second == layout.type)),"RHI: writable resource aliases in one dispatch");resources[resource] = layout.type;
            }
        }return;
    }
    const auto validLoad = [](LoadOp op) { return op == LoadOp::Clear || op == LoadOp::Load || op == LoadOp::Discard; };
    const auto validStore = [](StoreOp op) { return op == StoreOp::Store || op == StoreOp::Discard; };
    require(validLoad(pass.desc.colorLoad) && validStore(pass.desc.colorStore) &&
        validLoad(pass.desc.depthLoad) && validStore(pass.desc.depthStore), "RHI: invalid attachment operation");
    require(pass.desc.color || pass.desc.depth,"RHI: render pass has no attachment");
    require(pass.desc.color || pass.desc.additionalColors.empty(),"RHI: additional colors require a primary attachment");
    const auto& color = viewTextureDesc(pass.desc.color ? pass.desc.color : pass.desc.depth);
    const auto& viewport=pass.desc.viewport;
    require((!viewport.width && !viewport.height && !viewport.x && !viewport.y) || (viewport.width && viewport.height && viewport.x<=color.width && viewport.width<=color.width-viewport.x && viewport.y<=color.height && viewport.height<=color.height-viewport.y),"RHI: viewport exceeds attachment");
    const auto& clip=pass.desc.scissor;
    require((!clip.width && !clip.height && !clip.x && !clip.y) || (clip.width && clip.height && clip.x<=color.width && clip.width<=color.width-clip.x && clip.y<=color.height && clip.height<=color.height-clip.y),"RHI: scissor exceeds attachment");
    const auto colors = colorAttachments(pass.desc);
    require(colors.size() <= graphicsLimits_.maxColorAttachments, "RHI: color attachment count exceeds limit");
    std::unordered_set<uint64_t> attachmentTextures;
    for (const auto& attachment : colors) {
        const auto& desc = viewTextureDesc(attachment.view);
        require(hasUsage(desc.usage, TextureUsage::ColorAttachment) && desc.width == color.width && desc.height == color.height,
            "RHI: incompatible color attachment");
        require(validLoad(attachment.load) && validStore(attachment.store), "RHI: invalid color attachment operation");
        require(attachmentTextures.insert(view(attachment.view).desc.texture.value).second, "RHI: duplicate color attachment texture");
        for (float v : attachment.clear) require(std::isfinite(v), "RHI: nonfinite clear color");
    }
    if (pass.desc.depth) {
        const auto& depth = viewTextureDesc(pass.desc.depth);
        require(hasUsage(depth.usage, TextureUsage::DepthAttachment) && depth.width == color.width && depth.height == color.height,
                "RHI: incompatible depth attachment");
        require(std::isfinite(pass.desc.clearDepth) && pass.desc.clearDepth >= 0 && pass.desc.clearDepth <= 1, "RHI: invalid clear depth");
    }
    for (const auto& draw : pass.draws) {
        const auto& p = pipelineDesc(draw.pipeline);
        const auto formats = colorFormats(p);
        require(formats.size() == colors.size() && p.depthAttachment == bool(pass.desc.depth), "RHI: pipeline and pass formats differ");
        for (size_t i = 0; i < formats.size(); ++i) require(formats[i] == viewTextureDesc(colors[i].view).format, "RHI: pipeline and pass formats differ");
        nativeBuffer(draw.vertices, BufferUsage::Vertex);const auto& vertices = bufferDesc(draw.vertices);
        require(draw.vertexOffset <= vertices.size && !(draw.vertexOffset % 4), "RHI: invalid vertex offset");
        if (draw.indirect) {
            nativeBuffer(draw.indirect,BufferUsage::Indirect);const auto& args = bufferDesc(draw.indirect);
            require(!draw.indexed || !draw.indexOffset,"RHI: indexed indirect requires zero index offset");
            const size_t bytes = draw.indexed ? sizeof(DrawIndexedIndirectArguments) : sizeof(DrawIndirectArguments);
            require(!(draw.indirectOffset % 4) && draw.indirectOffset <= args.size && bytes <= args.size-draw.indirectOffset,"RHI: invalid indirect argument range");
            require(p.vertexStride <= vertices.size-draw.vertexOffset,"RHI: empty indirect vertex range");
        }
        if (draw.indexed) {
            nativeBuffer(draw.indices, BufferUsage::Index);const auto& indices = bufferDesc(draw.indices);
            const size_t unit = draw.indexType == IndexType::UInt16 ? 2 : 4;
            require(draw.indexType == IndexType::UInt16 || draw.indexType == IndexType::UInt32, "RHI: invalid index type");
            require(draw.indexOffset <= indices.size && !(draw.indexOffset % unit) &&
                (uint64_t(draw.first) + draw.count) <= (indices.size - draw.indexOffset) / unit, "RHI: index draw exceeds buffer");
            require(!draw.indirect || unit <= indices.size-draw.indexOffset,"RHI: empty indirect index range");
            require(p.vertexStride <= vertices.size - draw.vertexOffset, "RHI: empty indexed vertex range");
        } else if (!draw.indirect) require((uint64_t(draw.first) + draw.count) <= (vertices.size - draw.vertexOffset) / p.vertexStride, "RHI: vertex draw exceeds buffer");
        require(draw.bindings.size() == p.bindings.size(), "RHI: missing or extra binding groups");
        for (const auto& l : p.bindings) {
            auto b = std::find_if(draw.bindings.begin(), draw.bindings.end(), [&](auto h) { return bindingSet(h).layout.group == l.group; });
            require(b != draw.bindings.end() && sameLayout(l, bindingSet(*b).layout), "RHI: incompatible binding layout");
            validateBindings(bindingSet(*b));
            for (const auto& entry : bindingSet(*b).entries) if (entry.texture) {
                const auto parent = view(entry.texture).desc.texture.value;
                require(!attachmentTextures.count(parent) &&
                    (!pass.desc.depth || parent != view(pass.desc.depth).desc.texture.value), "RHI: attachment sampling feedback is unsupported");
            }
        }
    }
}
CommandList GraphicsDevice::createCommandList() { checkOpen();return CommandList(shared_from_this()); }
void GraphicsDevice::submit(CommandList& list) {
    checkOpen();require(list.owner_.get() == this && !list.submitted_ && !list.inPass_ && !list.passes_.empty(), "RHI: invalid command list submission");
    for (const auto& pass : list.passes_) validatePass(pass); // Validate entire list before any native work.
    // Compile pass dependencies from declared attachment/binding/buffer uses.
    // Native backends currently lower these boundaries conservatively; submissions
    // remain synchronous, so no resource can be concurrently recycled by the CPU.
    std::unordered_map<uint64_t,ResourceAccess> previousBuffers, previousTextures;
    std::unordered_map<uint64_t,ResourceStage> previousBufferStages, previousTextureStages;
    const auto writable = [](ResourceAccess access) { return uint32_t(access)&(uint32_t(ResourceAccess::ShaderWrite)|uint32_t(ResourceAccess::AttachmentWrite)|uint32_t(ResourceAccess::CopyWrite)); };
    for (auto& pass : list.passes_) {
        std::unordered_map<uint64_t,ResourceAccess> buffers, textures;
        std::unordered_map<uint64_t,ResourceStage> bufferStages, textureStages;
        auto bindings = [&](const std::vector<BindingSetHandle>& sets) {
            for (auto set : sets) {
                const auto& desc = bindingSet(set);
                for (const auto& entry : desc.entries) {
                    const auto& layout = *std::find_if(desc.layout.entries.begin(),desc.layout.entries.end(),[&](const auto& e) { return e.binding == entry.binding; });
                    const auto stage = layout.stage == ShaderStage::Vertex ? ResourceStage::Vertex : layout.stage == ShaderStage::Fragment ? ResourceStage::Fragment : ResourceStage::Compute;
                    if (entry.texture) {
                        const auto texture = view(entry.texture).desc.texture.value;
                        auto access = layout.type == BindingType::StorageTextureWrite ? ResourceAccess::ShaderWrite : layout.type == BindingType::StorageTextureReadWrite ? ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite : ResourceAccess::ShaderRead;
                        textures[texture] = textures[texture] | access;textureStages[texture] = textureStages[texture] | stage;
                    }
                    else {
                        auto access = layout.type == BindingType::UniformBuffer ? ResourceAccess::UniformRead : layout.type == BindingType::StorageRead ? ResourceAccess::ShaderRead : layout.type == BindingType::StorageWrite ? ResourceAccess::ShaderWrite : ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
                        buffers[entry.buffer.value] = buffers[entry.buffer.value] | access;bufferStages[entry.buffer.value] = bufferStages[entry.buffer.value] | stage;
                    }
                }
            }
        };
        if(pass.copy) {
            textures[pass.copySource.value]=ResourceAccess::CopyRead;textures[pass.copyDestination.value]=ResourceAccess::CopyWrite;
            textureStages[pass.copySource.value]=textureStages[pass.copyDestination.value]=ResourceStage::Transfer;
        } else if (pass.compute) {bindings(pass.dispatch.bindings);if(pass.dispatch.indirect){buffers[pass.dispatch.indirect.value]=buffers[pass.dispatch.indirect.value]|ResourceAccess::IndirectRead;bufferStages[pass.dispatch.indirect.value]=bufferStages[pass.dispatch.indirect.value]|ResourceStage::DrawIndirect;}}
        else {
            for (const auto& color : colorAttachments(pass.desc)) { const auto texture=view(color.view).desc.texture.value;textures[texture] = ResourceAccess::AttachmentWrite | (color.load == LoadOp::Load ? ResourceAccess::AttachmentRead : ResourceAccess::None);textureStages[texture] = ResourceStage::ColorOutput; }
            if (pass.desc.depth) { const auto texture=view(pass.desc.depth).desc.texture.value;textures[texture] = ResourceAccess::AttachmentRead | ResourceAccess::AttachmentWrite;textureStages[texture]=ResourceStage::DepthTest; }
            for (const auto& draw : pass.draws) {
                buffers[draw.vertices.value] = buffers[draw.vertices.value] | ResourceAccess::VertexRead;bufferStages[draw.vertices.value]=bufferStages[draw.vertices.value]|ResourceStage::VertexInput;
                if (draw.indexed) { buffers[draw.indices.value] = buffers[draw.indices.value] | ResourceAccess::IndexRead;bufferStages[draw.indices.value]=bufferStages[draw.indices.value]|ResourceStage::VertexInput; }
                if (draw.indirect) { buffers[draw.indirect.value] = buffers[draw.indirect.value] | ResourceAccess::IndirectRead;bufferStages[draw.indirect.value]=bufferStages[draw.indirect.value]|ResourceStage::DrawIndirect; }
                bindings(draw.bindings);
            }
        }
        pass.dependencies.clear();
        for (const auto& use : buffers) {
            auto previous = previousBuffers.find(use.first);
            if (previous != previousBuffers.end() && (writable(previous->second) || writable(use.second))) pass.dependencies.push_back({{use.first},{},previous->second,use.second,previousBufferStages[use.first],bufferStages[use.first]});
            previousBufferStages[use.first] = previous != previousBuffers.end() && !writable(previous->second) && !writable(use.second) ? previousBufferStages[use.first] | bufferStages[use.first] : bufferStages[use.first];
            previousBuffers[use.first] = previous != previousBuffers.end() && !writable(previous->second) && !writable(use.second) ? previous->second | use.second : use.second;
        }
        for (const auto& use : textures) {
            auto previous = previousTextures.find(use.first);
            if (previous != previousTextures.end() && (writable(previous->second) || writable(use.second))) pass.dependencies.push_back({{},{use.first},previous->second,use.second,previousTextureStages[use.first],textureStages[use.first]});
            previousTextureStages[use.first] = previous != previousTextures.end() && !writable(previous->second) && !writable(use.second) ? previousTextureStages[use.first] | textureStages[use.first] : textureStages[use.first];
            previousTextures[use.first] = previous != previousTextures.end() && !writable(previous->second) && !writable(use.second) ? previous->second | use.second : use.second;
        }
    }
    list.submitted_ = true;
    submitGraphicsImpl(list.passes_);if(!frameActive())waitIdle();
}
void GraphicsDevice::releaseResourcesImpl() {
    pipelineIdleLimit_=0;
    bindingSets_.clear();
    while (!computePipelines_.empty()) destroyComputePipeline(ComputePipelineHandle{computePipelines_.begin()->first});
    while (!pipelines_.empty()) destroyPipeline(PipelineHandle{pipelines_.begin()->first});
    trimPipelineCache();
    while (!views_.empty()) destroyTextureView(TextureViewHandle{views_.begin()->first});
    while (!samplers_.empty()) destroySampler(SamplerHandle{samplers_.begin()->first});
    while (!textures_.empty()) destroyTexture(TextureHandle{textures_.begin()->first});
}
std::shared_ptr<GraphicsDevice> graphicsDevice() {
    auto result = std::dynamic_pointer_cast<GraphicsDevice>(device());
    if (!result) throw std::logic_error("RHI: device does not implement graphics");return result;
}
CommandList::CommandList(std::shared_ptr<GraphicsDevice> owner) : owner_(std::move(owner)) {}
void CommandList::requirePass() const { require(owner_ && owner_->isOpen() && inPass_ && !submitted_, "RHI: no active render pass"); }
void CommandList::beginRenderPass(const RenderPassDesc& desc) {
    require(owner_ && owner_->isOpen() && !inPass_ && !submitted_, "RHI: cannot begin render pass");
    passes_.push_back({desc, {}});state_ = {};inPass_ = true;
}
void CommandList::bindPipeline(PipelineHandle pipeline) { requirePass();state_.pipeline = pipeline; }
void CommandList::bindBindingSet(BindingSetHandle set) {
    requirePass();const auto group = owner_->bindingSet(set).layout.group;
    for (auto& old : state_.bindings) if (owner_->bindingSet(old).layout.group == group) { old = set;return; }
    state_.bindings.push_back(set);
}
void CommandList::bindVertexBuffer(BufferHandle h, size_t offset) { requirePass();state_.vertices = h;state_.vertexOffset = offset; }
void CommandList::bindIndexBuffer(BufferHandle h, IndexType type, size_t offset) { requirePass();state_.indices = h;state_.indexType = type;state_.indexOffset = offset; }
void CommandList::draw(uint32_t count, uint32_t first) {
    requirePass();require(count && state_.pipeline && state_.vertices, "RHI: incomplete draw");
    auto draw = state_;draw.count = count;draw.first = first;draw.indexed = false;passes_.back().draws.push_back(std::move(draw));
}
void CommandList::drawIndexed(uint32_t count, uint32_t first, int32_t baseVertex) {
    requirePass();require(count && state_.pipeline && state_.vertices && state_.indices, "RHI: incomplete indexed draw");
    auto draw = state_;draw.count = count;draw.first = first;draw.baseVertex = baseVertex;draw.indexed = true;passes_.back().draws.push_back(std::move(draw));
}
void CommandList::drawIndirect(BufferHandle arguments, size_t offset) {
    requirePass();require(state_.pipeline && state_.vertices && arguments,"RHI: incomplete indirect draw");
    auto draw = state_;draw.indirect = arguments;draw.indirectOffset = offset;draw.indexed = false;passes_.back().draws.push_back(std::move(draw));
}
void CommandList::drawIndexedIndirect(BufferHandle arguments, size_t offset) {
    requirePass();require(state_.pipeline && state_.vertices && state_.indices && arguments,"RHI: incomplete indexed indirect draw");
    auto draw = state_;draw.indirect = arguments;draw.indirectOffset = offset;draw.indexed = true;passes_.back().draws.push_back(std::move(draw));
}
void CommandList::copyTexture(TextureHandle source,TextureHandle destination) {
    require(owner_ && owner_->isOpen() && !submitted_ && !inPass_,"RHI: texture copy must be outside a render pass");
    RecordedPass pass;pass.copy=true;pass.copySource=source;pass.copyDestination=destination;passes_.push_back(std::move(pass));
}
void CommandList::dispatch(ComputePipelineHandle pipeline, const std::vector<BindingSetHandle>& bindings, std::array<uint32_t,3> groups) {
    require(owner_ && owner_->isOpen() && !submitted_ && !inPass_,"RHI: dispatch must be outside a render pass");
    RecordedPass pass;pass.compute = true;pass.dispatch = {pipeline,bindings,groups};passes_.push_back(std::move(pass));
}
void CommandList::dispatchIndirect(ComputePipelineHandle pipeline,const std::vector<BindingSetHandle>& bindings,BufferHandle args,size_t offset){
    require(owner_ && owner_->isOpen() && !submitted_ && !inPass_ && args,"RHI: indirect dispatch must be outside a render pass");RecordedPass pass;pass.compute=true;pass.dispatch.pipeline=pipeline;pass.dispatch.bindings=bindings;pass.dispatch.indirect=args;pass.dispatch.indirectOffset=offset;passes_.push_back(std::move(pass));
}
void CommandList::endRenderPass() { requirePass();inPass_ = false; }
} // namespace rhi

namespace rhi {size_t GraphicsDevice::allocatedTextureBytes() const {return resourceMemory().textureBytes;}}
