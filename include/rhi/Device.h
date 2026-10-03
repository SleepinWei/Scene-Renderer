#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <deque>
#include <vector>
#include <functional>
#include <thread>
#include <stdexcept>

struct GLFWwindow;

namespace rhi {
enum class Backend { Metal, Vulkan, OpenGL };
enum class BufferUsage : uint32_t {
    None = 0, Uniform = 1, Storage = 2, Vertex = 4, Index = 8,
    Indirect = 16, CopySource = 32, CopyDestination = 64
};
constexpr BufferUsage operator|(BufferUsage a, BufferUsage b) {
    return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr bool hasUsage(BufferUsage value, BufferUsage flag) {
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
}
struct BufferHandle {
    uint64_t value = 0;
    explicit operator bool() const { return value != 0; }
};
struct BufferDesc {
    size_t size = 0;
    BufferUsage usage = BufferUsage::None;
    std::string label;
};
// Only limits exercised by the implemented buffer API; full feature/format
// queries will accompany textures, pipelines and command lists.
struct BufferLimits {
    size_t maxBufferSize = 0;
    size_t maxUniformBufferSize = 0;
    size_t uniformOffsetAlignment = 1;
    uint32_t maxUniformBindings = 0;
};

struct CompletionToken {uint64_t device=0,serial=0;explicit operator bool()const{return serial!=0;}};
struct ResourceMemoryStats {
    size_t bufferBytes=0,textureBytes=0,peakBytes=0,budgetBytes=0;
    size_t usedBytes() const {return bufferBytes+textureBytes;}
};
class ResourceBudgetExceeded:public std::runtime_error {
public:
    explicit ResourceBudgetExceeded(const std::string& message):std::runtime_error(message){}
};
class Device {
public:
    virtual ~Device() = default;
    virtual Backend backend() const = 0;
    const BufferLimits& bufferLimits() const { return limits_; }
    BufferHandle createBuffer(const BufferDesc&, const void* initialData = nullptr);
    void destroyBuffer(BufferHandle) noexcept;
    void writeBuffer(BufferHandle, size_t offset, size_t size, const void* data);
    // Synchronous validation readback. Not intended for the frame hot path.
    void readBuffer(BufferHandle, size_t offset, size_t size, void* data);
    // Transitional binding used by shaders awaiting BindingSet migration.
    void bindUniformBuffer(uint32_t slot, BufferHandle, size_t offset = 0, size_t size = 0);
    void beginFrame();
    void present();
    CompletionToken checkpoint(std::function<void()> onComplete={});
    CompletionToken endFrame(); // Offscreen frames use this without presentation.
    bool isComplete(CompletionToken);
    void wait(CompletionToken);
    void retireResources(std::function<void()>);
    bool frameActive()const{return frameActive_;}
    size_t framesInFlight()const{return frames_.size();}
    void setMaxFramesInFlight(size_t);
    void waitForResourceRelease();
    void waitIdle();
    void close();
    bool isOpen() const { return open_; }
    // Transfer only at a quiescent startup/join boundary; this is not a lock.
    void adoptCurrentThread() { ownerThread_=std::this_thread::get_id(); }
    void checkThread() const;
    size_t allocatedBufferBytes() const;
    void setResourceBudget(size_t bytes); // 0 is unlimited; tracked buffer/texture payloads only.
    ResourceMemoryStats resourceMemory() const;

protected:
    void checkResourceAllocation(size_t bytes,const std::string& label) const;
    void accountResourceAllocation(size_t bytes,bool texture) noexcept;
    void accountResourceRelease(size_t bytes,bool texture) noexcept;
    explicit Device(BufferLimits limits);
    using NativeBuffer = uint64_t;
    virtual NativeBuffer createBufferImpl(const BufferDesc&, const void*) = 0;
    virtual void destroyBufferImpl(NativeBuffer) noexcept = 0;
    virtual void writeBufferImpl(NativeBuffer, size_t, size_t, const void*) = 0;
    virtual void readBufferImpl(NativeBuffer, size_t, size_t, void*) = 0;
    virtual void bindUniformBufferImpl(uint32_t, NativeBuffer, size_t, size_t) = 0;
    virtual void beginFrameImpl() = 0;
    virtual void presentImpl() = 0;
    virtual void waitIdleImpl() = 0;
    virtual void closeImpl() = 0;
    virtual void releaseResourcesImpl() {}
    virtual uint64_t signalCompletionImpl(){waitIdleImpl();return 0;}
    virtual bool completionReadyImpl(uint64_t){return true;}
    virtual void waitCompletionImpl(uint64_t){waitIdleImpl();}
    NativeBuffer nativeBuffer(BufferHandle, BufferUsage required) const;
    const BufferDesc& bufferDesc(BufferHandle) const;

private:
    struct Record { BufferDesc desc; NativeBuffer native; };
    const Record& buffer(BufferHandle) const;
    void requireOpen() const;
    struct FrameRecord{uint64_t serial,native;std::vector<std::function<void()>> retirements;};
    void collectFrames(bool all=false);
    void runRetirements(std::vector<std::function<void()>>&);
    uint64_t identity_=0,nextSerial_=1,completedSerial_=0;
    size_t maxFrames_=3;
    bool frameActive_=false,retiring_=false;
    std::deque<FrameRecord> frames_;
    std::vector<std::function<void()>> retirements_;
    BufferLimits limits_;
    bool open_ = true;
    std::thread::id ownerThread_=std::this_thread::get_id();
    std::unordered_map<uint64_t, Record> buffers_;
    ResourceMemoryStats memory_;
};

// Installed by the native backend after its device/context is ready.
void installDevice(std::shared_ptr<Device>);
std::shared_ptr<Device> device();
void shutdown();
bool usesNativeRenderer();
void useLegacyRenderer(bool);
bool legacyRendererRequested();
Backend requestedBackend();
void requestBackend(Backend);
#ifndef SCENERENDERER_METAL
std::shared_ptr<Device> makeOpenGLDevice(GLFWwindow*);
#endif
} // namespace rhi
