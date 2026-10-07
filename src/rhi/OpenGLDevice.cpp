#include "rhi/Device.h"
#include "rhi/GraphicsDevice.h"
#include "rhi/ShaderAssets.h"
#include <glad/glad.h>
#include <glfw/glfw3.h>
#include <limits>
#include <stdexcept>
#include <vector>

namespace rhi {
namespace {
BufferLimits queryLimits(GLFWwindow* window) {
    if (!window || glfwGetCurrentContext() != window || !glad_glGenBuffers)
        throw std::logic_error("RHI: OpenGL requires a current context and loaded GLAD");
    GLint maxUniform = 0, alignment = 0, bindings = 0;
    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &maxUniform);
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &alignment);
    glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &bindings);
    return {size_t(std::numeric_limits<GLsizeiptr>::max()), size_t(maxUniform),
            size_t(alignment), uint32_t(bindings)};
}
// Copy bindings avoid changing legacy vertex/uniform bindings during uploads.
class ScopedCopyBinding {
public:
    ScopedCopyBinding(GLenum target, GLuint buffer) : target_(target) {
        glGetIntegerv(target, &previous_);
        glBindBuffer(target, buffer);
    }
    ~ScopedCopyBinding() { glBindBuffer(target_, GLuint(previous_)); }
private:
    GLenum target_;
    GLint previous_ = 0;
};
GraphicsLimits queryGraphicsLimits() {
    GLint dimension = 0, attributes = 0, colors = 0, drawBuffers = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &dimension);glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &attributes);
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &colors);glGetIntegerv(GL_MAX_DRAW_BUFFERS, &drawBuffers);
    return {uint32_t(dimension), uint32_t(attributes), 3, 8, uint32_t(std::min(colors, drawBuffers))};
}
struct ScopedTextureBinding {
    GLint texture = 0, unpack = 0, pack = 0, unpackBuffer = 0, packBuffer = 0;
    const GLenum stores[6] = {GL_PACK_ROW_LENGTH, GL_PACK_SKIP_PIXELS, GL_PACK_SKIP_ROWS, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_ROWS};
    GLint previousStores[6]{};
    ScopedTextureBinding(GLuint id) {
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack);glGetIntegerv(GL_PACK_ALIGNMENT, &pack);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
        for (unsigned i = 0; i < 6; ++i) { glGetIntegerv(stores[i], &previousStores[i]);glPixelStorei(stores[i], 0); }
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);glPixelStorei(GL_PACK_ALIGNMENT, 1);glBindTexture(GL_TEXTURE_2D, id);
    }
    ~ScopedTextureBinding() {
        glBindTexture(GL_TEXTURE_2D, GLuint(texture));glPixelStorei(GL_UNPACK_ALIGNMENT, unpack);glPixelStorei(GL_PACK_ALIGNMENT, pack);
        for (unsigned i = 0; i < 6; ++i) glPixelStorei(stores[i], previousStores[i]);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, GLuint(unpackBuffer));glBindBuffer(GL_PIXEL_PACK_BUFFER, GLuint(packBuffer));
    }
};
class OpenGLDevice final : public GraphicsDevice {
public:
    bool supportsWireframe() const override { return true; }
    ComputeLimits computeLimits() const override {
        if (!GLAD_GL_VERSION_4_3) return {};
        ComputeLimits limits;limits.supported=true;
        for (GLuint i=0;i<3;++i) { GLint value;glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT,i,&value);limits.maxGroups[i]=uint32_t(value);glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE,i,&value);limits.maxThreads[i]=uint32_t(value); }
        GLint value;glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS,&value);limits.maxInvocations=uint32_t(value);
        glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT,&value);limits.storageOffsetAlignment=size_t(value);
        GLint64 range;glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE,&range);limits.maxStorageRange=size_t(range);
        // Two groups flatten to sixteen slots. Reject contexts that cannot honor this ABI.
        glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS,&value);if(value<16) return {};
        glGetIntegerv(GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS,&value);limits.maxStorageBindings=uint32_t(value);
        glGetIntegerv(GL_MAX_COMPUTE_UNIFORM_BLOCKS,&value);limits.maxUniformBindings=uint32_t(value);
        return limits;
    }
    explicit OpenGLDevice(GLFWwindow* window) : GraphicsDevice(queryLimits(window), queryGraphicsLimits()), window_(window) {}
    bool supportsPresentation() const override { return window_ != nullptr; }
    Backend backend() const override { return Backend::OpenGL; }
    bool supportsTexture(Format format, TextureUsage usage) const override {
        const uint32_t bits = uint32_t(usage);
        if (!bits || (bits & ~31u)) return false;
        if (format == Format::Depth32Float) return hasUsage(usage,TextureUsage::DepthAttachment) && !(bits & ~uint32_t(TextureUsage::DepthAttachment|TextureUsage::Sampled|TextureUsage::CopySource));
        return (format == Format::RGBA8UNorm || format == Format::RGBA16Float || format == Format::RGBA32Float) && !hasUsage(usage, TextureUsage::DepthAttachment);
    }
