#pragma once
#include "rhi/Device.h"
#include <array>
#include <vector>

namespace rhi {
template<class Tag> struct Handle {
    uint64_t value = 0;
    explicit operator bool() const { return value != 0; }
};
using TextureHandle = Handle<struct TextureTag>;
using TextureViewHandle = Handle<struct TextureViewTag>;
using SamplerHandle = Handle<struct SamplerTag>;
using PipelineHandle = Handle<struct PipelineTag>;
using ComputePipelineHandle = Handle<struct ComputePipelineTag>;
using BindingSetHandle = Handle<struct BindingSetTag>;
enum class Format { RGBA8UNorm, RGBA16Float, Depth32Float, RGBA32Float };
enum class TextureUsage : uint32_t { None = 0, Sampled = 1, ColorAttachment = 2, DepthAttachment = 4, CopySource = 8, CopyDestination = 16, Storage = 32 };
constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) {
    return TextureUsage(uint32_t(a) | uint32_t(b));
}
constexpr bool hasUsage(TextureUsage value, TextureUsage flag) { return (uint32_t(value) & uint32_t(flag)) != 0; }
enum class Filter { Nearest, Linear };
enum class AddressMode { ClampToEdge, Repeat };
enum class ShaderStage { Vertex, Fragment, Compute };
enum class BindingType { UniformBuffer, SampledTexture, StorageRead, StorageWrite, StorageReadWrite, StorageTextureRead, StorageTextureWrite, StorageTextureReadWrite };
constexpr bool isStorage(BindingType type) { return type == BindingType::StorageRead || type == BindingType::StorageWrite || type == BindingType::StorageReadWrite; }
constexpr bool isStorageTexture(BindingType type) { return type == BindingType::StorageTextureRead || type == BindingType::StorageTextureWrite || type == BindingType::StorageTextureReadWrite; }
enum class VertexFormat { Float2, Float3, Float4 };
enum class LoadOp { Clear, Load, Discard };
enum class StoreOp { Store, Discard };
enum class IndexType { UInt16, UInt32 };
struct GraphicsLimits {
    uint32_t maxTextureDimension2D = 0;
    uint32_t maxVertexAttributes = 0;
    uint32_t maxBindingGroups = 2;
    uint32_t maxBindingsPerGroup = 8;
    uint32_t maxColorAttachments = 4;
};
// Initial contract: single-sample, one mip and one layer, two-dimensional textures.
// Unsupported dimensionality is not silently flattened into a 2D image.
struct TextureDesc {
    uint32_t width = 0, height = 0;
    Format format = Format::RGBA8UNorm;
    TextureUsage usage = TextureUsage::None;
    std::string label;
};
struct TextureViewDesc { TextureHandle texture; };
struct SamplerDesc { Filter filter = Filter::Linear; AddressMode address = AddressMode::ClampToEdge; };
struct BindingLayoutEntry {
    uint32_t binding = 0;
    BindingType type = BindingType::UniformBuffer;
    ShaderStage stage = ShaderStage::Fragment;
    std::string name; // OpenGL linker name; resolved once at pipeline creation.
    size_t minimumSize = 0; // Reflected uniform/storage block size, zero for textures.
    Format imageFormat = Format::RGBA32Float;
};
struct BindingLayout { uint32_t group = 0; std::vector<BindingLayoutEntry> entries; };
struct BindingEntry {
    uint32_t binding = 0;
    BufferHandle buffer;
    size_t offset = 0, size = 0;
    TextureViewHandle texture;
    SamplerHandle sampler;
};
struct BindingSetDesc { BindingLayout layout; std::vector<BindingEntry> entries; };
struct ShaderAsset {
    std::string glslPath, metallibPath, spirvPath, reflectionPath;
    std::string entryPoint = "main0";
    std::string spirvEntryPoint = "main";
};
struct ComputeLimits {
    bool supported = false;
    std::array<uint32_t,3> maxGroups{0,0,0}, maxThreads{0,0,0};
    uint32_t maxInvocations = 0;
    size_t storageOffsetAlignment = 1, maxStorageRange = 0;
    uint32_t maxStorageBindings = 16, maxUniformBindings = 16;
    uint32_t maxStorageImages = 0, maxSampledTextures = 0;
};
struct ComputePipelineDesc {
    ShaderAsset shader;
    std::vector<BindingLayout> bindings;
    std::array<uint32_t,3> threads{1,1,1};
    std::string label;
};
struct VertexAttribute { uint32_t location = 0; VertexFormat format = VertexFormat::Float2; uint32_t offset = 0; };
enum class DepthCompare {Less,LessEqual,Greater,Always};
enum class CullMode {None,Front,Back};
struct GraphicsPipelineDesc {
    ShaderAsset vertex, fragment;
    uint32_t vertexStride = 0;
    std::vector<VertexAttribute> attributes;
    std::vector<BindingLayout> bindings;
    Format colorFormat = Format::RGBA8UNorm;
    bool colorAttachment = true;
    std::vector<Format> additionalColorFormats;
    bool depthAttachment = false;
    bool depthTest = false, depthWrite = false;
    DepthCompare depthCompare=DepthCompare::Less;CullMode cull=CullMode::None;
    std::vector<bool> attachmentBlend; // Empty inherits blend; otherwise one entry per color target.
    bool wireframe = false;
    bool blend = false; // src alpha / one-minus-src-alpha, alpha uses one.
    std::string label;
};
struct ColorAttachment {
    TextureViewHandle view;
    LoadOp load = LoadOp::Clear;
    StoreOp store = StoreOp::Store;
    std::array<float, 4> clear{0, 0, 0, 0};
};
struct Viewport { uint32_t x=0,y=0,width=0,height=0; };
struct RenderPassDesc {
    TextureViewHandle color;
    LoadOp colorLoad = LoadOp::Clear;
    StoreOp colorStore = StoreOp::Store;
    std::array<float, 4> clearColor{0, 0, 0, 1};
    TextureViewHandle depth;
    LoadOp depthLoad = LoadOp::Clear;
    StoreOp depthStore = StoreOp::Store;
    float clearDepth = 1;
    std::vector<ColorAttachment> additionalColors;
    Viewport scissor; // Zero width/height uses the viewport.
    Viewport viewport; // Zero width/height select the entire attachment.
};
std::vector<Format> colorFormats(const GraphicsPipelineDesc&);
std::vector<ColorAttachment> colorAttachments(const RenderPassDesc&);
// Matches Metal, Vulkan and OpenGL native indirect argument layouts.
// Initial portable contract: firstInstance must be zero; argument counts,
// firstVertex/firstIndex/baseVertex must reference valid buffer contents.
// GPU-produced values remain the producer's responsibility.
struct DrawIndirectArguments { uint32_t vertexCount, instanceCount, firstVertex, firstInstance; };
struct DrawIndexedIndirectArguments { uint32_t indexCount, instanceCount, firstIndex;int32_t baseVertex;uint32_t firstInstance; };
struct DrawCommand {
    PipelineHandle pipeline;
    std::vector<BindingSetHandle> bindings;
    BufferHandle vertices, indices;
    size_t vertexOffset = 0, indexOffset = 0;
    IndexType indexType = IndexType::UInt32;
    uint32_t count = 0, first = 0;
    int32_t baseVertex = 0;
    bool indexed = false;
    BufferHandle indirect;
    size_t indirectOffset = 0;
};
enum class ResourceAccess : uint32_t {
    None=0, UniformRead=1, ShaderRead=2, ShaderWrite=4, VertexRead=8, IndexRead=16, IndirectRead=32, AttachmentRead=64, AttachmentWrite=128, CopyRead=256, CopyWrite=512
};
constexpr ResourceAccess operator|(ResourceAccess a, ResourceAccess b) { return ResourceAccess(uint32_t(a)|uint32_t(b)); }
enum class ResourceStage : uint32_t { None=0, Vertex=1, Fragment=2, Compute=4, VertexInput=8, DrawIndirect=16, ColorOutput=32, DepthTest=64, Transfer=128 };
constexpr ResourceStage operator|(ResourceStage a, ResourceStage b) { return ResourceStage(uint32_t(a)|uint32_t(b)); }
struct ResourceDependency {
    BufferHandle buffer;TextureHandle texture;
    ResourceAccess before=ResourceAccess::None, after=ResourceAccess::None;
    ResourceStage beforeStages=ResourceStage::None, afterStages=ResourceStage::None;
};
struct DispatchCommand { ComputePipelineHandle pipeline;std::vector<BindingSetHandle> bindings;std::array<uint32_t,3> groups{1,1,1};BufferHandle indirect;size_t indirectOffset=0; };
// Each dispatch is a separate logical pass, with a dependency boundary.
struct RecordedPass { RenderPassDesc desc;std::vector<DrawCommand> draws;bool compute = false;DispatchCommand dispatch;std::vector<ResourceDependency> dependencies;bool copy=false;TextureHandle copySource,copyDestination; };
class GraphicsDevice;
// A logical command list. submit is synchronous in this first implementation.
// Resources are checked again immediately before execution; lists are single-use.
class CommandList {
public:
    void beginRenderPass(const RenderPassDesc&);
    void bindPipeline(PipelineHandle);
    void bindBindingSet(BindingSetHandle);
    void bindVertexBuffer(BufferHandle, size_t offset = 0);
    void bindIndexBuffer(BufferHandle, IndexType = IndexType::UInt32, size_t offset = 0);
    void draw(uint32_t vertexCount, uint32_t firstVertex = 0);
    void drawIndexed(uint32_t indexCount, uint32_t firstIndex = 0, int32_t baseVertex = 0);
    void drawIndirect(BufferHandle arguments, size_t offset = 0);
    void drawIndexedIndirect(BufferHandle arguments, size_t offset = 0);
    void endRenderPass();
    void copyTexture(TextureHandle source,TextureHandle destination);
    void dispatch(ComputePipelineHandle, const std::vector<BindingSetHandle>&, std::array<uint32_t,3> groups);
    void dispatchIndirect(ComputePipelineHandle,const std::vector<BindingSetHandle>&,BufferHandle arguments,size_t offset=0);
    CommandList(const CommandList&) = delete;
    CommandList& operator=(const CommandList&) = delete;
    CommandList(CommandList&&) = default;
private:
    friend class GraphicsDevice;
    explicit CommandList(std::shared_ptr<GraphicsDevice>);
    void requirePass() const;
    std::shared_ptr<GraphicsDevice> owner_;
    std::vector<RecordedPass> passes_;
    DrawCommand state_;
    bool inPass_ = false, submitted_ = false;
};
struct ReadbackTicket {CompletionToken completion;std::shared_ptr<std::vector<uint8_t>> bytes;};
class GraphicsDevice : public Device, public std::enable_shared_from_this<GraphicsDevice> {
public:
    TextureHandle createTexture(const TextureDesc&);
    TextureViewHandle createTextureView(const TextureViewDesc&);
    SamplerHandle createSampler(const SamplerDesc&);
    PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc&);
    ComputePipelineHandle createComputePipeline(const ComputePipelineDesc&);
    void destroyComputePipeline(ComputePipelineHandle);
    virtual ComputeLimits computeLimits() const { return {}; }
    virtual bool supportsWireframe() const { return false; }
    BindingSetHandle createBindingSet(const BindingSetDesc&);
    void destroyTexture(TextureHandle);
    void destroyTextureView(TextureViewHandle);
    void destroySampler(SamplerHandle);
    void destroyPipeline(PipelineHandle);
    void destroyBindingSet(BindingSetHandle);
    void writeTexture(TextureHandle, const void* rgba, size_t bytes);
    // RGBA8 readback, tightly packed, row zero is the top of the render target.
    ReadbackTicket requestTextureReadback(TextureHandle); // Tightly packed rows; format matches TextureDesc.
    std::vector<uint8_t> readTexture(TextureHandle);
    std::vector<float> readTextureFloat(TextureHandle);
    void writeTextureFloat(TextureHandle, const float* rgba, size_t bytes);
    // Copy a display-ready RGBA8 target to the current native backbuffer.
    // Caller owns beginFrame/present. Offscreen devices reject this operation.
    void copyToBackbuffer(TextureHandle);
    virtual std::array<uint32_t,2> presentationExtent()const{return {0,0};}
    virtual bool supportsPresentation() const { return false; }
    CommandList createCommandList();
    void submit(CommandList&);
    const GraphicsLimits& graphicsLimits() const { return graphicsLimits_; }
    virtual bool supportsTexture(Format, TextureUsage) const = 0;
