#include "rhi/GraphicsDevice.h"
#include "rhi/ShaderAssets.h"
#include "rhi/PipelineDiskCache.h"
#include <Metal/Metal.h>
#include <Cocoa/Cocoa.h>
#include <QuartzCore/CAMetalLayer.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <json/json.hpp>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <map>
#include <stdexcept>
namespace rhi {namespace {
using json=nlohmann::json;
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error("RHI Metal: "+message);}
std::string errorText(NSError* error){return error?std::string(error.localizedDescription.UTF8String):std::string("unknown Metal error");}
struct Buffer{id<MTLBuffer> gpu;size_t size;};
struct NativeState{
    id<MTLDevice> device=nil;id<MTLCommandQueue> queue=nil;id<MTLCommandBuffer> command=nil;
    CAMetalLayer* layer=nil;id<CAMetalDrawable> drawable=nil;id<MTLTexture> screen=nil;
    GLFWwindow* window=nullptr;uint64_t next=1;std::unordered_map<uint64_t,Buffer> buffers;
};
class MetalDevice final : public rhi::GraphicsDevice {
public:
    explicit MetalDevice(NativeState state) : GraphicsDevice({size_t(state.device.maxBufferLength),65536,256,32},{16384,16,3,8,8}),state_(std::move(state)) {
        if(@available(macOS 11.0,*)) {
            if(std::getenv("SCENERENDERER_GPU_PROFILE") && [state_.device supportsFamily:MTLGPUFamilyApple1] && [state_.device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary])
                for(id<MTLCounterSet> set in state_.device.counterSets)if([set.name isEqualToString:MTLCommonCounterSetTimestamp])counterSet_=set;
            auto version=[NSProcessInfo processInfo].operatingSystemVersion;
            cachePath_=pipelineDiskPath("metal-"+std::to_string(state_.device.registryID)+"-"+std::to_string(version.majorVersion)+"-"+std::to_string(version.minorVersion)+"-"+std::to_string(version.patchVersion)+".archive");
            NSError* error=nil;auto desc=[MTLBinaryArchiveDescriptor new];
            const auto bytes=readPipelineDisk(cachePath_);if(!bytes.empty())desc.url=[NSURL fileURLWithPath:[NSString stringWithUTF8String:cachePath_.c_str()]];
            archive_=[state_.device newBinaryArchiveWithDescriptor:desc error:&error];const bool loaded=archive_ && desc.url;
            if(!archive_){desc.url=nil;archive_=[state_.device newBinaryArchiveWithDescriptor:desc error:&error];}
            diskStats_={archive_!=nil,loaded,false,loaded?bytes.size():0,cachePath_.string()};
        }
    }
    ~MetalDevice() override {try{close();}catch(...){}}
    std::vector<GpuProfileSample> drainGpuProfileImpl() override {auto result=std::move(profileSamples_);profileSamples_.clear();return result;}
    bool supportsWireframe() const override { return true; }
    rhi::ComputeLimits computeLimits() const override {
        auto size=state_.device.maxThreadsPerThreadgroup;
        return {true,{65535,65535,65535},{uint32_t(size.width),uint32_t(size.height),uint32_t(size.depth)},1024,16,size_t(state_.device.maxBufferLength),16,16,16,16};
    }
    std::array<uint32_t,2> presentationExtent()const override{if(hasConfiguredExtent_)return configuredExtent_;int w=0,h=0;if(state_.window)glfwGetFramebufferSize(state_.window,&w,&h);return {uint32_t(w),uint32_t(h)};}
    bool supportsPresentation() const override {return state_.window!=nullptr;}
    rhi::Backend backend() const override { return rhi::Backend::Metal; }
    bool supportsTexture(rhi::Format format,rhi::TextureUsage usage) const override {
        if(!uint32_t(usage)||(uint32_t(usage)&~63u))return false;
        if(format==rhi::Format::Depth32Float)return rhi::hasUsage(usage,rhi::TextureUsage::DepthAttachment) && !(uint32_t(usage)&~uint32_t(rhi::TextureUsage::DepthAttachment|rhi::TextureUsage::Sampled|rhi::TextureUsage::CopySource));
        return (format==rhi::Format::RGBA8UNorm||format==rhi::Format::RGBA16Float||format==rhi::Format::RGBA32Float)&&!rhi::hasUsage(usage,rhi::TextureUsage::DepthAttachment);
    }
protected:
    static std::unordered_map<uint32_t,uint32_t> textureSlots(const rhi::ShaderAsset& asset) {
        std::ifstream file(asset.reflectionPath);json reflection;file>>reflection;
        std::unordered_map<uint32_t,uint32_t> result;uint32_t slot=0;
        for(const char* kind:{"textures","images"}) {
            std::vector<uint32_t> bindings;
            for(const auto& resource:reflection.value(kind,json::array())) bindings.push_back(resource.value("set",0u)*8+resource.at("binding").get<uint32_t>());
            std::sort(bindings.begin(),bindings.end());
            for(auto binding:bindings)result[binding]=slot++;
        }return result;
    }
    static MTLPixelFormat format(rhi::Format value) {
        return value==rhi::Format::RGBA8UNorm?MTLPixelFormatRGBA8Unorm:value==rhi::Format::RGBA16Float?MTLPixelFormatRGBA16Float:value==rhi::Format::RGBA32Float?MTLPixelFormatRGBA32Float:MTLPixelFormatDepth32Float;
    }
    PipelineDiskCacheStats pipelineDiskCacheStatsImpl() const override {return diskStats_;}
    void savePipelineDiskCacheImpl() override {
        if(!archive_ || cachePath_.empty())return;
        std::filesystem::path temporary;
        try {
            std::filesystem::create_directories(cachePath_.parent_path());temporary=pipelineDiskTemporary(cachePath_);
            NSError* error=nil;auto url=[NSURL fileURLWithPath:[NSString stringWithUTF8String:temporary.c_str()]];
            if(![archive_ serializeToURL:url error:&error])throw std::runtime_error("Metal archive serialization failed");
            auto bytes=std::filesystem::file_size(temporary);if(bytes>64*1024*1024)throw std::runtime_error("Metal archive exceeds cache limit");
            std::filesystem::rename(temporary,cachePath_);diskStats_.saved=true;diskStats_.bytes=bytes;
        }catch(...){std::error_code error;std::filesystem::remove(temporary,error);}
    }
    bool supportsTextureSubresources() const override {return true;}
    GpuTimingStats gpuTimingStatsImpl() const override {return gpuTiming_;}
    NativeMemoryStats nativeMemoryStatsImpl() const override {
        return {true,uint64_t(state_.device.currentAllocatedSize),uint64_t(state_.device.recommendedMaxWorkingSetSize),"Metal device allocations"};
    }
    NativeObject createTextureImpl(const rhi::TextureDesc& desc) override {
        auto d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format(desc.format) width:desc.width height:desc.height mipmapped:NO];
        d.mipmapLevelCount=desc.mipLevels;d.arrayLength=desc.arrayLayers;d.textureType=desc.arrayLayers>1?MTLTextureType2DArray:MTLTextureType2D;
        d.storageMode=MTLStorageModePrivate;d.usage=MTLTextureUsagePixelFormatView;
        if(rhi::hasUsage(desc.usage,rhi::TextureUsage::Storage))d.usage|=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite;
        if(rhi::hasUsage(desc.usage,rhi::TextureUsage::Sampled))d.usage|=MTLTextureUsageShaderRead;
        if(rhi::hasUsage(desc.usage,rhi::TextureUsage::ColorAttachment)||rhi::hasUsage(desc.usage,rhi::TextureUsage::DepthAttachment))d.usage|=MTLTextureUsageRenderTarget;
        auto texture=[state_.device newTextureWithDescriptor:d];require(texture!=nil,"RHI texture allocation failed");
        texture.label=[NSString stringWithUTF8String:desc.label.c_str()];auto id=state_.next++;rhiTextures_[id]=texture;return id;
    }
    NativeObject createTextureViewImpl(NativeObject id,const rhi::TextureDesc& desc,const rhi::TextureViewDesc& selection) override {
        auto view=[rhiTextures_.at(id) newTextureViewWithPixelFormat:format(desc.format) textureType:MTLTextureType2D levels:NSMakeRange(selection.range.firstMip,selection.range.mipCount) slices:NSMakeRange(selection.range.firstLayer,1)];require(view!=nil,"RHI texture view creation failed");
        auto handle=state_.next++;rhiViews_[handle]=view;return handle;
    }
    NativeObject createSamplerImpl(const rhi::SamplerDesc& desc) override {
        auto d=[MTLSamplerDescriptor new];d.minFilter=d.magFilter=desc.filter==rhi::Filter::Nearest?MTLSamplerMinMagFilterNearest:MTLSamplerMinMagFilterLinear;
        d.sAddressMode=d.tAddressMode=desc.address==rhi::AddressMode::Repeat?MTLSamplerAddressModeRepeat:MTLSamplerAddressModeClampToEdge;
        auto sampler=[state_.device newSamplerStateWithDescriptor:d];require(sampler!=nil,"RHI sampler creation failed");
        auto id=state_.next++;rhiSamplers_[id]=sampler;return id;
    }
    NativeObject createComputePipelineImpl(const rhi::ComputePipelineDesc& desc) override {
        rhi::validateComputeShaderLayout(desc);NSError* error=nil;
        auto lib=[state_.device newLibraryWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:desc.shader.metallibPath.c_str()]] error:&error];require(lib!=nil,"RHI compute library: "+errorText(error));
        auto function=[lib newFunctionWithName:[NSString stringWithUTF8String:desc.shader.entryPoint.c_str()]];require(function!=nil,"RHI compute entry point missing");
        auto d=[MTLComputePipelineDescriptor new];d.computeFunction=function;
        if(archive_){d.binaryArchives=@[archive_];[archive_ addComputePipelineFunctionsWithDescriptor:d error:&error];}
        auto pipeline=[state_.device newComputePipelineStateWithDescriptor:d options:MTLPipelineOptionNone reflection:nil error:&error];
        if(!pipeline && archive_){d.binaryArchives=nil;pipeline=[state_.device newComputePipelineStateWithDescriptor:d options:MTLPipelineOptionNone reflection:nil error:&error];}require(pipeline!=nil,"RHI compute pipeline: "+errorText(error));
        const uint64_t threads=uint64_t(desc.threads[0])*desc.threads[1]*desc.threads[2];require(threads<=pipeline.maxTotalThreadsPerThreadgroup,"RHI compute pipeline workgroup exceeds limit");
        auto id=state_.next++;rhiComputePipelines_[id]={pipeline,textureSlots(desc.shader)};return id;
    }
    void destroyComputePipelineImpl(NativeObject id) noexcept override {rhiComputePipelines_.erase(id);}
    NativeObject createPipelineImpl(const rhi::GraphicsPipelineDesc& desc) override {
        rhi::validateShaderLayout(desc);
        auto function=[&](const rhi::ShaderAsset& asset) {
            NSError* error=nil;
            auto lib=[state_.device newLibraryWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:asset.metallibPath.c_str()]] error:&error];
            require(lib!=nil,"RHI shader library: "+errorText(error));auto f=[lib newFunctionWithName:[NSString stringWithUTF8String:asset.entryPoint.c_str()]];
            require(f!=nil,"RHI shader entry point missing");return f;
        };
        auto d=[MTLRenderPipelineDescriptor new];d.vertexFunction=function(desc.vertex);d.fragmentFunction=function(desc.fragment);
        auto v=[MTLVertexDescriptor vertexDescriptor];
        for(const auto& a:desc.attributes) {
            v.attributes[a.location].format=a.format==rhi::VertexFormat::Float2?MTLVertexFormatFloat2:a.format==rhi::VertexFormat::Float3?MTLVertexFormatFloat3:MTLVertexFormatFloat4;
            v.attributes[a.location].offset=a.offset;v.attributes[a.location].bufferIndex=30;
        }
        v.layouts[30].stride=desc.vertexStride;v.layouts[30].stepFunction=MTLVertexStepFunctionPerVertex;d.vertexDescriptor=v;
        const auto formats=rhi::colorFormats(desc);
        for(size_t i=0;i<formats.size();++i) {
            auto a=d.colorAttachments[i];a.pixelFormat=format(formats[i]);a.blendingEnabled=desc.attachmentBlend.empty()?desc.blend:desc.attachmentBlend.at(i);
            a.sourceRGBBlendFactor=MTLBlendFactorSourceAlpha;a.destinationRGBBlendFactor=MTLBlendFactorOneMinusSourceAlpha;
            a.sourceAlphaBlendFactor=MTLBlendFactorOne;a.destinationAlphaBlendFactor=MTLBlendFactorOneMinusSourceAlpha;
        }
        if(desc.depthAttachment)d.depthAttachmentPixelFormat=MTLPixelFormatDepth32Float;
        NSError* error=nil;if(archive_){d.binaryArchives=@[archive_];[archive_ addRenderPipelineFunctionsWithDescriptor:d error:&error];}
        auto pipeline=[state_.device newRenderPipelineStateWithDescriptor:d error:&error];
        if(!pipeline && archive_){d.binaryArchives=nil;pipeline=[state_.device newRenderPipelineStateWithDescriptor:d error:&error];}require(pipeline!=nil,"RHI pipeline: "+errorText(error));
        auto depth=[MTLDepthStencilDescriptor new];depth.depthCompareFunction=!desc.depthTest?MTLCompareFunctionAlways:desc.depthCompare==rhi::DepthCompare::Less?MTLCompareFunctionLess:desc.depthCompare==rhi::DepthCompare::LessEqual?MTLCompareFunctionLessEqual:desc.depthCompare==rhi::DepthCompare::Greater?MTLCompareFunctionGreater:MTLCompareFunctionAlways;depth.depthWriteEnabled=desc.depthWrite;
        auto depthState=[state_.device newDepthStencilStateWithDescriptor:depth];require(depthState!=nil,"RHI depth state creation failed");
        auto id=state_.next++;rhiPipelines_[id]={pipeline,depthState,textureSlots(desc.vertex),textureSlots(desc.fragment)};return id;
    }
    void destroyTextureImpl(NativeObject id) noexcept override {rhiTextures_.erase(id);}
    void destroyTextureViewImpl(NativeObject id) noexcept override {rhiViews_.erase(id);}
    void destroySamplerImpl(NativeObject id) noexcept override {rhiSamplers_.erase(id);}
    void destroyPipelineImpl(NativeObject id) noexcept override {rhiPipelines_.erase(id);}
    void writeTextureRegionImpl(NativeObject id,const rhi::TextureDesc& desc,rhi::TextureRegion r,const void* pixels,size_t) override {
        const size_t pixelBytes=desc.format==rhi::Format::RGBA8UNorm?4:16;
        const size_t row=(size_t(r.width)*pixelBytes+255)&~size_t(255);
        auto buffer=[state_.device newBufferWithLength:row*r.height options:MTLResourceStorageModeShared];require(buffer!=nil,"RHI texture staging failed");
        for(size_t y=0;y<r.height;++y)memcpy(static_cast<uint8_t*>(buffer.contents)+y*row,static_cast<const uint8_t*>(pixels)+y*r.width*pixelBytes,r.width*pixelBytes);
        endEncoders();command();auto e=[state_.command blitCommandEncoder];
        [e copyFromBuffer:buffer sourceOffset:0 sourceBytesPerRow:row sourceBytesPerImage:row*r.height sourceSize:MTLSizeMake(r.width,r.height,1)
               toTexture:rhiTextures_.at(id) destinationSlice:r.layer destinationLevel:r.mip destinationOrigin:MTLOriginMake(r.x,r.y,0)];[e endEncoding];
    }
    void writeTextureImpl(NativeObject id,const rhi::TextureDesc& desc,const void* pixels,size_t bytes) override {writeTextureRegionImpl(id,desc,{0,0,desc.width,desc.height},pixels,bytes);}
    void writeTextureFloatImpl(NativeObject id,const rhi::TextureDesc& desc,const float* pixels,size_t bytes) override {writeTextureRegionImpl(id,desc,{0,0,desc.width,desc.height},pixels,bytes);}
    std::vector<uint8_t> readPixels(NativeObject id,const rhi::TextureDesc& desc,size_t pixelBytes) {
        const size_t row=(size_t(desc.width)*pixelBytes+255)&~size_t(255);
        auto buffer=[state_.device newBufferWithLength:row*desc.height options:MTLResourceStorageModeShared];require(buffer!=nil,"RHI texture readback allocation failed");
        endEncoders();command();auto e=[state_.command blitCommandEncoder];
        [e copyFromTexture:rhiTextures_.at(id) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(desc.width,desc.height,1)
                 toBuffer:buffer destinationOffset:0 destinationBytesPerRow:row destinationBytesPerImage:row*desc.height];[e endEncoding];finish();
        std::vector<uint8_t> result(size_t(desc.width)*desc.height*pixelBytes);
        for(size_t y=0;y<desc.height;++y)memcpy(result.data()+y*desc.width*pixelBytes,static_cast<uint8_t*>(buffer.contents)+y*row,desc.width*pixelBytes);return result;
    }
    std::function<void()> queueTextureReadbackImpl(NativeObject id,const rhi::TextureDesc& desc,std::shared_ptr<std::vector<uint8_t>> output,rhi::TextureSubresource level) override {
        const size_t pixelBytes=desc.format==rhi::Format::RGBA8UNorm || desc.format==rhi::Format::Depth32Float?4:desc.format==rhi::Format::RGBA16Float?8:16,row=(size_t(desc.width)*pixelBytes+255)&~size_t(255);
        auto buffer=[state_.device newBufferWithLength:row*desc.height options:MTLResourceStorageModeShared];require(buffer!=nil,"RHI async staging allocation failed");endEncoders();command();auto e=[state_.command blitCommandEncoder];[e copyFromTexture:rhiTextures_.at(id) sourceSlice:level.layer sourceLevel:level.mip sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(desc.width,desc.height,1) toBuffer:buffer destinationOffset:0 destinationBytesPerRow:row destinationBytesPerImage:row*desc.height];[e endEncoding];
        return [buffer,row,pixelBytes,desc,output]{output->resize(size_t(desc.width)*desc.height*pixelBytes);for(size_t y=0;y<desc.height;++y)memcpy(output->data()+y*desc.width*pixelBytes,static_cast<const uint8_t*>(buffer.contents)+y*row,desc.width*pixelBytes);};
    }
    std::vector<uint8_t> readTextureImpl(NativeObject id,const rhi::TextureDesc& desc) override {return readPixels(id,desc,4);}
    std::vector<float> readTextureFloatImpl(NativeObject id,const rhi::TextureDesc& desc) override {
        if(desc.format==rhi::Format::RGBA16Float)return rhi::decodeHalfPixels(readPixels(id,desc,8));
        auto bytes=readPixels(id,desc,desc.format==rhi::Format::Depth32Float?4:16);std::vector<float> values(bytes.size()/4);memcpy(values.data(),bytes.data(),bytes.size());return values;
    }
    void copyToBackbufferImpl(NativeObject id,const rhi::TextureDesc& desc) override {
        require(frameActive() || state_.drawable!=nil,"RHI: beginFrame is required before backbuffer copy");
        if(state_.drawable==nil || state_.screen==nil)return; // Minimized/unavailable surface; frame still completes offscreen.
        (void)desc;
        if(!rhiPresentPipeline_) {
            const char* source=R"MSL(
#include <metal_stdlib>
using namespace metal;
vertex float4 rhiPresentVertex(uint id [[vertex_id]]) {
    const float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};return float4(p[id],0,1);
}
fragment float4 rhiPresentFragment(float4 position [[position]],texture2d<float> source [[texture(0)]],constant float2& targetSize [[buffer(0)]]) {uint2 size=uint2(source.get_width(),source.get_height());return source.read(min(uint2(position.xy*float2(size)/targetSize),size-1));}
)MSL";
            NSError* error=nil;auto library=[state_.device newLibraryWithSource:[NSString stringWithUTF8String:source] options:nil error:&error];require(library!=nil,"RHI presentation library: "+errorText(error));
            auto d=[MTLRenderPipelineDescriptor new];d.vertexFunction=[library newFunctionWithName:@"rhiPresentVertex"];d.fragmentFunction=[library newFunctionWithName:@"rhiPresentFragment"];
            d.colorAttachments[0].pixelFormat=MTLPixelFormatBGRA8Unorm;rhiPresentPipeline_=[state_.device newRenderPipelineStateWithDescriptor:d error:&error];require(rhiPresentPipeline_!=nil,"RHI presentation pipeline: "+errorText(error));
        }
        endEncoders();command();auto pass=[MTLRenderPassDescriptor renderPassDescriptor];pass.colorAttachments[0].texture=state_.screen;
        pass.colorAttachments[0].loadAction=MTLLoadActionDontCare;pass.colorAttachments[0].storeAction=MTLStoreActionStore;
        auto sample=profilePass("presentation","render",4);
        if(sample.buffer){auto a=pass.sampleBufferAttachments[0];a.sampleBuffer=sample.buffer;a.startOfVertexSampleIndex=sample.index;a.endOfVertexSampleIndex=sample.index+1;a.startOfFragmentSampleIndex=sample.index+2;a.endOfFragmentSampleIndex=sample.index+3;}
        auto e=[state_.command renderCommandEncoderWithDescriptor:pass];require(e!=nil,"RHI presentation encoder failed");
        [e setRenderPipelineState:rhiPresentPipeline_];[e setFragmentTexture:rhiTextures_.at(id) atIndex:0];const float targetSize[2]={float(state_.screen.width),float(state_.screen.height)};[e setFragmentBytes:targetSize length:sizeof(targetSize) atIndex:0];[e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[e endEncoding];
    }
    void submitGraphicsImpl(const std::vector<rhi::RecordedPass>& passes) override {
        endEncoders();command();
        auto load=[](rhi::LoadOp value) {return value==rhi::LoadOp::Clear?MTLLoadActionClear:value==rhi::LoadOp::Load?MTLLoadActionLoad:MTLLoadActionDontCare;};
        auto store=[](rhi::StoreOp value) {return value==rhi::StoreOp::Store?MTLStoreActionStore:MTLStoreActionDontCare;};
        for(const auto& pass:passes) {
            if(pass.copy) {
                const auto& desc=textureDesc(pass.copySource);auto e=profileBlit(pass.label.empty()?"texture-copy":pass.label);e.label=[NSString stringWithUTF8String:pass.label.c_str()];
                [e copyFromTexture:rhiTextures_.at(textureObject(pass.copySource)) sourceSlice:pass.sourceSubresource.layer sourceLevel:pass.sourceSubresource.mip sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(std::max(1u,desc.width>>pass.sourceSubresource.mip),std::max(1u,desc.height>>pass.sourceSubresource.mip),1) toTexture:rhiTextures_.at(textureObject(pass.copyDestination)) destinationSlice:pass.destinationSubresource.layer destinationLevel:pass.destinationSubresource.mip destinationOrigin:MTLOriginMake(0,0,0)];[e endEncoding];continue;
            }
            if(pass.compute) {
                const auto& dispatch=pass.dispatch;const auto& desc=computePipelineDesc(dispatch.pipeline);
                auto computeDesc=[MTLComputePassDescriptor computePassDescriptor];auto sample=profilePass(pass.label.empty()?desc.label:pass.label,"compute",2);
                if(sample.buffer){auto a=computeDesc.sampleBufferAttachments[0];a.sampleBuffer=sample.buffer;a.startOfEncoderSampleIndex=sample.index;a.endOfEncoderSampleIndex=sample.index+1;}
                auto e=[state_.command computeCommandEncoderWithDescriptor:computeDesc];require(e!=nil,"RHI compute encoder creation failed");e.label=[NSString stringWithUTF8String:pass.label.c_str()];
                const auto& pipeline=rhiComputePipelines_.at(computePipelineObject(dispatch.pipeline));[e setComputePipelineState:pipeline.pipeline];
                for(auto set:dispatch.bindings)for(const auto& b:resolvedBindings(set)) {
                    if(b.buffer)[e setBuffer:state_.buffers.at(b.buffer).gpu offset:b.offset atIndex:b.layout.binding];
                    else {const auto index=pipeline.textures.at(b.layout.binding);[e setTexture:rhiViews_.at(b.textureView) atIndex:index];if(b.sampler)[e setSamplerState:rhiSamplers_.at(b.sampler) atIndex:index];}
                }
                if(dispatch.indirect)[e dispatchThreadgroupsWithIndirectBuffer:state_.buffers.at(unsigned(nativeBuffer(dispatch.indirect,rhi::BufferUsage::Indirect))).gpu indirectBufferOffset:dispatch.indirectOffset threadsPerThreadgroup:MTLSizeMake(desc.threads[0],desc.threads[1],desc.threads[2])];
                else [e dispatchThreadgroups:MTLSizeMake(dispatch.groups[0],dispatch.groups[1],dispatch.groups[2]) threadsPerThreadgroup:MTLSizeMake(desc.threads[0],desc.threads[1],desc.threads[2])];
                // Tracked resources and encoder boundaries resolve compute -> compute/render hazards.
                [e endEncoding];continue;
            }
            const auto& desc=viewTextureDesc(pass.desc.color?pass.desc.color:pass.desc.depth);auto d=[MTLRenderPassDescriptor renderPassDescriptor];
            const auto colors=rhi::colorAttachments(pass.desc);
            for(size_t i=0;i<colors.size();++i) {
                const auto& color=colors[i];auto a=d.colorAttachments[i];a.texture=rhiViews_.at(textureViewObject(color.view));a.loadAction=load(color.load);
                a.storeAction=store(color.store);const auto& c=color.clear;a.clearColor=MTLClearColorMake(c[0],c[1],c[2],c[3]);
            }
            if(pass.desc.depth) {d.depthAttachment.texture=rhiViews_.at(textureViewObject(pass.desc.depth));d.depthAttachment.loadAction=load(pass.desc.depthLoad);
                d.depthAttachment.storeAction=store(pass.desc.depthStore);d.depthAttachment.clearDepth=pass.desc.clearDepth;}
            auto sample=profilePass(pass.label.empty()?"render":pass.label,"render",4);
            if(sample.buffer){auto a=d.sampleBufferAttachments[0];a.sampleBuffer=sample.buffer;a.startOfVertexSampleIndex=sample.index;a.endOfVertexSampleIndex=sample.index+1;a.startOfFragmentSampleIndex=sample.index+2;a.endOfFragmentSampleIndex=sample.index+3;}
            auto e=[state_.command renderCommandEncoderWithDescriptor:d];require(e!=nil,"RHI render encoder creation failed");e.label=[NSString stringWithUTF8String:pass.label.c_str()];
            const auto v=pass.desc.viewport.width?pass.desc.viewport:rhi::Viewport{0,0,desc.width,desc.height};
            [e setViewport:MTLViewport{double(v.x),double(v.y),double(v.width),double(v.height),0,1}];const auto clip=pass.desc.scissor.width?pass.desc.scissor:v;[e setScissorRect:MTLScissorRect{clip.x,clip.y,clip.width,clip.height}];[e setFrontFacingWinding:MTLWindingCounterClockwise];[e setCullMode:MTLCullModeNone];[e setTriangleFillMode:MTLTriangleFillModeFill];
            for(const auto& draw:pass.draws) {
                const auto& p=rhiPipelines_.at(pipelineObject(draw.pipeline));[e setRenderPipelineState:p.pipeline];[e setDepthStencilState:p.depth];
                [e setTriangleFillMode:pipelineDesc(draw.pipeline).wireframe?MTLTriangleFillModeLines:MTLTriangleFillModeFill];const auto cull=pipelineDesc(draw.pipeline).cull;[e setCullMode:cull==rhi::CullMode::None?MTLCullModeNone:cull==rhi::CullMode::Back?MTLCullModeBack:MTLCullModeFront];
                [e setVertexBuffer:state_.buffers.at(nativeBuffer(draw.vertices,rhi::BufferUsage::Vertex)).gpu offset:draw.vertexOffset atIndex:30];
                for(auto set:draw.bindings)for(const auto& b:resolvedBindings(set)) {
                    const auto index=b.layout.binding;const bool vertex=b.layout.stage==rhi::ShaderStage::Vertex;
                    if(b.buffer) {
                        auto buffer=state_.buffers.at(b.buffer).gpu;
                        if(vertex)[e setVertexBuffer:buffer offset:b.offset atIndex:index];else [e setFragmentBuffer:buffer offset:b.offset atIndex:index];
                    } else {
                        auto texture=rhiViews_.at(b.textureView);auto sampler=rhiSamplers_.at(b.sampler);const auto textureIndex=(vertex?p.vertexTextures:p.fragmentTextures).at(index);
                        if(vertex) {[e setVertexTexture:texture atIndex:textureIndex];[e setVertexSamplerState:sampler atIndex:textureIndex];}
                        else {[e setFragmentTexture:texture atIndex:textureIndex];[e setFragmentSamplerState:sampler atIndex:textureIndex];}
                    }
                }
                if(draw.indirect) {
                    auto arguments=state_.buffers.at(nativeBuffer(draw.indirect,rhi::BufferUsage::Indirect)).gpu;
                    if(draw.indexed) [e drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexType:draw.indexType==rhi::IndexType::UInt16?MTLIndexTypeUInt16:MTLIndexTypeUInt32 indexBuffer:state_.buffers.at(nativeBuffer(draw.indices,rhi::BufferUsage::Index)).gpu indexBufferOffset:draw.indexOffset indirectBuffer:arguments indirectBufferOffset:draw.indirectOffset];
                    else [e drawPrimitives:MTLPrimitiveTypeTriangle indirectBuffer:arguments indirectBufferOffset:draw.indirectOffset];
                } else if(draw.indexed) {
                    const auto type=draw.indexType==rhi::IndexType::UInt16?MTLIndexTypeUInt16:MTLIndexTypeUInt32;const auto unit=draw.indexType==rhi::IndexType::UInt16?2u:4u;
                    [e drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:draw.count indexType:type indexBuffer:state_.buffers.at(nativeBuffer(draw.indices,rhi::BufferUsage::Index)).gpu
                           indexBufferOffset:draw.indexOffset+size_t(draw.first)*unit instanceCount:1 baseVertex:draw.baseVertex baseInstance:0];
                } else [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:draw.first vertexCount:draw.count];
            }
            [e endEncoding];
        }
    }
    NativeBuffer createBufferImpl(const rhi::BufferDesc& desc, const void* data) override {
        auto gpu=[state_.device newBufferWithLength:std::max<size_t>((desc.size+15)&~size_t(15),16)
                                      options:MTLResourceStorageModeShared];
        require(gpu!=nil,"RHI buffer allocation failed: "+desc.label);
        gpu.label=[NSString stringWithUTF8String:desc.label.c_str()];
        if(data)memcpy(gpu.contents,data,desc.size);
        else if(desc.initialization == rhi::BufferInitialization::Zeroed) memset(gpu.contents,0,desc.size);
        const unsigned id=state_.next++;
        state_.buffers.emplace(id,Buffer{gpu,desc.size});
        return id;
    }
    void destroyBufferImpl(NativeBuffer native) noexcept override {
        const auto id=static_cast<unsigned>(native);
        state_.buffers.erase(id);
    }
    void writeBufferImpl(NativeBuffer native,size_t offset,size_t size,const void* data) override {
        auto& b=state_.buffers.at(static_cast<unsigned>(native));
        endEncoders();command();
        auto staging=[state_.device newBufferWithBytes:data length:size options:MTLResourceStorageModeShared];
        require(staging!=nil,"RHI staging allocation failed");
        auto e=profileBlit("buffer-upload");
        [e copyFromBuffer:staging sourceOffset:0 toBuffer:b.gpu destinationOffset:offset size:size];
        [e endEncoding];
    }
    void readBufferImpl(NativeBuffer native,size_t offset,size_t size,void* data) override {
        waitIdleImpl();
        memcpy(data,static_cast<uint8_t*>(state_.buffers.at(static_cast<unsigned>(native)).gpu.contents)+offset,size);
    }
    void bindUniformBufferImpl(uint32_t slot,NativeBuffer native,size_t offset,size_t) override {
        throw std::logic_error("Native Metal uses explicit BindingSet; legacy uniform binding is unavailable");
    }
    void beginFrameImpl() override {
        if(state_.window){auto size=presentationExtent();state_.drawable=nil;state_.screen=nil;if(size[0] && size[1]){state_.layer.drawableSize=CGSizeMake(size[0],size[1]);state_.drawable=[state_.layer nextDrawable];if(state_.drawable)state_.screen=state_.drawable.texture;}}command();
    }
    void presentImpl() override {
        if(!frameActive()){endEncoders();command();if(state_.drawable)[state_.command presentDrawable:state_.drawable];finish();state_.drawable=nil;return;}
        endEncoders();command();if(state_.drawable)[state_.command presentDrawable:state_.drawable];state_.drawable=nil;
    }
    uint64_t signalCompletionImpl() override {
        endEncoders();command();auto cmd=state_.command;profileCommit(cmd);[cmd commit];state_.command=nil;const auto serial=++submitted_;pending_[serial]=cmd;return serial;
    }
    bool completionReadyImpl(uint64_t serial) override {
        auto it=pending_.find(serial);if(it==pending_.end())return true;auto cmd=it->second;
        if(cmd.status<MTLCommandBufferStatusCompleted)return false;require(cmd.status!=MTLCommandBufferStatusError,errorText(cmd.error));recordTiming(cmd);pending_.erase(it);return true;
    }
    void waitCompletionImpl(uint64_t serial) override {
        auto it=pending_.find(serial);if(it!=pending_.end()){[it->second waitUntilCompleted];completionReadyImpl(serial);}
    }
    void waitIdleImpl() override { finish();while(!pending_.empty())waitCompletionImpl(pending_.begin()->first); }
    void closeImpl() override {
        if(auto path=std::getenv("SCENERENDERER_GPU_PROFILE_OUTPUT")){json report;report["device"]=state_.device.name.UTF8String;report["supported"]=counterSet_!=nil;report["time_domain"]="Apple GPU counters, host monotonic milliseconds";report["samples"]=json::array();
            for(const auto& s:profileSamples_)report["samples"].push_back({{"label",s.label},{"kind",s.kind},{"start_ms",s.startMilliseconds},{"end_ms",s.endMilliseconds},{"vertex_ms",s.vertexMilliseconds},{"fragment_ms",s.fragmentMilliseconds},{"commit_ms",s.commitMilliseconds},
                {"vertex_start_ms",s.vertexStartMilliseconds},{"vertex_end_ms",s.vertexEndMilliseconds},{"fragment_start_ms",s.fragmentStartMilliseconds},{"fragment_end_ms",s.fragmentEndMilliseconds}});
            std::ofstream(path)<<report.dump(2)<<'\n';}
        savePipelineDiskCacheImpl();archive_=nil;rhiPresentPipeline_=nil;profiles_.clear();profileSamples_.clear();profileCounterPool_.clear();counterSet_=nil;state_=NativeState{};}
