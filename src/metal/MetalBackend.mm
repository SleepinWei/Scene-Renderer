#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <glfw/glfw3.h>
#include <glfw/glfw3native.h>
#include <glad/glad.h>
#include <imgui/imgui.h>
#include <stb/stb_image_write.h>
#include <json/json.hpp>
#include "metal/MetalBackend.h"
#include "metal/MetalDemo.h"
#include <array>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <filesystem>
#include <regex>
#include <stdexcept>
#include <iostream>
#include <cstring>
#include <cmath>
#include <algorithm>
using json = nlohmann::json;
namespace {
void require(bool value,const std::string& message) { if(!value) throw std::runtime_error("Metal: "+message); }
std::string errorText(NSError* error) {return error ? std::string(error.localizedDescription.UTF8String) : "unknown error";}
struct Buffer { id<MTLBuffer> gpu=nil; size_t size=0; };
struct Texture {
    id<MTLTexture> gpu=nil;
    GLenum target=GL_TEXTURE_2D, internal=GL_RGBA;
    int width=0,height=0,layers=1;
    GLenum wrapS=GL_REPEAT,wrapT=GL_REPEAT,min=GL_LINEAR,mag=GL_LINEAR;
    bool mipmapped=true;
};
struct Attribute { unsigned buffer=0; int count=0; GLenum type=GL_FLOAT; unsigned stride=0; size_t offset=0; bool enabled=false; };
struct VertexArray { std::array<Attribute,16> attributes{}; unsigned indices=0; };
struct Attachment { unsigned texture=0; unsigned slice=0; bool layered=false; bool renderbuffer=false; };
struct Framebuffer { std::array<Attachment,8> colors{}; Attachment depth; std::vector<unsigned> draw={0}; };
struct Stage {
    json reflection;
    id<MTLFunction> function=nil;
    id<MTLComputePipelineState> compute=nil;
    std::vector<uint8_t> uniforms;
    std::string name;
};
struct Program {
    Stage vertex,fragment,compute,control,evaluation;
    std::unordered_map<std::string,std::vector<uint8_t>> values;
    std::unordered_map<std::string,unsigned> blocks;
    std::unordered_map<std::string,id<MTLRenderPipelineState>> pipelines;
    int layers=1;
    bool tess=false;
};
struct State {
    id<MTLDevice> device=nil;
    id<MTLCommandQueue> queue=nil;
    id<MTLCommandBuffer> command=nil;
    id<MTLRenderCommandEncoder> render=nil;
    id<MTLComputeCommandEncoder> compute=nil;
    CAMetalLayer* layer=nil;
    id<CAMetalDrawable> drawable=nil;
    id<MTLTexture> screen=nil,screenDepth=nil;
    id<MTLSamplerState> sampler=nil;
    id<MTLBuffer> zero=nil;
    GLFWwindow* window=nullptr;
    int width=1600,height=900;
    unsigned next=1,vao=0,program=0,framebuffer=0,readFramebuffer=0,activeTexture=0,renderbuffer=0;
    std::unordered_map<unsigned,Buffer> buffers;
    std::unordered_map<unsigned,Texture> textures,renderbuffers;
    std::unordered_map<unsigned,VertexArray> vaos;
    std::unordered_map<unsigned,Framebuffer> framebuffers;
    std::unordered_map<unsigned,Program> programs;
    std::unordered_map<GLenum,unsigned> boundBuffers;
    std::array<unsigned,32> uniformBuffers{},storageBuffers{},images{};
    std::array<size_t,32> uniformOffsets{},storageOffsets{};
    std::array<std::unordered_map<GLenum,unsigned>,64> textureUnits;
    std::unordered_map<std::string,id<MTLSamplerState>> samplers;
    std::array<float,4> clear={0,0,0,1};
    MTLViewport viewport={0,0,1600,900,0,1};
    bool depthTest=true,depthWrite=true,cull=true,blend=false;
    GLenum cullMode=GL_BACK,depthFunc=GL_LESS,poly=GL_FILL;
    GLenum blendSrc=GL_SRC_ALPHA,blendDst=GL_ONE_MINUS_SRC_ALPHA;
    id<MTLRenderPipelineState> guiPipeline=nil;
    id<MTLTexture> font=nil;
    std::unordered_map<int,id<MTLDepthStencilState>> depthStates;
};
State s;
void endEncoders() {
    if(s.render) {[s.render endEncoding];s.render=nil;}
    if(s.compute) {[s.compute endEncoding];s.compute=nil;}
}
void command() {if(!s.command) s.command=[s.queue commandBuffer];}
void finish() {
    endEncoders();
    if(s.command) {
        [s.command commit]; [s.command waitUntilCompleted];
        require(s.command.status!=MTLCommandBufferStatusError,errorText(s.command.error));
        s.command=nil;
    }
}
id<MTLTexture> makeTexture(MTLPixelFormat format,int w,int h,int layers=1,MTLTextureType type=MTLTextureType2D,bool mip=false) {
    require(w>0&&h>0,"invalid texture dimensions");
    MTLTextureDescriptor* d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:w height:h mipmapped:mip];
    d.textureType=type; d.arrayLength=type==MTLTextureType2DArray?layers:1;
    d.storageMode=MTLStorageModePrivate;
    d.usage=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite|MTLTextureUsageRenderTarget;
    if(format==MTLPixelFormatDepth32Float || format==MTLPixelFormatBC1_RGBA || format==MTLPixelFormatBC3_RGBA)
        d.usage=MTLTextureUsageShaderRead|(format==MTLPixelFormatDepth32Float?MTLTextureUsageRenderTarget:0);
    id<MTLTexture> t=[s.device newTextureWithDescriptor:d];
    require(t!=nil,"texture allocation failed"); return t;
}
MTLPixelFormat pixelFormat(GLenum format) {
    switch(format) {
        case GL_DEPTH_COMPONENT:case GL_DEPTH_COMPONENT24:case GL_DEPTH_COMPONENT32:case GL_DEPTH_COMPONENT32F:return MTLPixelFormatDepth32Float;
        case GL_R32F:return MTLPixelFormatR32Float;
        case GL_RG32F:return MTLPixelFormatRG32Float;
        case GL_R16F:return MTLPixelFormatR16Float;
        case GL_RG16F:return MTLPixelFormatRG16Float;
        case GL_RGB16F:case GL_RGBA16F:return MTLPixelFormatRGBA16Float;
        case GL_RGB32F:case GL_RGBA32F:return MTLPixelFormatRGBA32Float;
        case GL_RED:case GL_R8:return MTLPixelFormatR8Unorm;
        case GL_RG:case GL_RG8:return MTLPixelFormatRG8Unorm;
        case GL_SRGB:case GL_SRGB_ALPHA:case GL_SRGB8:case GL_SRGB8_ALPHA8:return MTLPixelFormatRGBA8Unorm_sRGB;
        case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:return MTLPixelFormatBC1_RGBA;
        case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:return MTLPixelFormatBC3_RGBA;
        default:return MTLPixelFormatRGBA8Unorm;
    }
}
int channels(GLenum f) {return f==GL_RED||f==GL_DEPTH_COMPONENT?1:f==GL_RG?2:f==GL_RGB?3:4;}
uint16_t halfBits(float value) { _Float16 h=value; uint16_t result;memcpy(&result,&h,2);return result; }
void uploadTexture(Texture& t,int level,int slice,GLenum format,GLenum type,const void* pixels,bool compressed=false) {
    if(!pixels)return;
    endEncoders();command();
    const size_t w=std::max(1,t.width>>level),h=std::max(1,t.height>>level);
    size_t row=0,rows=h;
    std::vector<uint8_t> data;
    if(compressed) {
        size_t block=t.gpu.pixelFormat==MTLPixelFormatBC1_RGBA?8:16;
        row=((w+3)/4)*block;rows=(h+3)/4;
        data.assign((const uint8_t*)pixels,(const uint8_t*)pixels+row*rows);
    } else {
        auto pf=t.gpu.pixelFormat;
        bool float32=pf==MTLPixelFormatR32Float||pf==MTLPixelFormatRG32Float||pf==MTLPixelFormatRGBA32Float;
        bool float16=pf==MTLPixelFormatR16Float||pf==MTLPixelFormatRG16Float||pf==MTLPixelFormatRGBA16Float;
        int dst=(pf==MTLPixelFormatR32Float||pf==MTLPixelFormatR16Float||pf==MTLPixelFormatR8Unorm)?1:
                (pf==MTLPixelFormatRG32Float||pf==MTLPixelFormatRG16Float||pf==MTLPixelFormatRG8Unorm)?2:4;
        int src=channels(format),bytes=float32?4:float16?2:1;
        row=w*dst*bytes;data.resize(row*h);
        for(size_t p=0;p<w*h;p++) for(int c=0;c<dst;c++) {
            float value=c<src?(type==GL_FLOAT?((const float*)pixels)[p*src+c]:((const uint8_t*)pixels)[p*src+c]/255.f):(c==3?1.f:0.f);
            size_t at=(p*dst+c)*bytes;
            if(float32) memcpy(data.data()+at,&value,4);
            else if(float16) {auto v=halfBits(value);memcpy(data.data()+at,&v,2);}
            else data[at]=uint8_t(std::clamp(value,0.f,1.f)*255+.5f);
        }
    }
    const size_t padded=(row+255)&~size_t(255);
    std::vector<uint8_t> staging(padded*rows);
    for(size_t y=0;y<rows;y++)memcpy(staging.data()+y*padded,data.data()+y*row,row);
    id<MTLBuffer> b=[s.device newBufferWithBytes:staging.data() length:staging.size() options:MTLResourceStorageModeShared];
    auto encoder=[s.command blitCommandEncoder];
    [encoder copyFromBuffer:b sourceOffset:0 sourceBytesPerRow:padded sourceBytesPerImage:padded*rows sourceSize:MTLSizeMake(w,h,1) toTexture:t.gpu destinationSlice:slice destinationLevel:level destinationOrigin:MTLOriginMake(0,0,0)];
    [encoder endEncoding];
}
unsigned boundTexture(GLenum target) {
    if(target>=GL_TEXTURE_CUBE_MAP_POSITIVE_X&&target<=GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)target=GL_TEXTURE_CUBE_MAP;
    return s.textureUnits.at(s.activeTexture)[target];
}
Texture& attached(const Attachment& a) {return a.renderbuffer?s.renderbuffers.at(a.texture):s.textures.at(a.texture);}
Framebuffer& framebuffer() {return s.framebuffers[s.framebuffer];}
id<MTLTexture> attachmentTexture(const Attachment& a) {return a.texture?attached(a).gpu:nil;}
MTLRenderPassDescriptor* pass(int layer=0,GLbitfield clear=0) {
    MTLRenderPassDescriptor* d=[MTLRenderPassDescriptor renderPassDescriptor];
    if(!s.framebuffer) {
        require(s.screen!=nil,"beginFrame must precede drawing");
        d.colorAttachments[0].texture=s.screen;d.depthAttachment.texture=s.screenDepth;
    } else {
        auto& f=framebuffer();
        for(unsigned i:f.draw)if(i<8) {
            auto& a=f.colors[i];d.colorAttachments[i].texture=attachmentTexture(a);
            d.colorAttachments[i].slice=a.layered?layer:a.slice;
        }
        d.depthAttachment.texture=attachmentTexture(f.depth);d.depthAttachment.slice=f.depth.layered?layer:f.depth.slice;
    }
    for(int i=0;i<8;i++)if(d.colorAttachments[i].texture) {
        d.colorAttachments[i].loadAction=clear&GL_COLOR_BUFFER_BIT?MTLLoadActionClear:MTLLoadActionLoad;
        d.colorAttachments[i].storeAction=MTLStoreActionStore;
        d.colorAttachments[i].clearColor=MTLClearColorMake(s.clear[0],s.clear[1],s.clear[2],s.clear[3]);
    }
    d.depthAttachment.loadAction=clear&GL_DEPTH_BUFFER_BIT?MTLLoadActionClear:MTLLoadActionLoad;
    d.depthAttachment.storeAction=MTLStoreActionStore;d.depthAttachment.clearDepth=1;
    return d;
}
void beginRender(int layer=0) {
    if(s.render)return;
    endEncoders();command();s.render=[s.command renderCommandEncoderWithDescriptor:pass(layer)];
    require(s.render!=nil,"render encoder creation failed");
}
void beginCompute() {endEncoders();command();s.compute=[s.command computeCommandEncoder];}
std::filesystem::path shaderPath(const char* path,const std::string& suffix) {
    auto p=std::filesystem::path(path).lexically_normal();std::string relative=p.generic_string();
    auto at=relative.find("src/shader/");require(at!=std::string::npos,"unknown shader path: "+relative);
    return std::filesystem::path(SR_METAL_SHADER_DIR)/(relative.substr(at+11)+suffix);
}
Stage loadStage(const char* path,const std::string& variant="") {
    Stage result;if(!path)return result;
    auto p=shaderPath(path,variant+".json");std::ifstream f(p);require(bool(f),"missing shader reflection "+p.string());
    f>>result.reflection;result.name=path;
    for(auto& u:result.reflection.value("ubos",json::array()))if(u["name"]=="DefaultUniforms")result.uniforms.resize(std::max(16,(int(u["block_size"])+15)&~15),0);
    auto libPath=shaderPath(path,variant+".metallib");NSError* error=nil;
    id<MTLLibrary> lib=[s.device newLibraryWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:libPath.c_str()]] error:&error];
    require(lib!=nil,result.name+": "+errorText(error));result.function=[lib newFunctionWithName:@"main0"];
    require(result.function!=nil,"entry point absent: "+result.name);
    if(result.reflection["metal_stage"]=="comp") {
        result.compute=[s.device newComputePipelineStateWithFunction:result.function error:&error];
        // Capture VS requires a stage-input descriptor, configured per mesh.
        if(variant.empty())require(result.compute!=nil,result.name+": "+errorText(error));
    }
    return result;
}
void flattenUniforms(const json& types,const json& members,const std::string& prefix,size_t base,
                    const std::unordered_map<std::string,std::vector<uint8_t>>& values,std::vector<uint8_t>& out) {
    for(auto& m:members) {
        auto name=prefix+m["name"].get<std::string>();size_t offset=base+m.value("offset",0);
        std::string type=m["type"];
        size_t count=m.contains("array")?m["array"][0].get<size_t>():1;
        for(size_t a=0;a<count;a++) {
            auto key=name+(m.contains("array")?"["+std::to_string(a)+"]":"");
            auto dst=offset+a*m.value("array_stride",0);
            if(types.contains(type)) flattenUniforms(types,types[type]["members"],key+".",dst,values,out);
            else if(auto it=values.find(key);it!=values.end()) {
                auto& data=it->second;
                if(type.rfind("mat",0)==0&&m.contains("matrix_stride")) {
                    size_t dim=std::stoul(type.substr(3));size_t stride=m["matrix_stride"];
                    for(size_t c=0;c<dim;c++)if(dst+c*stride+dim*4<=out.size())memcpy(out.data()+dst+c*stride,data.data()+c*dim*4,dim*4);
                } else if(dst+data.size()<=out.size())memcpy(out.data()+dst,data.data(),data.size());
            }
        }
    }
}
unsigned textureUnit(const Program& p,const std::string& name) {
    auto it=p.values.find(name);if(it==p.values.end())return 0;
    unsigned unit=0;memcpy(&unit,it->second.data(),4);require(unit<64,"texture unit exceeds limit");return unit;
}
GLenum resourceTarget(const std::string& type) {
    if(type.find("Cube")!=std::string::npos)return GL_TEXTURE_CUBE_MAP;
    if(type.find("Array")!=std::string::npos)return GL_TEXTURE_2D_ARRAY;
    return GL_TEXTURE_2D;
}
id<MTLSamplerState> textureSampler(const Texture& tex) {
    std::string key=std::to_string(tex.wrapS)+":"+std::to_string(tex.wrapT)+":"+std::to_string(tex.min)+":"+std::to_string(tex.mag);
    if(s.samplers.count(key))return s.samplers.at(key);
    MTLSamplerDescriptor* d=[MTLSamplerDescriptor new];
    d.minFilter=tex.min==GL_NEAREST?MTLSamplerMinMagFilterNearest:MTLSamplerMinMagFilterLinear;
    d.magFilter=tex.mag==GL_NEAREST?MTLSamplerMinMagFilterNearest:MTLSamplerMinMagFilterLinear;
    d.mipFilter=tex.min==GL_LINEAR_MIPMAP_LINEAR?MTLSamplerMipFilterLinear:MTLSamplerMipFilterNotMipmapped;
    auto address=[](GLenum e) {return e==GL_REPEAT?MTLSamplerAddressModeRepeat:e==GL_MIRRORED_REPEAT?MTLSamplerAddressModeMirrorRepeat:e==GL_CLAMP_TO_BORDER?MTLSamplerAddressModeClampToBorderColor:MTLSamplerAddressModeClampToEdge;};
    d.sAddressMode=address(tex.wrapS);d.tAddressMode=address(tex.wrapT);d.rAddressMode=d.tAddressMode;
    d.borderColor=MTLSamplerBorderColorOpaqueWhite;
    auto sampler=[s.device newSamplerStateWithDescriptor:d];s.samplers[key]=sampler;return sampler;
}
void bindStage(Stage& stage,Program& p,int target) {
    if(!stage.function)return;
    auto bindBuffer=[&](id<MTLBuffer> b,size_t offset,unsigned index) {
        if(target==0)[s.render setVertexBuffer:b offset:offset atIndex:index];
        else if(target==1)[s.render setFragmentBuffer:b offset:offset atIndex:index];
        else [s.compute setBuffer:b offset:offset atIndex:index];
    };
    auto bindBytes=[&](const void* data,size_t size,unsigned index) {
        if(target==0)[s.render setVertexBytes:data length:size atIndex:index];
        else if(target==1)[s.render setFragmentBytes:data length:size atIndex:index];
        else [s.compute setBytes:data length:size atIndex:index];
    };
    for(auto kind:{"ubos","ssbos"})for(auto& r:stage.reflection.value(kind,json::array())) {
        if(r.value("inactive",false))continue;
        unsigned index=r["msl_index"],binding=r["binding"];std::string name=r["name"];
        if(name=="DefaultUniforms") {
            std::fill(stage.uniforms.begin(),stage.uniforms.end(),0);
            flattenUniforms(stage.reflection["types"],stage.reflection["types"][r["type"]]["members"],"",0,p.values,stage.uniforms);
            bindBytes(stage.uniforms.data(),stage.uniforms.size(),index);
        } else {
            bool u=std::string(kind)=="ubos";if(u&&p.blocks.count(name))binding=p.blocks[name];
            require(binding<32,"buffer binding exceeds limit");
            unsigned id=u?s.uniformBuffers[binding]:s.storageBuffers[binding];
            size_t offset=u?s.uniformOffsets[binding]:s.storageOffsets[binding];
            bindBuffer(id?s.buffers.at(id).gpu:s.zero,offset,index);
        }
    }
    for(auto kind:{"textures","images"})for(auto& r:stage.reflection.value(kind,json::array())) {
        if(r.value("inactive",false))continue;
        unsigned index=r["msl_index"];
        id<MTLSamplerState> sampler=s.sampler;
        unsigned count=r.contains("array")?r["array"][0].get<unsigned>():1;
        std::string name=r["name"],type=r["type"];
        for(unsigned a=0;a<count;a++) {
            unsigned textureID=0;
            if(std::string(kind)=="images")textureID=s.images.at(r["binding"].get<unsigned>());
            else {
                auto key=name+(r.contains("array")?"["+std::to_string(a)+"]":"");
                unsigned unit=textureUnit(p,key);auto textureTarget=resourceTarget(type);textureID=s.textureUnits[unit][textureTarget];
            }
            id<MTLTexture> texture=textureID?s.textures.at(textureID).gpu:nil;
            if(textureID&&std::string(kind)=="textures")sampler=textureSampler(s.textures.at(textureID));
            if(target==0)[s.render setVertexTexture:texture atIndex:index+a];
            else if(target==1)[s.render setFragmentTexture:texture atIndex:index+a];
            else [s.compute setTexture:texture atIndex:index+a];
        }
        if(std::string(kind)=="textures") {
            std::string key=std::regex_replace(name,std::regex("[^A-Za-z0-9_]"),"_");
            unsigned samplerIndex=stage.reflection["samplers"].value(key,0);
    if(target==0)[s.render setVertexSamplerState:sampler atIndex:samplerIndex];
    else if(target==1)[s.render setFragmentSamplerState:sampler atIndex:samplerIndex];
    else [s.compute setSamplerState:sampler atIndex:samplerIndex];
        }
    }
}
MTLVertexFormat vertexFormat(const Attribute& a) {
    require(a.type==GL_FLOAT,"only float vertex attributes are used by this renderer");
    return a.count==1?MTLVertexFormatFloat:a.count==2?MTLVertexFormatFloat2:a.count==3?MTLVertexFormatFloat3:MTLVertexFormatFloat4;
}
MTLVertexDescriptor* vertexDescriptor(Stage& stage) {
    MTLVertexDescriptor* d=[MTLVertexDescriptor vertexDescriptor];auto& v=s.vaos[s.vao];
    for(auto& input:stage.reflection.value("inputs",json::array())) {
        unsigned index=input["location"];auto& a=v.attributes.at(index);
        require(a.enabled,"missing vertex attribute "+std::to_string(index)+" for "+stage.name);
        d.attributes[index].format=vertexFormat(a);d.attributes[index].offset=a.offset;d.attributes[index].bufferIndex=16+index;
        d.layouts[16+index].stride=a.stride?a.stride:a.count*4;d.layouts[16+index].stepFunction=MTLVertexStepFunctionPerVertex;
    }
    return d;
}
MTLBlendFactor blendFactor(GLenum e) {
    switch(e) {case GL_ONE:return MTLBlendFactorOne;case GL_ZERO:return MTLBlendFactorZero;case GL_SRC_ALPHA:return MTLBlendFactorSourceAlpha;case GL_ONE_MINUS_SRC_ALPHA:return MTLBlendFactorOneMinusSourceAlpha;default:throw std::runtime_error("unsupported blend factor");}
}
id<MTLRenderPipelineState> pipeline(Program& p) {
    auto desc=pass();std::string key=std::to_string(s.vao)+":"+std::to_string(s.blend)+":"+std::to_string(s.blendSrc)+":"+std::to_string(s.blendDst);
    for(int i=0;i<8;i++)key+=":"+std::to_string(desc.colorAttachments[i].texture.pixelFormat);
    key+=":"+std::to_string(desc.depthAttachment.texture.pixelFormat);
    if(p.pipelines.count(key))return p.pipelines.at(key);
    MTLRenderPipelineDescriptor* d=[MTLRenderPipelineDescriptor new];
    d.vertexFunction=p.tess?p.evaluation.function:p.vertex.function;d.fragmentFunction=p.fragment.function;
    if(!p.tess)d.vertexDescriptor=vertexDescriptor(p.vertex);
    d.depthAttachmentPixelFormat=desc.depthAttachment.texture.pixelFormat;
    for(int i=0;i<8;i++) {
        d.colorAttachments[i].pixelFormat=desc.colorAttachments[i].texture.pixelFormat;
        d.colorAttachments[i].blendingEnabled=s.blend;
        d.colorAttachments[i].sourceRGBBlendFactor=blendFactor(s.blendSrc);d.colorAttachments[i].destinationRGBBlendFactor=blendFactor(s.blendDst);
        d.colorAttachments[i].sourceAlphaBlendFactor=MTLBlendFactorOne;d.colorAttachments[i].destinationAlphaBlendFactor=MTLBlendFactorOneMinusSourceAlpha;
    }
    if(p.tess) {
        d.maxTessellationFactor=64;d.tessellationFactorFormat=MTLTessellationFactorFormatHalf;
        d.tessellationFactorStepFunction=MTLTessellationFactorStepFunctionPerPatch;d.tessellationControlPointIndexType=MTLTessellationControlPointIndexTypeNone;
        d.tessellationPartitionMode=p.evaluation.reflection["control_points"]==3?MTLTessellationPartitionModeInteger:MTLTessellationPartitionModeFractionalOdd;
        d.tessellationOutputWindingOrder=MTLWindingCounterClockwise;
    }
    NSError* error=nil;auto result=[s.device newRenderPipelineStateWithDescriptor:d error:&error];
    require(result!=nil,p.vertex.name+" + "+p.fragment.name+": "+errorText(error));p.pipelines[key]=result;return result;
}
void renderState() {
    [s.render setViewport:s.viewport];
    bool flip=s.framebuffer!=0;
    [s.render setFrontFacingWinding:flip?MTLWindingClockwise:MTLWindingCounterClockwise];
    [s.render setCullMode:!s.cull?MTLCullModeNone:s.cullMode==GL_BACK?MTLCullModeBack:MTLCullModeFront];
    [s.render setTriangleFillMode:s.poly==GL_LINE?MTLTriangleFillModeLines:MTLTriangleFillModeFill];
    bool hasDepth=pass().depthAttachment.texture!=nil;
    int key=int(s.depthFunc)*8+int(s.depthTest)*4+int(s.depthWrite)*2+int(hasDepth);
    if(!s.depthStates.count(key)) {
        MTLDepthStencilDescriptor* d=[MTLDepthStencilDescriptor new];d.depthWriteEnabled=s.depthTest&&s.depthWrite&&hasDepth;
        d.depthCompareFunction=(!s.depthTest||!hasDepth)?MTLCompareFunctionAlways:s.depthFunc==GL_LEQUAL?MTLCompareFunctionLessEqual:s.depthFunc==GL_ALWAYS?MTLCompareFunctionAlways:MTLCompareFunctionLess;
        s.depthStates[key]=[s.device newDepthStencilStateWithDescriptor:d];
    }
    [s.render setDepthStencilState:s.depthStates[key]];
    float flipValue=flip?-1.f:1.f;[s.render setVertexBytes:&flipValue length:4 atIndex:27];
}
void bindVertices(Stage& stage) {
    auto& v=s.vaos[s.vao];for(auto& input:stage.reflection.value("inputs",json::array())) {
        unsigned index=input["location"];auto& a=v.attributes[index];[s.render setVertexBuffer:s.buffers.at(a.buffer).gpu offset:0 atIndex:16+index];
    }
}
MTLPrimitiveType primitive(GLenum mode) {
    switch(mode) {case GL_TRIANGLES:return MTLPrimitiveTypeTriangle;case GL_TRIANGLE_STRIP:return MTLPrimitiveTypeTriangleStrip;case GL_POINTS:return MTLPrimitiveTypePoint;case GL_LINES:return MTLPrimitiveTypeLine;default:throw std::runtime_error("unsupported primitive");}
}
void tessDraw(Program& p,unsigned count,bool indexed,size_t offset) {
    endEncoders();unsigned points=p.evaluation.reflection["control_points"],patches=count/points;
    if(!patches)return;
    constexpr unsigned stride=96; // Largest captured structure (Object + gl_PerVertex).
    id<MTLBuffer> captured=[s.device newBufferWithLength:count*stride options:MTLResourceStorageModePrivate];
    id<MTLBuffer> controlled=[s.device newBufferWithLength:count*stride options:MTLResourceStorageModePrivate];
    id<MTLBuffer> factors=[s.device newBufferWithLength:patches*(points==3?8:12) options:MTLResourceStorageModePrivate];
    // Stage-in indexed fetch runs the original VS as a compute kernel.
    MTLComputePipelineDescriptor* d=[MTLComputePipelineDescriptor new];d.computeFunction=p.vertex.function;
    auto vd=vertexDescriptor(p.vertex);MTLStageInputOutputDescriptor* input=[MTLStageInputOutputDescriptor stageInputOutputDescriptor];
    for(auto& a:p.vertex.reflection.value("inputs",json::array())) {
        unsigned i=a["location"],bi=16+i;input.attributes[i].format=static_cast<MTLAttributeFormat>(vd.attributes[i].format);input.attributes[i].offset=vd.attributes[i].offset;input.attributes[i].bufferIndex=bi;
        input.layouts[bi].stride=vd.layouts[bi].stride;input.layouts[bi].stepFunction=indexed?MTLStepFunctionThreadPositionInGridXIndexed:MTLStepFunctionThreadPositionInGridX;
    }
    input.indexBufferIndex=23;input.indexType=MTLIndexTypeUInt32;d.stageInputDescriptor=input;
    NSError* error=nil;id<MTLComputePipelineState> capture=[s.device newComputePipelineStateWithDescriptor:d options:MTLPipelineOptionNone reflection:nil error:&error];
    require(capture!=nil,errorText(error));beginCompute();[s.compute setComputePipelineState:capture];bindStage(p.vertex,p,2);
    for(auto& a:p.vertex.reflection.value("inputs",json::array())) {unsigned i=a["location"];[s.compute setBuffer:s.buffers.at(s.vaos[s.vao].attributes[i].buffer).gpu offset:0 atIndex:16+i];}
    if(indexed)[s.compute setBuffer:s.buffers.at(s.vaos[s.vao].indices).gpu offset:offset atIndex:23];
    [s.compute setBuffer:captured offset:0 atIndex:28];[s.compute setStageInRegion:MTLRegionMake1D(0,count)];
    [s.compute dispatchThreads:MTLSizeMake(count,1,1) threadsPerThreadgroup:MTLSizeMake(std::min(count,64u),1,1)];
    beginCompute();[s.compute setComputePipelineState:p.control.compute];bindStage(p.control,p,2);
    unsigned params[2]={points,patches};[s.compute setBytes:params length:8 atIndex:29];[s.compute setBuffer:captured offset:0 atIndex:22];
    [s.compute setBuffer:controlled offset:0 atIndex:28];[s.compute setBuffer:factors offset:0 atIndex:26];
    unsigned group=points*std::min(16u,std::max(1u,(unsigned)p.control.compute.maxTotalThreadsPerThreadgroup/points));
    [s.compute dispatchThreads:MTLSizeMake(count,1,1) threadsPerThreadgroup:MTLSizeMake(group,1,1)];
    endEncoders();
    beginRender();[s.render setRenderPipelineState:pipeline(p)];renderState();[s.render setCullMode:MTLCullModeNone];bindStage(p.evaluation,p,0);bindStage(p.fragment,p,1);
    [s.render setVertexBuffer:controlled offset:0 atIndex:22];[s.render setTessellationFactorBuffer:factors offset:0 instanceStride:0];
    [s.render drawPatches:points patchStart:0 patchCount:patches patchIndexBuffer:nil patchIndexBufferOffset:0 instanceCount:1 baseInstance:0];
}
void draw(GLenum mode,unsigned count,bool indexed=false,size_t offset=0,unsigned first=0,id<MTLBuffer> indirect=nil,size_t indirectOffset=0) {
    if(!count&&!indirect)return;auto& p=s.programs.at(s.program);
    if(mode==GL_PATCHES) {require(p.tess,"patch draw without tessellation shaders");tessDraw(p,count,indexed,offset);return;}
    for(int layer=0;layer<p.layers;layer++) {
        if(p.layers>1) {endEncoders();int value=layer;MetalBackend::setUniform(s.program,"srLayer",&value,1);}
        beginRender(layer);s.render.label=[NSString stringWithUTF8String:p.fragment.name.c_str()];[s.render setRenderPipelineState:pipeline(p)];renderState();
        if(p.vertex.name.find("hdr.vs")!=std::string::npos||p.vertex.name.find("deferred/deferred.vs")!=std::string::npos||p.vertex.name.find("ssao/ssao.vs")!=std::string::npos)[s.render setCullMode:MTLCullModeNone];
        bindStage(p.vertex,p,0);bindStage(p.fragment,p,1);bindVertices(p.vertex);
        if(indirect) [s.render drawPrimitives:primitive(mode) indirectBuffer:indirect indirectBufferOffset:indirectOffset];
        else if(indexed)[s.render drawIndexedPrimitives:primitive(mode) indexCount:count indexType:MTLIndexTypeUInt32 indexBuffer:s.buffers.at(s.vaos[s.vao].indices).gpu indexBufferOffset:offset];
        else [s.render drawPrimitives:primitive(mode) vertexStart:first vertexCount:count];
    }
}
void dispatch(unsigned x,unsigned y,unsigned z,id<MTLBuffer> indirect=nil,size_t offset=0) {
    auto& p=s.programs.at(s.program);require(p.compute.compute!=nil,"not a compute program");beginCompute();
    [s.compute setComputePipelineState:p.compute.compute];bindStage(p.compute,p,2);
    auto size=p.compute.reflection["entryPoints"][0]["workgroup_size"];
    MTLSize threads=MTLSizeMake(size[0].get<unsigned>(),size[1].get<unsigned>(),size[2].get<unsigned>());
    if(indirect)[s.compute dispatchThreadgroupsWithIndirectBuffer:indirect indirectBufferOffset:offset threadsPerThreadgroup:threads];
    else if(x&&y&&z)[s.compute dispatchThreadgroups:MTLSizeMake(x,y,z) threadsPerThreadgroup:threads];
}
// These entry points preserve the existing component interface while replacing
// every GPU operation with Metal. No GL context or driver entry point is loaded.
void api_glGenBuffers(GLsizei n,GLuint* ids) {for(int i=0;i<n;i++){ids[i]=s.next++;s.buffers[ids[i]]={};}}
void api_glBindBuffer(GLenum target,GLuint id) {s.boundBuffers[target]=id;if(target==GL_ELEMENT_ARRAY_BUFFER)s.vaos[s.vao].indices=id;}
void api_glBufferData(GLenum target,GLsizeiptr size,const void* data,GLenum) {
    require(size>=0,"negative buffer size");unsigned id=s.boundBuffers[target];auto& b=s.buffers.at(id);b.size=size;
    b.gpu=[s.device newBufferWithLength:std::max<GLsizeiptr>((size+15)&~GLsizeiptr(15),16) options:MTLResourceStorageModeShared];
    if(data&&size)memcpy(b.gpu.contents,data,size);
    require(b.gpu!=nil,"buffer allocation failed");
}
void api_glBufferSubData(GLenum target,GLintptr offset,GLsizeiptr size,const void* data) {
    if(!size)return;auto& b=s.buffers.at(s.boundBuffers[target]);require(offset>=0&&size>=0&&size_t(offset+size)<=b.size,"buffer update out of bounds");
    endEncoders();command();auto staging=[s.device newBufferWithBytes:data length:size options:MTLResourceStorageModeShared];
    auto e=[s.command blitCommandEncoder];[e copyFromBuffer:staging sourceOffset:0 toBuffer:b.gpu destinationOffset:offset size:size];[e endEncoding];
}
void api_glBindBufferRange(GLenum target,GLuint binding,GLuint id,GLintptr offset,GLsizeiptr) {
    if(target==GL_UNIFORM_BUFFER){s.uniformBuffers.at(binding)=id;s.uniformOffsets.at(binding)=offset;}
    else if(target==GL_SHADER_STORAGE_BUFFER){s.storageBuffers.at(binding)=id;s.storageOffsets.at(binding)=offset;}
    else throw std::runtime_error("unsupported indexed buffer target");
}
void api_glBindBufferBase(GLenum target,GLuint binding,GLuint id){api_glBindBufferRange(target,binding,id,0,0);}
void api_glDeleteBuffers(GLsizei n,const GLuint* ids){for(int i=0;i<n;i++)s.buffers.erase(ids[i]);}
void* api_glMapBuffer(GLenum target,GLenum){finish();return s.buffers.at(s.boundBuffers[target]).gpu.contents;}
GLboolean api_glUnmapBuffer(GLenum){return GL_TRUE;}
void api_glGenVertexArrays(GLsizei n,GLuint* ids){for(int i=0;i<n;i++){ids[i]=s.next++;s.vaos[ids[i]]={};}}
void api_glBindVertexArray(GLuint id){s.vao=id;s.boundBuffers[GL_ELEMENT_ARRAY_BUFFER]=s.vaos[id].indices;}
void api_glDeleteVertexArrays(GLsizei n,const GLuint* ids){for(int i=0;i<n;i++)s.vaos.erase(ids[i]);}
void api_glVertexAttribPointer(GLuint index,GLint size,GLenum type,GLboolean normalized,GLsizei stride,const void* offset) {
    require(!normalized,"normalized attributes aren't used by this renderer");auto& a=s.vaos[s.vao].attributes.at(index);
    a.buffer=s.boundBuffers[GL_ARRAY_BUFFER];a.count=size;a.type=type;a.stride=stride;a.offset=(size_t)offset;
}
void api_glEnableVertexAttribArray(GLuint i){s.vaos[s.vao].attributes.at(i).enabled=true;}
void api_glGenTextures(GLsizei n,GLuint* ids){for(int i=0;i<n;i++){ids[i]=s.next++;s.textures[ids[i]]={};}}
void api_glActiveTexture(GLenum unit){require(unit>=GL_TEXTURE0&&unit<GL_TEXTURE0+64,"bad texture unit");s.activeTexture=unit-GL_TEXTURE0;}
void api_glBindTexture(GLenum target,GLuint id){s.textureUnits[s.activeTexture][target]=id;if(id)s.textures.at(id).target=target;}
void api_glDeleteTextures(GLsizei n,const GLuint* ids) {
    for(int i=0;i<n;i++) {
        for(auto& unit:s.textureUnits)for(auto& binding:unit)if(binding.second==ids[i])binding.second=0;
        for(auto& image:s.images)if(image==ids[i])image=0;
        s.textures.erase(ids[i]);
    }
}
void texImage(GLenum target,int level,GLenum internal,int w,int h,int layers,GLenum format,GLenum type,const void* pixels,bool compressed=false) {
    auto& t=s.textures.at(boundTexture(target));
    if(!t.gpu || (level==0&&(t.width!=w||t.height!=h||t.internal!=internal))) {
        t.width=w;t.height=h;t.layers=layers;t.internal=internal;
        MTLTextureType mt=t.target==GL_TEXTURE_CUBE_MAP?MTLTextureTypeCube:t.target==GL_TEXTURE_2D_ARRAY?MTLTextureType2DArray:MTLTextureType2D;
        bool mip=pixelFormat(internal)!=MTLPixelFormatDepth32Float&&mt!=MTLTextureType2DArray;
        t.gpu=makeTexture(pixelFormat(internal),w,h,layers,mt,mip);
    }
    int slice=(target>=GL_TEXTURE_CUBE_MAP_POSITIVE_X&&target<=GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)?target-GL_TEXTURE_CUBE_MAP_POSITIVE_X:0;
    uploadTexture(t,level,slice,format,type,pixels,compressed);
}
void api_glTexImage2D(GLenum target,GLint level,GLint internal,GLsizei w,GLsizei h,GLint border,GLenum format,GLenum type,const void* data) {
    require(border==0,"texture borders unsupported");texImage(target,level,internal,w,h,1,format,type,data);
}
void api_glTexImage3D(GLenum target,GLint level,GLint internal,GLsizei w,GLsizei h,GLsizei depth,GLint,GLenum format,GLenum type,const void* data) {
    require(!data,"array textures are render targets in this renderer");texImage(target,level,internal,w,h,depth,format,type,data);
}
void api_glCompressedTexImage2D(GLenum target,GLint level,GLenum internal,GLsizei w,GLsizei h,GLint,GLsizei,const void* data){texImage(target,level,internal,w,h,1,GL_RGBA,GL_UNSIGNED_BYTE,data,true);}
void api_glTexParameteri(GLenum target,GLenum key,GLint value) {
    auto& t=s.textures.at(boundTexture(target));
    switch(key){case GL_TEXTURE_WRAP_S:t.wrapS=value;break;case GL_TEXTURE_WRAP_T:t.wrapT=value;break;case GL_TEXTURE_WRAP_R:break;case GL_TEXTURE_MIN_FILTER:t.min=value;break;case GL_TEXTURE_MAG_FILTER:t.mag=value;break;default:throw std::runtime_error("unsupported texture parameter");}
}
void api_glTexParameterfv(GLenum,GLenum key,const GLfloat*){require(key==GL_TEXTURE_BORDER_COLOR,"unsupported float texture parameter");}
void api_glGenerateMipmap(GLenum target) {
    auto& t=s.textures.at(boundTexture(target));if(t.gpu.mipmapLevelCount<2)return;
    endEncoders();command();auto e=[s.command blitCommandEncoder];[e generateMipmapsForTexture:t.gpu];[e endEncoding];
}
void api_glBindImageTexture(GLuint binding,GLuint texture,GLint level,GLboolean layered,GLint layer,GLenum,GLenum) {
    require(level==0&&!layered&&layer==0,"this renderer uses only level-zero 2D images");s.images.at(binding)=texture;
}
void api_glGenFramebuffers(GLsizei n,GLuint* ids){for(int i=0;i<n;i++){ids[i]=s.next++;s.framebuffers[ids[i]]={};}}
void api_glBindFramebuffer(GLenum target,GLuint id) {
    endEncoders();if(target==GL_READ_FRAMEBUFFER)s.readFramebuffer=id;
    else {s.framebuffer=id;if(target==GL_FRAMEBUFFER)s.readFramebuffer=id;}
}
void api_glDeleteFramebuffers(GLsizei n,const GLuint* ids){for(int i=0;i<n;i++)s.framebuffers.erase(ids[i]);}
void attach(GLenum attachment,unsigned id,unsigned slice,bool layered,bool rb=false) {
    endEncoders();Attachment a{id,slice,layered,rb};
    if(attachment==GL_DEPTH_ATTACHMENT)framebuffer().depth=a;
    else {require(attachment>=GL_COLOR_ATTACHMENT0&&attachment<GL_COLOR_ATTACHMENT0+8,"unsupported attachment");framebuffer().colors[attachment-GL_COLOR_ATTACHMENT0]=a;}
}
void api_glFramebufferTexture(GLenum,GLenum attachment,GLuint id,GLint level){require(level==0,"nonzero attachment level");attach(attachment,id,0,true);}
void api_glFramebufferTexture2D(GLenum,GLenum attachment,GLenum target,GLuint id,GLint level){require(level==0,"nonzero attachment level");attach(attachment,id,target>=GL_TEXTURE_CUBE_MAP_POSITIVE_X&&target<=GL_TEXTURE_CUBE_MAP_NEGATIVE_Z?target-GL_TEXTURE_CUBE_MAP_POSITIVE_X:0,false);}
void api_glDrawBuffers(GLsizei n,const GLenum* attachments) {endEncoders();framebuffer().draw.clear();for(int i=0;i<n;i++)if(attachments[i]!=GL_NONE)framebuffer().draw.push_back(attachments[i]-GL_COLOR_ATTACHMENT0);}
void api_glDrawBuffer(GLenum a){api_glDrawBuffers(1,&a);}
void api_glReadBuffer(GLenum a){require(a==GL_NONE||a==GL_COLOR_ATTACHMENT0,"unsupported read attachment");}
GLenum api_glCheckFramebufferStatus(GLenum) {
    auto& f=framebuffer();if(!f.depth.texture&&std::none_of(f.colors.begin(),f.colors.end(),[](auto& a){return a.texture;}))return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    return GL_FRAMEBUFFER_COMPLETE;
}
void api_glGenRenderbuffers(GLsizei n,GLuint* ids){for(int i=0;i<n;i++){ids[i]=s.next++;s.renderbuffers[ids[i]]={};}}
void api_glBindRenderbuffer(GLenum,GLuint id){s.renderbuffer=id;}
void api_glRenderbufferStorage(GLenum,GLenum internal,GLsizei w,GLsizei h){auto& t=s.renderbuffers.at(s.renderbuffer);t.width=w;t.height=h;t.gpu=makeTexture(pixelFormat(internal),w,h);}
void api_glFramebufferRenderbuffer(GLenum,GLenum attachment,GLenum,GLuint id){attach(attachment,id,0,false,true);}
void api_glDeleteRenderbuffers(GLsizei n,const GLuint* ids){for(int i=0;i<n;i++)s.renderbuffers.erase(ids[i]);}
void api_glBlitFramebuffer(GLint x0,GLint y0,GLint x1,GLint y1,GLint dx0,GLint dy0,GLint dx1,GLint dy1,GLbitfield mask,GLenum) {
    require(x0==dx0&&y0==dy0&&x1==dx1&&y1==dy1,"scaled blits aren't used by this renderer");endEncoders();command();
    auto& from=s.framebuffers.at(s.readFramebuffer);auto& to=framebuffer();auto e=[s.command blitCommandEncoder];
    if(mask&GL_DEPTH_BUFFER_BIT) [e copyFromTexture:attachmentTexture(from.depth) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(x0,y0,0) sourceSize:MTLSizeMake(x1-x0,y1-y0,1) toTexture:s.framebuffer?attachmentTexture(to.depth):s.screenDepth destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(dx0,dy0,0)];
    if(mask&GL_COLOR_BUFFER_BIT) [e copyFromTexture:attachmentTexture(from.colors[0]) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(x0,y0,0) sourceSize:MTLSizeMake(x1-x0,y1-y0,1) toTexture:s.framebuffer?attachmentTexture(to.colors[0]):s.screen destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(dx0,dy0,0)];
    [e endEncoding];
}
void api_glClearColor(GLfloat r,GLfloat g,GLfloat b,GLfloat a){s.clear={r,g,b,a};}
void api_glClear(GLbitfield mask) {
    endEncoders();command();int layers=1;
    if(s.framebuffer&&framebuffer().depth.layered){auto& t=attached(framebuffer().depth);layers=t.target==GL_TEXTURE_CUBE_MAP?6:t.layers;}
    for(int i=0;i<layers;i++){auto e=[s.command renderCommandEncoderWithDescriptor:pass(i,mask)];[e endEncoding];}
}
void api_glViewport(GLint x,GLint y,GLsizei w,GLsizei h){s.viewport=MTLViewport{double(x),double(y),double(w),double(h),0,1};}
void api_glEnable(GLenum e){switch(e){case GL_DEPTH_TEST:s.depthTest=true;break;case GL_CULL_FACE:s.cull=true;break;case GL_BLEND:s.blend=true;break;case GL_PROGRAM_POINT_SIZE:break;default:throw std::runtime_error("unsupported enable state");}}
void api_glDisable(GLenum e){switch(e){case GL_DEPTH_TEST:s.depthTest=false;break;case GL_CULL_FACE:s.cull=false;break;case GL_BLEND:s.blend=false;break;default:throw std::runtime_error("unsupported disable state");}}
void api_glCullFace(GLenum e){s.cullMode=e;}
void api_glDepthFunc(GLenum e){s.depthFunc=e;}
void api_glDepthMask(GLboolean e){s.depthWrite=e;}
void api_glBlendFunc(GLenum a,GLenum b){s.blendSrc=a;s.blendDst=b;}
void api_glPolygonMode(GLenum,GLenum e){s.poly=e;}
void api_glPatchParameteri(GLenum key,GLint value){require(key==GL_PATCH_VERTICES&&(value==3||value==4),"invalid control-point count");}
void api_glDrawArrays(GLenum mode,GLint first,GLsizei count){draw(mode,count,false,0,first);}
void api_glDrawElements(GLenum mode,GLsizei count,GLenum type,const void* offset){require(type==GL_UNSIGNED_INT,"index type must be uint32");draw(mode,count,true,(size_t)offset);}
void api_glDrawArraysIndirect(GLenum mode,const void* offset){draw(mode,0,false,0,0,s.buffers.at(s.boundBuffers[GL_DRAW_INDIRECT_BUFFER]).gpu,(size_t)offset);}
void api_glDispatchCompute(GLuint x,GLuint y,GLuint z){dispatch(x,y,z);}
void api_glDispatchComputeIndirect(GLintptr offset){dispatch(0,0,0,s.buffers.at(s.boundBuffers[GL_DISPATCH_INDIRECT_BUFFER]).gpu,offset);}
void api_glMemoryBarrier(GLbitfield){endEncoders();} // Tracked resources synchronize across encoders on this queue.
GLenum api_glGetError(){return GL_NO_ERROR;} // Metal failures are reported with context and exceptions.
void installAPI() {
#define INSTALL(name) glad_##name = api_##name
    INSTALL(glGenBuffers);INSTALL(glBindBuffer);INSTALL(glBufferData);INSTALL(glBufferSubData);INSTALL(glBindBufferBase);INSTALL(glBindBufferRange);INSTALL(glDeleteBuffers);INSTALL(glMapBuffer);INSTALL(glUnmapBuffer);
    INSTALL(glGenVertexArrays);INSTALL(glBindVertexArray);INSTALL(glDeleteVertexArrays);INSTALL(glVertexAttribPointer);INSTALL(glEnableVertexAttribArray);
    INSTALL(glGenTextures);INSTALL(glActiveTexture);INSTALL(glBindTexture);INSTALL(glDeleteTextures);INSTALL(glTexImage2D);INSTALL(glTexImage3D);INSTALL(glCompressedTexImage2D);INSTALL(glTexParameteri);INSTALL(glTexParameterfv);INSTALL(glGenerateMipmap);INSTALL(glBindImageTexture);
    INSTALL(glGenFramebuffers);INSTALL(glBindFramebuffer);INSTALL(glDeleteFramebuffers);INSTALL(glFramebufferTexture);INSTALL(glFramebufferTexture2D);INSTALL(glDrawBuffers);INSTALL(glDrawBuffer);INSTALL(glReadBuffer);INSTALL(glCheckFramebufferStatus);INSTALL(glBlitFramebuffer);
    INSTALL(glGenRenderbuffers);INSTALL(glBindRenderbuffer);INSTALL(glRenderbufferStorage);INSTALL(glFramebufferRenderbuffer);INSTALL(glDeleteRenderbuffers);
    INSTALL(glClearColor);INSTALL(glClear);INSTALL(glViewport);INSTALL(glEnable);INSTALL(glDisable);INSTALL(glCullFace);INSTALL(glDepthFunc);INSTALL(glDepthMask);INSTALL(glBlendFunc);INSTALL(glPolygonMode);INSTALL(glPatchParameteri);
    INSTALL(glDrawArrays);INSTALL(glDrawElements);INSTALL(glDrawArraysIndirect);INSTALL(glDispatchCompute);INSTALL(glDispatchComputeIndirect);INSTALL(glMemoryBarrier);INSTALL(glGetError);
#undef INSTALL
}
} // namespace
namespace MetalBackend {
void initialize(GLFWwindow* window,int width,int height) {
    s.device=MTLCreateSystemDefaultDevice();require(s.device!=nil,"no Metal device available");
    s.queue=[s.device newCommandQueue];s.window=window;installAPI();
    s.zero=[s.device newBufferWithLength:65536 options:MTLResourceStorageModeShared];memset(s.zero.contents,0,65536);
    MTLSamplerDescriptor* d=[MTLSamplerDescriptor new];d.minFilter=MTLSamplerMinMagFilterLinear;d.magFilter=MTLSamplerMinMagFilterLinear;d.sAddressMode=MTLSamplerAddressModeClampToEdge;d.tAddressMode=MTLSamplerAddressModeClampToEdge;s.sampler=[s.device newSamplerStateWithDescriptor:d];
    if(window) {
        NSWindow* ns=glfwGetCocoaWindow(window);s.layer=[CAMetalLayer layer];s.layer.device=s.device;s.layer.pixelFormat=MTLPixelFormatBGRA8Unorm;s.layer.framebufferOnly=NO;
        ns.contentView.wantsLayer=YES;ns.contentView.layer=s.layer;
        glfwGetFramebufferSize(window,&width,&height);
    }
    resize(width,height);std::cout<<"Metal device: "<<s.device.name.UTF8String<<'\n';
}
void resize(int w,int h) {
    if(w<=0||h<=0)return;s.width=w;s.height=h;
    s.screenDepth=makeTexture(MTLPixelFormatDepth32Float,w,h);
    if(s.layer){s.layer.drawableSize=CGSizeMake(w,h);s.layer.contentsScale=[(NSWindow*)glfwGetCocoaWindow(s.window) backingScaleFactor];}
    else s.screen=makeTexture(MTLPixelFormatBGRA8Unorm,w,h);
}
void beginFrame() {
    if(s.window){int w,h;glfwGetFramebufferSize(s.window,&w,&h);if(w!=s.width||h!=s.height)resize(w,h);}
    if(s.layer){s.drawable=[s.layer nextDrawable];require(s.drawable!=nil,"no drawable available");s.screen=s.drawable.texture;}
    command();s.framebuffer=0;
}
void present() {
    endEncoders();if(s.drawable)[s.command presentDrawable:s.drawable];finish();s.drawable=nil;
}
void shutdown(){finish();s=State{};}
unsigned createProgram(const char* v,const char* f,const char* g,const char* c,const char* e) {
    unsigned id=s.next++;Program p;p.blocks["VP"]=0;p.tess=c&&e;
    p.vertex=loadStage(v,p.tess?".capture":"");p.fragment=loadStage(f);
    if(p.tess){p.control=loadStage(c);p.evaluation=loadStage(e);}
    if(g) {std::string path=g;require(path.find("cascaded_shadow_depth")!=std::string::npos||path.find("point_shadow_depth")!=std::string::npos,"geometry stage has no Metal migration: "+path);p.layers=path.find("point_")!=std::string::npos?6:5;}
    s.programs.emplace(id,std::move(p));return id;
}
unsigned createComputeProgram(const char* path){unsigned id=s.next++;Program p;p.blocks["VP"]=0;p.compute=loadStage(path);s.programs.emplace(id,std::move(p));return id;}
void setUniform(unsigned program,const char* name,const void* data,unsigned components,unsigned columns) {
    auto& value=s.programs.at(program).values[name];value.resize(components*columns*4);memcpy(value.data(),data,value.size());
}
void useProgram(unsigned program){require(s.programs.count(program),"invalid program");s.program=program;}
void setBlockBinding(unsigned program,const char* name,unsigned binding){s.programs.at(program).blocks[name]=binding;}
void guiInitialize() {
    const char* source=R"MSL(
#include <metal_stdlib>
using namespace metal;
struct V { float2 pos; float2 uv; uint color; };
struct O { float4 pos [[position]]; float2 uv; float4 color; };
vertex O uiVertex(uint id [[vertex_id]],const device V* vertices [[buffer(0)]],constant float4x4& projection [[buffer(1)]]) {
    V v=vertices[id];O o;o.pos=projection*float4(v.pos,0,1);o.uv=v.uv;
    o.color=float4(v.color&255,(v.color>>8)&255,(v.color>>16)&255,(v.color>>24)&255)/255.;return o;
}
fragment float4 uiFragment(O in [[stage_in]],texture2d<float> tex [[texture(0)]],sampler sam [[sampler(0)]]) {return in.color*tex.sample(sam,in.uv);}
)MSL";
    NSError* error=nil;auto lib=[s.device newLibraryWithSource:[NSString stringWithUTF8String:source] options:nil error:&error];require(lib!=nil,errorText(error));
    MTLRenderPipelineDescriptor* d=[MTLRenderPipelineDescriptor new];d.vertexFunction=[lib newFunctionWithName:@"uiVertex"];d.fragmentFunction=[lib newFunctionWithName:@"uiFragment"];
    d.colorAttachments[0].pixelFormat=MTLPixelFormatBGRA8Unorm;d.depthAttachmentPixelFormat=MTLPixelFormatDepth32Float;
    d.colorAttachments[0].blendingEnabled=YES;d.colorAttachments[0].sourceRGBBlendFactor=MTLBlendFactorSourceAlpha;d.colorAttachments[0].destinationRGBBlendFactor=MTLBlendFactorOneMinusSourceAlpha;
    d.colorAttachments[0].sourceAlphaBlendFactor=MTLBlendFactorOne;d.colorAttachments[0].destinationAlphaBlendFactor=MTLBlendFactorOneMinusSourceAlpha;
    s.guiPipeline=[s.device newRenderPipelineStateWithDescriptor:d error:&error];require(s.guiPipeline!=nil,errorText(error));
    auto& io=ImGui::GetIO();io.BackendRendererName="SceneRenderer_Metal";io.BackendFlags|=ImGuiBackendFlags_RendererHasVtxOffset;
    unsigned char* pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    Texture tex;tex.width=w;tex.height=h;tex.gpu=makeTexture(MTLPixelFormatRGBA8Unorm,w,h);uploadTexture(tex,0,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    s.font=tex.gpu;io.Fonts->SetTexID((ImTextureID)(__bridge void*)s.font);
}
void guiNewFrame() {}
void guiRender(ImDrawData* data) {
    if(!data||data->TotalVtxCount==0)return;endEncoders();s.framebuffer=0;beginRender();
    auto setup=[&]() {
        [s.render setRenderPipelineState:s.guiPipeline];[s.render setCullMode:MTLCullModeNone];[s.render setTriangleFillMode:MTLTriangleFillModeFill];
        MTLDepthStencilDescriptor* depth=[MTLDepthStencilDescriptor new];depth.depthCompareFunction=MTLCompareFunctionAlways;depth.depthWriteEnabled=NO;[s.render setDepthStencilState:[s.device newDepthStencilStateWithDescriptor:depth]];
        [s.render setViewport:MTLViewport{0,0,double(s.width),double(s.height),0,1}];
        float l=data->DisplayPos.x,r=l+data->DisplaySize.x,t=data->DisplayPos.y,b=t+data->DisplaySize.y;
        float projection[16]={2/(r-l),0,0,0,0,2/(t-b),0,0,0,0,1,0,(r+l)/(l-r),(t+b)/(b-t),0,1};
        [s.render setVertexBytes:projection length:sizeof(projection) atIndex:1];[s.render setFragmentSamplerState:s.sampler atIndex:0];
    };setup();
    for(int i=0;i<data->CmdListsCount;i++) {
        auto list=data->CmdLists[i];
        auto vertices=[s.device newBufferWithBytes:list->VtxBuffer.Data length:list->VtxBuffer.Size*sizeof(ImDrawVert) options:MTLResourceStorageModeShared];
        auto indices=[s.device newBufferWithBytes:list->IdxBuffer.Data length:list->IdxBuffer.Size*sizeof(ImDrawIdx) options:MTLResourceStorageModeShared];
        for(auto& cmd:list->CmdBuffer) {
            if(cmd.UserCallback) {if(cmd.UserCallback==ImDrawCallback_ResetRenderState)setup();else cmd.UserCallback(list,&cmd);continue;}
            float x0=(cmd.ClipRect.x-data->DisplayPos.x)*data->FramebufferScale.x,y0=(cmd.ClipRect.y-data->DisplayPos.y)*data->FramebufferScale.y;
            float x1=(cmd.ClipRect.z-data->DisplayPos.x)*data->FramebufferScale.x,y1=(cmd.ClipRect.w-data->DisplayPos.y)*data->FramebufferScale.y;
            x0=std::clamp(x0,0.f,float(s.width));y0=std::clamp(y0,0.f,float(s.height));x1=std::clamp(x1,0.f,float(s.width));y1=std::clamp(y1,0.f,float(s.height));if(x1<=x0||y1<=y0)continue;
            [s.render setScissorRect:MTLScissorRect{NSUInteger(x0),NSUInteger(y0),NSUInteger(x1-x0),NSUInteger(y1-y0)}];
            [s.render setVertexBuffer:vertices offset:cmd.VtxOffset*sizeof(ImDrawVert) atIndex:0];
            [s.render setFragmentTexture:(__bridge id<MTLTexture>)(void*)cmd.TextureId atIndex:0];
            [s.render drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:cmd.ElemCount indexType:sizeof(ImDrawIdx)==2?MTLIndexTypeUInt16:MTLIndexTypeUInt32 indexBuffer:indices indexBufferOffset:cmd.IdxOffset*sizeof(ImDrawIdx)];
        }
    }
    endEncoders();
}
void guiShutdown(){s.font=nil;s.guiPipeline=nil;}
void beginGPUCapture(const char* path) {
    finish();
    auto manager = [MTLCaptureManager sharedCaptureManager];
    require([manager supportsDestination:MTLCaptureDestinationGPUTraceDocument],
            "GPU capture unavailable; launch with MTL_CAPTURE_ENABLED=1");
    MTLCaptureDescriptor* descriptor = [MTLCaptureDescriptor new];
    descriptor.captureObject = s.device;
    descriptor.destination = MTLCaptureDestinationGPUTraceDocument;
    descriptor.outputURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
    NSError* error = nil;
    bool started = [manager startCaptureWithDescriptor:descriptor error:&error];
    require(started, error ? std::string(error.localizedDescription.UTF8String) : "GPU capture failed");
}
void endGPUCapture() {
    finish();
    [[MTLCaptureManager sharedCaptureManager] stopCapture];
}
FloatTexture readFloatTexture(unsigned texture) {
    auto gpu=s.textures.at(texture).gpu;auto format=gpu.pixelFormat;
    require(format==MTLPixelFormatRGBA16Float||format==MTLPixelFormatRGBA32Float,"readback requires RGBA float");
    size_t w=gpu.width,h=gpu.height,component=format==MTLPixelFormatRGBA16Float?2:4,row=(w*component*4+255)&~size_t(255);
    auto buffer=[s.device newBufferWithLength:row*h options:MTLResourceStorageModeShared];endEncoders();command();auto e=[s.command blitCommandEncoder];
    [e copyFromTexture:gpu sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(w,h,1) toBuffer:buffer destinationOffset:0 destinationBytesPerRow:row destinationBytesPerImage:row*h];[e endEncoding];finish();
    FloatTexture result{unsigned(w),unsigned(h),std::vector<float>(w*h*4)};
    for(size_t y=0;y<h;y++)for(size_t x=0;x<w;x++)for(size_t c=0;c<4;c++) {
        auto bytes=(uint8_t*)buffer.contents+y*row+(x*4+c)*component;
        if(component==2){_Float16 half;memcpy(&half,bytes,2);result.rgba[(y*w+x)*4+c]=half;}
        else memcpy(&result.rgba[(y*w+x)*4+c],bytes,4);
    }
    return result;
}
void inspectTexture(unsigned texture,const char* path) {
    auto gpu=s.textures.at(texture).gpu;auto format=gpu.pixelFormat;
    require(format==MTLPixelFormatRGBA16Float||format==MTLPixelFormatRGBA32Float,"inspection requires RGBA float");
    size_t w=gpu.width,h=gpu.height,component=format==MTLPixelFormatRGBA16Float?2:4,row=(w*component*4+255)&~size_t(255);
    auto buffer=[s.device newBufferWithLength:row*h options:MTLResourceStorageModeShared];endEncoders();command();auto e=[s.command blitCommandEncoder];
    [e copyFromTexture:gpu sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(w,h,1) toBuffer:buffer destinationOffset:0 destinationBytesPerRow:row destinationBytesPerImage:row*h];[e endEncoding];finish();
    float minimum=1e20f,maximum=-1e20f;size_t invalid=0,nonzero=0;std::vector<uint8_t> pixels(w*h*3);
    for(size_t y=0;y<h;y++)for(size_t x=0;x<w;x++)for(size_t c=0;c<3;c++) {
        auto bytes=(uint8_t*)buffer.contents+y*row+(x*4+c)*component;float v;
        if(component==2){_Float16 half;memcpy(&half,bytes,2);v=half;}else memcpy(&v,bytes,4);
        invalid+=!std::isfinite(v);nonzero+=v!=0;minimum=std::min(minimum,v);maximum=std::max(maximum,v);
        pixels[((h-y-1)*w+x)*3+c]=std::isfinite(v)?uint8_t(std::clamp(v,0.f,1.f)*255):255;
    }
    std::cout<<path<<" min="<<minimum<<" max="<<maximum<<" invalid="<<invalid<<" nonzero="<<nonzero<<"\n";
    require(invalid==0,std::string(path)+" contains nonfinite GPU values");
    require(nonzero>0,std::string(path)+" is empty");
    stbi_write_png(path,w,h,3,pixels.data(),w*3);

}
void capture(const char* path) {
    endEncoders();command();size_t row=(s.width*4+255)&~size_t(255);
    auto buffer=[s.device newBufferWithLength:row*s.height options:MTLResourceStorageModeShared];
    auto e=[s.command blitCommandEncoder];
    [e copyFromTexture:s.screen sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(s.width,s.height,1) toBuffer:buffer destinationOffset:0 destinationBytesPerRow:row destinationBytesPerImage:row*s.height];
    [e endEncoding];finish();std::vector<uint8_t> rgba(s.width*s.height*4);auto bytes=(uint8_t*)buffer.contents;
    unsigned minValue=255,maxValue=0;
    for(int y=0;y<s.height;y++)for(int x=0;x<s.width;x++) {
        auto pixel=bytes+y*row+x*4;auto dst=rgba.data()+(y*s.width+x)*4;
        dst[0]=pixel[2];dst[1]=pixel[1];dst[2]=pixel[0];dst[3]=255;
        for(int c=0;c<3;c++){minValue=std::min(minValue,unsigned(dst[c]));maxValue=std::max(maxValue,unsigned(dst[c]));}
    }
    require(maxValue>minValue+10,"render produced a uniform or empty image");
    require(stbi_write_png(path,s.width,s.height,4,rgba.data(),s.width*4)!=0,"failed to save GPU capture");
}
void selfTest() {
    // Shader library coverage + real GPU readback, including compute image writes.
    initialize(nullptr,64,64);beginFrame();
    for(auto& entry:std::filesystem::recursive_directory_iterator(SR_METAL_SHADER_DIR))if(entry.path().extension()==".metallib") {
        NSError* error=nil;auto lib=[s.device newLibraryWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:entry.path().c_str()]] error:&error];require(lib!=nil,errorText(error));require([lib newFunctionWithName:@"main0"]!=nil,"invalid shader library");
    }
    auto program=createComputeProgram("src/shader/ocean/ocean_ComputeGaussianRandom.comp");useProgram(program);
    int n=16;setUniform(program,"N",&n,1);GLuint tex;api_glGenTextures(1,&tex);api_glBindTexture(GL_TEXTURE_2D,tex);
    api_glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,n,n,0,GL_RGBA,GL_FLOAT,nullptr);api_glBindImageTexture(1,tex,0,false,0,GL_WRITE_ONLY,GL_RGBA32F);
    api_glDispatchCompute(2,2,1);endEncoders();command();auto readback=[s.device newBufferWithLength:n*n*16 options:MTLResourceStorageModeShared];
    auto e=[s.command blitCommandEncoder];[e copyFromTexture:s.textures.at(tex).gpu sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(n,n,1) toBuffer:readback destinationOffset:0 destinationBytesPerRow:n*16 destinationBytesPerImage:n*n*16];[e endEncoding];finish();
    float* samples=(float*)readback.contents;bool nonzero=false;for(int i=0;i<n*n*4;i++){require(std::isfinite(samples[i]),"nonfinite Gaussian output");nonzero|=samples[i]!=0;}require(nonzero,"Gaussian compute did not write its texture");
    std::cout<<"Metal shader libraries and GPU Gaussian readback passed\n";resize(320,180);validateMetalFeatures();validateMetalRSM();shutdown();
}
} // namespace MetalBackend