protected:
    using NativeObject = uint64_t;
    GraphicsDevice(BufferLimits, GraphicsLimits);
    struct NativeBinding {
        BindingLayoutEntry layout;
        NativeObject buffer = 0, textureView = 0, sampler = 0;
        size_t offset = 0, size = 0;
    };
    NativeObject textureObject(TextureHandle) const;
    const TextureDesc& textureDesc(TextureHandle) const;
    NativeObject textureViewObject(TextureViewHandle) const;
    const TextureDesc& viewTextureDesc(TextureViewHandle) const;
    NativeObject pipelineObject(PipelineHandle) const;
    NativeObject computePipelineObject(ComputePipelineHandle) const;
    const ComputePipelineDesc& computePipelineDesc(ComputePipelineHandle) const;
    const GraphicsPipelineDesc& pipelineDesc(PipelineHandle) const;
    std::vector<NativeBinding> resolvedBindings(BindingSetHandle) const;
    virtual NativeObject createComputePipelineImpl(const ComputePipelineDesc&);
    virtual void destroyComputePipelineImpl(NativeObject) noexcept {}
    virtual NativeObject createTextureImpl(const TextureDesc&) = 0;
    virtual NativeObject createTextureViewImpl(NativeObject, const TextureDesc&) = 0;
    virtual NativeObject createSamplerImpl(const SamplerDesc&) = 0;
    virtual NativeObject createPipelineImpl(const GraphicsPipelineDesc&) = 0;
    virtual void destroyTextureImpl(NativeObject) noexcept = 0;
    virtual void destroyTextureViewImpl(NativeObject) noexcept = 0;
    virtual void destroySamplerImpl(NativeObject) noexcept = 0;
    virtual void destroyPipelineImpl(NativeObject) noexcept = 0;
    virtual std::function<void()> queueTextureReadbackImpl(NativeObject,const TextureDesc&,std::shared_ptr<std::vector<uint8_t>>);
    virtual void writeTextureImpl(NativeObject, const TextureDesc&, const void*, size_t) = 0;
    virtual std::vector<uint8_t> readTextureImpl(NativeObject, const TextureDesc&) = 0;
    virtual void writeTextureFloatImpl(NativeObject, const TextureDesc&, const float*, size_t);
    virtual std::vector<float> readTextureFloatImpl(NativeObject, const TextureDesc&);
    virtual void copyToBackbufferImpl(NativeObject, const TextureDesc&);
    virtual void submitGraphicsImpl(const std::vector<RecordedPass>&) = 0;