protected:
    NativeObject createTextureImpl(const TextureDesc& desc) override {
        GLuint id = 0;glGenTextures(1, &id);ScopedTextureBinding binding(id);
        const auto format = desc.format == Format::RGBA8UNorm ? GL_RGBA8 : desc.format == Format::RGBA16Float ? GL_RGBA16F : desc.format == Format::RGBA32Float ? GL_RGBA32F : GL_DEPTH_COMPONENT32F;
        glTexImage2D(GL_TEXTURE_2D, 0, format, desc.width, desc.height, 0,
            desc.format == Format::Depth32Float ? GL_DEPTH_COMPONENT : GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        GLint width = 0;glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        if (width != GLint(desc.width)) { glDeleteTextures(1, &id);throw std::runtime_error("RHI: GL texture allocation failed"); }
        return id;
    }
    // GL 4.1 view for the only supported subresource aliases its texture; native
    // texture-view support will be capability-gated when mip/layer ranges arrive.
    NativeObject createTextureViewImpl(NativeObject texture, const TextureDesc&,const TextureViewDesc&) override { return texture; }
    NativeObject createSamplerImpl(const SamplerDesc& desc) override {
        GLuint id = 0;glGenSamplers(1, &id);
        glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER, desc.filter == Filter::Nearest ? GL_NEAREST : GL_LINEAR);
        glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, desc.filter == Filter::Nearest ? GL_NEAREST : GL_LINEAR);
        const auto wrap = desc.address == AddressMode::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
        glSamplerParameteri(id, GL_TEXTURE_WRAP_S, wrap);glSamplerParameteri(id, GL_TEXTURE_WRAP_T, wrap);return id;
    }
    static GLuint compile(GLenum type, const std::string& path) {
        auto text = readShaderText(path);const char* source = text.c_str();GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);glCompileShader(shader);GLint status = 0;glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
        if (!status) {
            char message[4096];glGetShaderInfoLog(shader, sizeof(message), nullptr, message);glDeleteShader(shader);
            throw std::runtime_error(std::string("RHI: GL shader compilation failed: ") + message);
        }return shader;
    }
    NativeObject createComputePipelineImpl(const ComputePipelineDesc& desc) override {
        validateComputeShaderLayout(desc);GLuint shader=compile(GL_COMPUTE_SHADER,desc.shader.glslPath), program=0;
        try {
            program=glCreateProgram();glAttachShader(program,shader);glLinkProgram(program);GLint linked;glGetProgramiv(program,GL_LINK_STATUS,&linked);
            if(!linked) { char message[4096];glGetProgramInfoLog(program,sizeof(message),nullptr,message);throw std::runtime_error(std::string("RHI: compute GL link failed: ")+message); }
            for(const auto& l:desc.bindings)for(const auto& e:l.entries) {
                const auto slot=l.group*8+e.binding;
                if(isStorage(e.type)) { auto index=glGetProgramResourceIndex(program,GL_SHADER_STORAGE_BLOCK,e.name.c_str());if(index==GL_INVALID_INDEX)throw std::invalid_argument("RHI: missing GL storage block");glShaderStorageBlockBinding(program,index,slot); }
                else { auto index=glGetUniformBlockIndex(program,e.name.c_str());if(index==GL_INVALID_INDEX)throw std::invalid_argument("RHI: missing GL compute uniform block");glUniformBlockBinding(program,index,slot); }
            }
            glDeleteShader(shader);return program;
        } catch(...) { glDeleteShader(shader);if(program)glDeleteProgram(program);throw; }
    }
    void destroyComputePipelineImpl(NativeObject id) noexcept override {glDeleteProgram(GLuint(id));}
    NativeObject createPipelineImpl(const GraphicsPipelineDesc& desc) override {
        validateShaderLayout(desc);
        GLuint vertex = compile(GL_VERTEX_SHADER, desc.vertex.glslPath), fragment = 0, program = 0, vao = 0;
        GLint previous = 0;glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
        try {
            fragment = compile(GL_FRAGMENT_SHADER, desc.fragment.glslPath);program = glCreateProgram();
            glAttachShader(program, vertex);glAttachShader(program, fragment);glLinkProgram(program);
            GLint linked = 0;glGetProgramiv(program, GL_LINK_STATUS, &linked);
            if (!linked) { char message[4096];glGetProgramInfoLog(program, sizeof(message), nullptr, message);throw std::runtime_error(std::string("RHI: GL link failed: ") + message); }
            glUseProgram(program);
            for (const auto& l : desc.bindings) for (const auto& e : l.entries) {
                const auto slot = l.group * graphicsLimits().maxBindingsPerGroup + e.binding;
                if(isStorage(e.type)){auto index=glGetProgramResourceIndex(program,GL_SHADER_STORAGE_BLOCK,e.name.c_str());if(index==GL_INVALID_INDEX)throw std::invalid_argument("RHI: missing graphics storage block");glShaderStorageBlockBinding(program,index,slot);}
                else if (e.type == BindingType::UniformBuffer) {
                    GLuint index = glGetUniformBlockIndex(program, e.name.c_str());
                    if (index == GL_INVALID_INDEX) continue; // SPIR-V validation already verified the declaration; GL may optimize it out.
                    glUniformBlockBinding(program, index, slot);
                } else {
                    GLint location = glGetUniformLocation(program, e.name.c_str());
                    if (location < 0) continue; // Linked shader can discard inactive resources.
                    glUniform1i(location, slot);
                }
            }
            glGenVertexArrays(1, &vao);pipelines_.emplace(program, vao);
            glUseProgram(GLuint(previous));glDeleteShader(vertex);glDeleteShader(fragment);return program;
        } catch (...) {
            glUseProgram(GLuint(previous));glDeleteShader(vertex);if (fragment) glDeleteShader(fragment);
            if (program) glDeleteProgram(program);if (vao) glDeleteVertexArrays(1, &vao);throw;
        }
    }
    void destroyTextureImpl(NativeObject native) noexcept override { GLuint id = GLuint(native);glDeleteTextures(1, &id); }
    void destroyTextureViewImpl(NativeObject) noexcept override {}
    void destroySamplerImpl(NativeObject native) noexcept override { GLuint id = GLuint(native);glDeleteSamplers(1, &id); }
    void destroyPipelineImpl(NativeObject native) noexcept override {
        const auto it = pipelines_.find(GLuint(native));if (it != pipelines_.end()) { glDeleteVertexArrays(1, &it->second);pipelines_.erase(it); }
        glDeleteProgram(GLuint(native));
    }
    void writeTextureRegionImpl(NativeObject native,const TextureDesc& desc,TextureRegion r,const void* pixels,size_t) override {
        ScopedTextureBinding binding{GLuint(native)};
        glTexSubImage2D(GL_TEXTURE_2D,0,r.x,r.y,r.width,r.height,GL_RGBA,desc.format==Format::RGBA8UNorm?GL_UNSIGNED_BYTE:GL_FLOAT,pixels);
    }
    void writeTextureImpl(NativeObject id,const TextureDesc& desc,const void* pixels,size_t bytes) override {writeTextureRegionImpl(id,desc,{0,0,desc.width,desc.height},pixels,bytes);}
    void writeTextureFloatImpl(NativeObject id,const TextureDesc& desc,const float* pixels,size_t bytes) override {writeTextureImpl(id,desc,pixels,bytes);}
    std::vector<uint8_t> readTextureImpl(NativeObject native, const TextureDesc& desc) override {
        std::vector<uint8_t> result(size_t(desc.width) * desc.height * 4);ScopedTextureBinding binding{GLuint(native)};
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, result.data());return result;
    }
    std::vector<float> readTextureFloatImpl(NativeObject native, const TextureDesc& desc) override {
        std::vector<float> result(size_t(desc.width) * desc.height * (desc.format==Format::Depth32Float?1:4));ScopedTextureBinding binding{GLuint(native)};
        glGetTexImage(GL_TEXTURE_2D, 0, desc.format==Format::Depth32Float?GL_DEPTH_COMPONENT:GL_RGBA, GL_FLOAT, result.data());return result;
    }
    void copyToBackbufferImpl(NativeObject native, const TextureDesc& desc) override {
        int width, height;glfwGetFramebufferSize(window_, &width, &height);
        if (width != int(desc.width) || height != int(desc.height)) throw std::invalid_argument("RHI: backbuffer copy size differs");
        GLint read = 0, draw = 0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
        const auto scissor = glIsEnabled(GL_SCISSOR_TEST), srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
        GLuint fbo = 0;glGenFramebuffers(1, &fbo);glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, GLuint(native), 0);glReadBuffer(GL_COLOR_ATTACHMENT0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);GLint drawBuffer = 0;glGetIntegerv(GL_DRAW_BUFFER, &drawBuffer);glDrawBuffer(GL_BACK);
        glDisable(GL_SCISSOR_TEST);glDisable(GL_FRAMEBUFFER_SRGB);
        // RHI row zero is stored at GL y=0; the window's top is GL y=height.
        glBlitFramebuffer(0, 0, width, height, 0, height, width, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glDrawBuffer(GLenum(drawBuffer));glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(read));glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(draw));
        glDeleteFramebuffers(1, &fbo);if (scissor) glEnable(GL_SCISSOR_TEST);if (srgb) glEnable(GL_FRAMEBUFFER_SRGB);
    }
    void submitGraphicsImpl(const std::vector<RecordedPass>& passes) override {
        // Restore legacy state after RHI execution so both paths can coexist.
        GLint oldIndirect = 0, oldStorage = 0;glGetIntegerv(GL_DRAW_INDIRECT_BUFFER_BINDING,&oldIndirect);
        const bool computeSupported=computeLimits().supported;if(computeSupported)glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&oldStorage);
        GLint oldRead = 0, oldDraw = 0, oldProgram = 0, oldVAO = 0, oldArray = 0, oldActive = 0, viewport[4], polygon[2];
        GLint srcRGB, dstRGB, srcAlpha, dstAlpha, depthFunc, equationRGB, equationAlpha, oldUniform;
        GLboolean depthMask, colorMask[4];
        const GLenum capabilities[] = {GL_DEPTH_TEST, GL_BLEND, GL_CULL_FACE, GL_SCISSOR_TEST, GL_FRAMEBUFFER_SRGB,
            GL_RASTERIZER_DISCARD, GL_STENCIL_TEST, GL_DEPTH_CLAMP, GL_POLYGON_OFFSET_FILL, GL_COLOR_LOGIC_OP,
            GL_DITHER, GL_PRIMITIVE_RESTART, GL_SAMPLE_ALPHA_TO_COVERAGE, GL_SAMPLE_COVERAGE};
        GLboolean enabled[sizeof(capabilities) / sizeof(capabilities[0])]{};
        for (unsigned i = 0; i < sizeof(enabled); ++i) enabled[i] = glIsEnabled(capabilities[i]);
        GLint clipCount = 0;glGetIntegerv(GL_MAX_CLIP_DISTANCES, &clipCount);std::vector<GLboolean> clipEnabled(clipCount);
        for (int i = 0; i < clipCount; ++i) clipEnabled[i] = glIsEnabled(GL_CLIP_DISTANCE0 + i);
        GLdouble depthRange[2];glGetDoublev(GL_DEPTH_RANGE, depthRange);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldRead);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldDraw);
        glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVAO);glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldArray);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);glGetIntegerv(GL_VIEWPORT, viewport);glGetIntegerv(GL_POLYGON_MODE, polygon);
        glGetIntegerv(GL_BLEND_SRC_RGB, &srcRGB);glGetIntegerv(GL_BLEND_DST_RGB, &dstRGB);glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha);glGetIntegerv(GL_BLEND_DST_ALPHA, &dstAlpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &equationRGB);glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &equationAlpha);glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &oldUniform);
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
        struct Slot { GLint texture = 0, sampler = 0, uniform = 0, storage = 0;GLint64 offset = 0, size = 0, storageOffset = 0, storageSize = 0; };std::vector<Slot> slots(graphicsLimits().maxBindingGroups*graphicsLimits().maxBindingsPerGroup);
        for (unsigned i = 0; i < slots.size(); ++i) {
            glActiveTexture(GL_TEXTURE0 + i);glGetIntegerv(GL_TEXTURE_BINDING_2D, &slots[i].texture);
            glGetIntegerv(GL_SAMPLER_BINDING, &slots[i].sampler);glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, i, &slots[i].uniform);
            if(computeSupported) { glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,i,&slots[i].storage);glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START,i,&slots[i].storageOffset);glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE,i,&slots[i].storageSize); }
            glGetInteger64i_v(GL_UNIFORM_BUFFER_START, i, &slots[i].offset);glGetInteger64i_v(GL_UNIFORM_BUFFER_SIZE, i, &slots[i].size);
        }
        struct ColorState { GLboolean mask[4], blend;GLint srcRGB,dstRGB,srcAlpha,dstAlpha,equationRGB,equationAlpha; };
        std::vector<ColorState> colorStates(graphicsLimits().maxColorAttachments);
        for(GLuint i=0;i<colorStates.size();++i) {
            auto& state=colorStates[i];glGetBooleani_v(GL_COLOR_WRITEMASK,i,state.mask);state.blend=glIsEnabledi(GL_BLEND,i);
            glGetIntegeri_v(GL_BLEND_SRC_RGB,i,&state.srcRGB);glGetIntegeri_v(GL_BLEND_DST_RGB,i,&state.dstRGB);glGetIntegeri_v(GL_BLEND_SRC_ALPHA,i,&state.srcAlpha);glGetIntegeri_v(GL_BLEND_DST_ALPHA,i,&state.dstAlpha);
            glGetIntegeri_v(GL_BLEND_EQUATION_RGB,i,&state.equationRGB);glGetIntegeri_v(GL_BLEND_EQUATION_ALPHA,i,&state.equationAlpha);
        }
        GLint oldDispatchIndirect=0;if(computeSupported)glGetIntegerv(GL_DISPATCH_INDIRECT_BUFFER_BINDING,&oldDispatchIndirect);
        GLint oldCullFace,oldFrontFace;glGetIntegerv(GL_CULL_FACE_MODE,&oldCullFace);glGetIntegerv(GL_FRONT_FACE,&oldFrontFace);
        GLint scissorBox[4];glGetIntegerv(GL_SCISSOR_BOX,scissorBox);
        GLuint fbo = 0;glGenFramebuffers(1, &fbo);
        auto restore = [&] {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(oldRead));glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(oldDraw));glDeleteFramebuffers(1, &fbo);
            glUseProgram(GLuint(oldProgram));glBindVertexArray(GLuint(oldVAO));glBindBuffer(GL_ARRAY_BUFFER, GLuint(oldArray));glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
            glPolygonMode(GL_FRONT_AND_BACK, polygon[0]);glDepthFunc(depthFunc);glDepthMask(depthMask);glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
            glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);glBlendEquationSeparate(equationRGB, equationAlpha);
            glCullFace(oldCullFace);glFrontFace(oldFrontFace);
            glDepthRange(depthRange[0], depthRange[1]);glScissor(scissorBox[0],scissorBox[1],scissorBox[2],scissorBox[3]);
            for (unsigned i = 0; i < sizeof(enabled); ++i) if (enabled[i]) glEnable(capabilities[i]);else glDisable(capabilities[i]);
            for (int i = 0; i < clipCount; ++i) if (clipEnabled[i]) glEnable(GL_CLIP_DISTANCE0 + i);else glDisable(GL_CLIP_DISTANCE0 + i);
            for (unsigned i = 0; i < slots.size(); ++i) {
                glActiveTexture(GL_TEXTURE0 + i);glBindTexture(GL_TEXTURE_2D, GLuint(slots[i].texture));glBindSampler(i, GLuint(slots[i].sampler));
                if (slots[i].uniform && slots[i].size) glBindBufferRange(GL_UNIFORM_BUFFER, i, GLuint(slots[i].uniform), slots[i].offset, slots[i].size);
                else glBindBufferBase(GL_UNIFORM_BUFFER, i, GLuint(slots[i].uniform));
                if(computeSupported) { if(slots[i].storage && slots[i].storageSize)glBindBufferRange(GL_SHADER_STORAGE_BUFFER,i,GLuint(slots[i].storage),slots[i].storageOffset,slots[i].storageSize);else glBindBufferBase(GL_SHADER_STORAGE_BUFFER,i,GLuint(slots[i].storage)); }
            }
            for(GLuint i=0;i<colorStates.size();++i) {
                const auto& state=colorStates[i];glColorMaski(i,state.mask[0],state.mask[1],state.mask[2],state.mask[3]);
                if(state.blend)glEnablei(GL_BLEND,i);else glDisablei(GL_BLEND,i);
                glBlendFuncSeparatei(i,state.srcRGB,state.dstRGB,state.srcAlpha,state.dstAlpha);glBlendEquationSeparatei(i,state.equationRGB,state.equationAlpha);
            }
            if(computeSupported)glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,GLuint(oldDispatchIndirect));
            glBindBuffer(GL_DRAW_INDIRECT_BUFFER,GLuint(oldIndirect));if(computeSupported)glBindBuffer(GL_SHADER_STORAGE_BUFFER,GLuint(oldStorage));
            glBindBuffer(GL_UNIFORM_BUFFER, GLuint(oldUniform));glActiveTexture(GLenum(oldActive));
        };
        try {
            for (auto capability : capabilities) glDisable(capability);
            for (int i = 0; i < clipCount; ++i) glDisable(GL_CLIP_DISTANCE0 + i);
            glDepthRange(0, 1);
            glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            for (const auto& pass : passes) {
                if(computeSupported)glMemoryBarrier(GL_ALL_BARRIER_BITS);
                if(pass.copy) {
                    GLuint read=0;glGenFramebuffers(1,&read);glBindFramebuffer(GL_READ_FRAMEBUFFER,read);glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,GLuint(textureObject(pass.copySource)),0);glReadBuffer(GL_COLOR_ATTACHMENT0);
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,fbo);for(uint32_t i=1;i<graphicsLimits().maxColorAttachments;++i)glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0+i,GL_TEXTURE_2D,0,0);glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,0,0);glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,GLuint(textureObject(pass.copyDestination)),0);glDrawBuffer(GL_COLOR_ATTACHMENT0);
                    const auto& desc=textureDesc(pass.copySource);glDisable(GL_SCISSOR_TEST);glBlitFramebuffer(0,0,desc.width,desc.height,0,0,desc.width,desc.height,GL_COLOR_BUFFER_BIT,GL_NEAREST);glDeleteFramebuffers(1,&read);glBindFramebuffer(GL_FRAMEBUFFER,fbo);continue;
                }
                if(pass.compute) {
                    glUseProgram(GLuint(computePipelineObject(pass.dispatch.pipeline)));
                    for(auto set:pass.dispatch.bindings)for(const auto& b:resolvedBindings(set))glBindBufferRange(isStorage(b.layout.type)?GL_SHADER_STORAGE_BUFFER:GL_UNIFORM_BUFFER,b.layout.binding,GLuint(b.buffer),b.offset,b.size);
                    const auto& groups=pass.dispatch.groups;if(pass.dispatch.indirect){glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,GLuint(nativeBuffer(pass.dispatch.indirect,BufferUsage::Indirect)));glDispatchComputeIndirect(pass.dispatch.indirectOffset);}else glDispatchCompute(groups[0],groups[1],groups[2]);continue;
                }
                const auto& target = viewTextureDesc(pass.desc.color?pass.desc.color:pass.desc.depth);
                const auto colors = colorAttachments(pass.desc);std::vector<GLenum> drawBuffers;
                for (uint32_t i = 0; i < graphicsLimits().maxColorAttachments; ++i) {
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, i < colors.size() ? GLuint(textureViewObject(colors[i].view)) : 0, 0);
                    if (i < colors.size()) drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + i);
                }
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, pass.desc.depth ? GLuint(textureViewObject(pass.desc.depth)) : 0, 0);
                glDrawBuffers(GLsizei(drawBuffers.size()), drawBuffers.data());glReadBuffer(drawBuffers.empty()?GL_NONE:GL_COLOR_ATTACHMENT0);
                if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) throw std::runtime_error("RHI: incomplete GL render target");
                const auto v=pass.desc.viewport.width?pass.desc.viewport:Viewport{0,0,target.width,target.height};
                glDisable(GL_SCISSOR_TEST);glViewport(v.x,v.y,v.width,v.height);glDepthMask(GL_TRUE);
                for (size_t i = 0; i < colors.size(); ++i) if (colors[i].load == LoadOp::Clear) glClearBufferfv(GL_COLOR, GLint(i), colors[i].clear.data());
                if (pass.desc.depth && pass.desc.depthLoad == LoadOp::Clear) glClearBufferfv(GL_DEPTH, 0, &pass.desc.clearDepth);
                glEnable(GL_SCISSOR_TEST);const auto clip=pass.desc.scissor.width?pass.desc.scissor:v;glScissor(clip.x,clip.y,clip.width,clip.height);
                for (const auto& draw : pass.draws) {
                    const auto program = GLuint(pipelineObject(draw.pipeline));const auto& p = pipelineDesc(draw.pipeline);
                    glUseProgram(program);glBindVertexArray(pipelines_.at(program));glPolygonMode(GL_FRONT_AND_BACK,p.wireframe?GL_LINE:GL_FILL);
                    if (p.depthTest) glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);glDepthFunc(p.depthCompare==DepthCompare::Less?GL_LESS:p.depthCompare==DepthCompare::LessEqual?GL_LEQUAL:p.depthCompare==DepthCompare::Greater?GL_GREATER:GL_ALWAYS);glDepthMask(p.depthWrite);
                    glFrontFace(GL_CW);if(p.cull==CullMode::None)glDisable(GL_CULL_FACE);else {glEnable(GL_CULL_FACE);glCullFace(p.cull==CullMode::Back?GL_BACK:GL_FRONT);glFrontFace(GL_CW);}
                    for(size_t i=0;i<colors.size();++i){if(p.attachmentBlend.empty()?p.blend:p.attachmentBlend.at(i))glEnablei(GL_BLEND,i);else glDisablei(GL_BLEND,i);glBlendFuncSeparatei(GLuint(i),p.additiveBlend?GL_ONE:GL_SRC_ALPHA,p.additiveBlend?GL_ONE:GL_ONE_MINUS_SRC_ALPHA,GL_ONE,p.additiveBlend?GL_ONE:GL_ONE_MINUS_SRC_ALPHA);}
                    glBindBuffer(GL_ARRAY_BUFFER, GLuint(nativeBuffer(draw.vertices, BufferUsage::Vertex)));
                    for (const auto& a : p.attributes) {
                        glEnableVertexAttribArray(a.location);const int count = a.format == VertexFormat::Float2 ? 2 : a.format == VertexFormat::Float3 ? 3 : 4;
                        glVertexAttribPointer(a.location, count, GL_FLOAT, GL_FALSE, p.vertexStride, reinterpret_cast<void*>(draw.vertexOffset + a.offset));
                    }
                    for (auto set : draw.bindings) for (const auto& b : resolvedBindings(set)) {
                        if(b.buffer)glBindBufferRange(isStorage(b.layout.type)?GL_SHADER_STORAGE_BUFFER:GL_UNIFORM_BUFFER, b.layout.binding, GLuint(b.buffer), b.offset, b.size);
                        else { glActiveTexture(GL_TEXTURE0 + b.layout.binding);glBindTexture(GL_TEXTURE_2D, GLuint(b.textureView));glBindSampler(b.layout.binding, GLuint(b.sampler)); }
                    }
                    if (draw.indirect) {
                        glBindBuffer(GL_DRAW_INDIRECT_BUFFER,GLuint(nativeBuffer(draw.indirect,BufferUsage::Indirect)));
                        if(draw.indexed) {
                            // GL indexed indirect's firstIndex is relative to the complete index buffer.
                            // The common contract requires indexOffset=0 for this initial path.
                            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,GLuint(nativeBuffer(draw.indices,BufferUsage::Index)));
                            glDrawElementsIndirect(GL_TRIANGLES,draw.indexType==IndexType::UInt16?GL_UNSIGNED_SHORT:GL_UNSIGNED_INT,reinterpret_cast<void*>(draw.indirectOffset));
                        } else glDrawArraysIndirect(GL_TRIANGLES,reinterpret_cast<void*>(draw.indirectOffset));
                    } else if (draw.indexed) {
                        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, GLuint(nativeBuffer(draw.indices, BufferUsage::Index)));
                        const auto unit = draw.indexType == IndexType::UInt16 ? 2u : 4u;
                        glDrawElementsBaseVertex(GL_TRIANGLES, draw.count, unit == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                            reinterpret_cast<void*>(draw.indexOffset + size_t(draw.first) * unit), draw.baseVertex);
                    } else glDrawArrays(GL_TRIANGLES, draw.first, draw.count);
                }
            }
            if(computeSupported)glMemoryBarrier(GL_ALL_BARRIER_BITS);glFinish();restore();
        } catch (...) { restore();throw; }
    }
    NativeBuffer createBufferImpl(const BufferDesc& desc, const void* data) override {
        std::vector<uint8_t> zero;
        if (!data && desc.initialization == BufferInitialization::Zeroed) { zero.resize(desc.size, 0); data = zero.data(); }
        GLuint id = 0;
        glGenBuffers(1, &id);
        ScopedCopyBinding binding(GL_COPY_WRITE_BUFFER, id);
        glBufferData(GL_COPY_WRITE_BUFFER, GLsizeiptr(desc.size), data, GL_DYNAMIC_DRAW);
        GLint64 actual = 0;
        glGetBufferParameteri64v(GL_COPY_WRITE_BUFFER, GL_BUFFER_SIZE, &actual);
        if (!id || actual != GLint64(desc.size)) {
            glDeleteBuffers(1, &id);
            throw std::runtime_error("RHI: OpenGL buffer allocation failed: " + desc.label);
        }
        return id;
    }
    void destroyBufferImpl(NativeBuffer native) noexcept override {
        const GLuint id = GLuint(native);
        glDeleteBuffers(1, &id);
    }
    void writeBufferImpl(NativeBuffer native, size_t offset, size_t size, const void* data) override {
        ScopedCopyBinding binding(GL_COPY_WRITE_BUFFER, GLuint(native));
        glBufferSubData(GL_COPY_WRITE_BUFFER, GLintptr(offset), GLsizeiptr(size), data);
    }
    void readBufferImpl(NativeBuffer native, size_t offset, size_t size, void* data) override {
        ScopedCopyBinding binding(GL_COPY_READ_BUFFER, GLuint(native));
        glGetBufferSubData(GL_COPY_READ_BUFFER, GLintptr(offset), GLsizeiptr(size), data);
    }
    void bindUniformBufferImpl(uint32_t slot, NativeBuffer native, size_t offset, size_t size) override {
        if (native) glBindBufferRange(GL_UNIFORM_BUFFER, slot, GLuint(native), GLintptr(offset), GLsizeiptr(size));
        else glBindBufferBase(GL_UNIFORM_BUFFER, slot, 0);
    }
    void beginFrameImpl() override {
        if (glfwGetCurrentContext() != window_)
            throw std::logic_error("RHI: OpenGL context is not current on the render thread");
    }
    void presentImpl() override { glfwSwapBuffers(window_); }
    void waitIdleImpl() override { glFinish(); }
    // GLFW owns the context and destroys it after RHI shutdown.
    void closeImpl() override { window_ = nullptr; }
private:
    GLFWwindow* window_;
    std::unordered_map<GLuint, GLuint> pipelines_;
};
}
std::shared_ptr<Device> makeOpenGLDevice(GLFWwindow* window) {
    return std::make_shared<OpenGLDevice>(window);
}
} // namespace rhi