private:
    GpuTimingStats gpuTiming_{true,0,0,"Metal command buffer"};
    void recordTiming(id<MTLCommandBuffer> command){resolveProfile(command);if(command.GPUEndTime>=command.GPUStartTime && command.GPUStartTime>0){gpuTiming_.milliseconds=(command.GPUEndTime-command.GPUStartTime)*1000;gpuTiming_.peakMilliseconds=std::max(gpuTiming_.peakMilliseconds,gpuTiming_.milliseconds);++gpuTiming_.samples;}}
    struct PassMarker {std::string label,kind;NSUInteger index,count;};
    struct ProfileBuffer {id<MTLCounterSampleBuffer> buffer=nil;NSUInteger used=0,dropped=0;double commit=0;std::vector<PassMarker> passes;};
    struct SampleLocation {id<MTLCounterSampleBuffer> buffer=nil;NSUInteger index=0;};
    id<MTLCounterSet> counterSet_=nil;
    std::map<uintptr_t,ProfileBuffer> profiles_;
    std::vector<GpuProfileSample> profileSamples_;
    std::vector<id<MTLCounterSampleBuffer>> profileCounterPool_;
    uintptr_t profileKey(id<MTLCommandBuffer> cmd){return reinterpret_cast<uintptr_t>((__bridge void*)cmd);}
    SampleLocation profilePass(const std::string& label,const std::string& kind,NSUInteger count) {
        if(!counterSet_)return {};
        auto& p=profiles_[profileKey(state_.command)];
        if(!p.buffer){
            if(!profileCounterPool_.empty()){p.buffer=profileCounterPool_.back();profileCounterPool_.pop_back();}
            else {auto d=[MTLCounterSampleBufferDescriptor new];d.counterSet=counterSet_;d.storageMode=MTLStorageModeShared;d.sampleCount=4096;NSError* error=nil;p.buffer=[state_.device newCounterSampleBufferWithDescriptor:d error:&error];}
            if(!p.buffer){++p.dropped;return {};}
        }
        if(p.used+count>4096){++p.dropped;return {};}
        auto index=p.used;p.used+=count;p.passes.push_back({label,kind,index,count});return {p.buffer,index};
    }
    id<MTLBlitCommandEncoder> profileBlit(const std::string& label) {
        auto d=[MTLBlitPassDescriptor blitPassDescriptor];auto s=profilePass(label,"blit",2);
        if(s.buffer){auto a=d.sampleBufferAttachments[0];a.sampleBuffer=s.buffer;a.startOfEncoderSampleIndex=s.index;a.endOfEncoderSampleIndex=s.index+1;}
        return [state_.command blitCommandEncoderWithDescriptor:d];
    }
    void profileCommit(id<MTLCommandBuffer> cmd){if(counterSet_)profiles_[profileKey(cmd)].commit=CACurrentMediaTime()*1000.;}
    void resolveProfile(id<MTLCommandBuffer> cmd) {
        auto it=profiles_.find(profileKey(cmd));if(it==profiles_.end())return;
        auto& p=it->second;
        if(cmd.GPUStartTime>0)profileSamples_.push_back({"submission","submission",cmd.GPUStartTime*1000.,cmd.GPUEndTime*1000.,0,0,p.commit});
        if(p.used){NSData* data=[p.buffer resolveCounterRange:NSMakeRange(0,p.used)];require(data.length>=p.used*sizeof(MTLCounterResultTimestamp),"GPU counter resolve failed");auto t=static_cast<const MTLCounterResultTimestamp*>(data.bytes);
            for(const auto& pass:p.passes){auto i=pass.index;bool valid=true;for(NSUInteger j=0;j<pass.count;++j)valid&=t[i+j].timestamp!=MTLCounterErrorValue && t[i+j].timestamp>0;
                if(!valid)continue;double start=t[i].timestamp*1e-6,end=t[i+pass.count-1].timestamp*1e-6;
                if(end<start || start<cmd.GPUStartTime*1000.-.001 || end>cmd.GPUEndTime*1000.+.001)continue;
                if(pass.count==4 && (t[i+1].timestamp<t[i].timestamp || t[i+3].timestamp<t[i+2].timestamp))continue;GpuProfileSample s{pass.label,pass.kind,start,end};if(pass.count==4){s.vertexMilliseconds=(t[i+1].timestamp-t[i].timestamp)*1e-6;s.fragmentMilliseconds=(t[i+3].timestamp-t[i+2].timestamp)*1e-6;s.vertexStartMilliseconds=start;s.vertexEndMilliseconds=t[i+1].timestamp*1e-6;s.fragmentStartMilliseconds=t[i+2].timestamp*1e-6;s.fragmentEndMilliseconds=end;}profileSamples_.push_back(s);}
        }
        if(p.dropped)profileSamples_.push_back({"profile-truncated/"+std::to_string(p.dropped),"diagnostic"});
        // Only completed command buffers return counters to the pool. This
        // also avoids the device's small counter-buffer allocation limit when
        // old autoreleased command buffers still retain their attachments.
        if(p.buffer && profileCounterPool_.size()<8)profileCounterPool_.push_back(p.buffer);
        profiles_.erase(it);
        // An unattended editor session must never grow telemetry without bound.
        if(profileSamples_.size()>32768)profileSamples_.erase(profileSamples_.begin(),profileSamples_.begin()+16384);
    }
    NativeState state_;
    id<MTLBinaryArchive> archive_=nil;std::filesystem::path cachePath_;PipelineDiskCacheStats diskStats_;
    void endEncoders(){} // Native encoders are scoped and ended in each recorded pass.
    void command(){if(!state_.command)state_.command=[state_.queue commandBuffer];}
    void finish(){if(state_.command){profileCommit(state_.command);[state_.command commit];[state_.command waitUntilCompleted];require(state_.command.status!=MTLCommandBufferStatusError,errorText(state_.command.error));recordTiming(state_.command);state_.command=nil;}}
    uint64_t submitted_=0;std::map<uint64_t,id<MTLCommandBuffer>> pending_;
    id<MTLRenderPipelineState> rhiPresentPipeline_=nil;
    struct RHIPipeline {id<MTLRenderPipelineState> pipeline;id<MTLDepthStencilState> depth;std::unordered_map<uint32_t,uint32_t> vertexTextures,fragmentTextures;};
    struct RHIComputePipeline {id<MTLComputePipelineState> pipeline;std::unordered_map<uint32_t,uint32_t> textures;};
    std::unordered_map<NativeObject,id<MTLTexture>> rhiTextures_,rhiViews_;
    std::unordered_map<NativeObject,id<MTLSamplerState>> rhiSamplers_;
    std::unordered_map<NativeObject,RHIPipeline> rhiPipelines_;
    std::unordered_map<NativeObject,RHIComputePipeline> rhiComputePipelines_;
};
} // namespace
std::shared_ptr<GraphicsDevice> makeMetalDevice(GLFWwindow* window){
    NativeState state;state.device=MTLCreateSystemDefaultDevice();require(state.device!=nil,"Metal device unavailable");state.queue=[state.device newCommandQueue];state.window=window;
    if(window){NSWindow* ns=glfwGetCocoaWindow(window);state.layer=[CAMetalLayer layer];state.layer.device=state.device;state.layer.pixelFormat=MTLPixelFormatBGRA8Unorm;state.layer.framebufferOnly=YES;state.layer.maximumDrawableCount=3;ns.contentView.wantsLayer=YES;ns.contentView.layer=state.layer;}
    return std::make_shared<MetalDevice>(std::move(state));
}
} // namespace rhi