private:
    friend class CommandList;
    struct TextureRecord { TextureDesc desc; NativeObject native; };
    struct ViewRecord { TextureViewDesc desc; NativeObject native; };
    struct PipelineRecord { GraphicsPipelineDesc desc; NativeObject native; };
    struct ComputeRecord { ComputePipelineDesc desc;NativeObject native; };
    struct SamplerRecord { SamplerDesc desc; NativeObject native; };
    const TextureRecord& texture(TextureHandle) const;
    const ViewRecord& view(TextureViewHandle) const;
    const PipelineRecord& pipeline(PipelineHandle) const;
    const SamplerRecord& sampler(SamplerHandle) const;
    const BindingSetDesc& bindingSet(BindingSetHandle) const;
    void validateLayout(const BindingLayout&) const;
    void validateBindings(const BindingSetDesc&) const;
    void validatePass(const RecordedPass&) const;
    void releaseResourcesImpl() final;
    void checkOpen() const;
    GraphicsLimits graphicsLimits_;
    std::unordered_map<uint64_t, TextureRecord> textures_;
    std::unordered_map<uint64_t, ViewRecord> views_;
    std::unordered_map<uint64_t, PipelineRecord> pipelines_;
    std::unordered_map<uint64_t, ComputeRecord> computePipelines_;
    std::unordered_map<uint64_t, SamplerRecord> samplers_;
    std::unordered_map<uint64_t, BindingSetDesc> bindingSets_;
};
std::shared_ptr<GraphicsDevice> graphicsDevice();
// Offscreen Vulkan prototype; independent of the legacy scene renderer.
// Call before glfwInit when Vulkan loader lives outside the standard search path.
void configureVulkanWindowing();
std::shared_ptr<GraphicsDevice> makeMetalDevice(GLFWwindow* window=nullptr);
std::shared_ptr<GraphicsDevice> makeVulkanDevice(GLFWwindow* window = nullptr);
std::vector<float> decodeHalfPixels(const std::vector<uint8_t>&);
} // namespace rhi
