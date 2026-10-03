#pragma once
#include "rhi/GraphicsDevice.h"
#include <utility>
#include <stdexcept>
namespace render {
// Owned resources released in dependency order. Device shutdown makes late
// owners harmless; close must still precede destruction of a native context.
class Resources {
public:
    explicit Resources(std::shared_ptr<rhi::GraphicsDevice> device) : device(std::move(device)) {
        if (!this->device || !this->device->isOpen()) throw std::invalid_argument("Renderer: no open graphics device");
    }
    ~Resources() { clear(); }
    Resources(const Resources&) = delete;
    Resources& operator=(const Resources&) = delete;
    void clear() noexcept {
        if(!device->isOpen()){sets.clear();computePipelines.clear();pipelines.clear();views.clear();samplers.clear();textures.clear();buffers.clear();return;}
        auto weak=std::weak_ptr<rhi::GraphicsDevice>(device);
        auto release=[weak,sets=std::move(sets),computePipelines=std::move(computePipelines),pipelines=std::move(pipelines),views=std::move(views),samplers=std::move(samplers),textures=std::move(textures),buffers=std::move(buffers)](){
            auto device=weak.lock();if(!device || !device->isOpen())return;
            for(auto h:sets)device->destroyBindingSet(h);
            for(auto h:computePipelines)device->destroyComputePipeline(h);
            for(auto h:pipelines)device->destroyPipeline(h);
            for(auto h:views)device->destroyTextureView(h);
            for(auto h:samplers)device->destroySampler(h);
            for(auto h:textures)device->destroyTexture(h);
            for(auto h:buffers)device->destroyBuffer(h);
        };
        try{device->retireResources(std::move(release));}catch(...){/* Device::close reclaims a lost device. */}
    }
    rhi::BufferHandle buffer(const rhi::BufferDesc& d, const void* bytes = nullptr) { auto h = device->createBuffer(d, bytes);buffers.push_back(h);return h; }
    rhi::TextureHandle texture(const rhi::TextureDesc& d) { auto h = device->createTexture(d);textures.push_back(h);return h; }
    rhi::TextureViewHandle view(rhi::TextureHandle texture) { auto h = device->createTextureView({texture});views.push_back(h);return h; }
    rhi::SamplerHandle sampler(const rhi::SamplerDesc& d) { auto h = device->createSampler(d);samplers.push_back(h);return h; }
    rhi::PipelineHandle pipeline(const rhi::GraphicsPipelineDesc& d) { auto h = device->createGraphicsPipeline(d);pipelines.push_back(h);return h; }
    rhi::ComputePipelineHandle computePipeline(const rhi::ComputePipelineDesc& d) { auto h=device->createComputePipeline(d);computePipelines.push_back(h);return h; }
    rhi::BindingSetHandle bindings(const rhi::BindingSetDesc& d) { auto h = device->createBindingSet(d);sets.push_back(h);return h; }
    std::shared_ptr<rhi::GraphicsDevice> device;
private:
    std::vector<rhi::BufferHandle> buffers;
    std::vector<rhi::TextureHandle> textures;
    std::vector<rhi::TextureViewHandle> views;
    std::vector<rhi::SamplerHandle> samplers;
    std::vector<rhi::PipelineHandle> pipelines;
    std::vector<rhi::ComputePipelineHandle> computePipelines;
    std::vector<rhi::BindingSetHandle> sets;
};
}
