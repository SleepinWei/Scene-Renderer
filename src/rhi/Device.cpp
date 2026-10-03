#include "rhi/Device.h"
#include <atomic>
#include <stdexcept>

namespace rhi {
namespace {
std::shared_ptr<Device> active;
bool legacy=false;
#if defined(SCENERENDERER_DEFAULT_VULKAN)
Backend requested=Backend::Vulkan;
#elif defined(SCENERENDERER_METAL)
Backend requested=Backend::Metal;
#else
Backend requested=Backend::OpenGL;
#endif
std::atomic<uint64_t> nextHandle{1};
void checkRange(size_t capacity, size_t offset, size_t size) {
    if (offset > capacity || size > capacity - offset)
        throw std::out_of_range("RHI: buffer range exceeds allocation");
}
}
Device::Device(BufferLimits limits) : identity_(nextHandle.fetch_add(1)),limits_(limits) {
    if (!limits.maxBufferSize || !limits.maxUniformBufferSize ||
        !limits.uniformOffsetAlignment || !limits.maxUniformBindings)
        throw std::invalid_argument("RHI: invalid buffer limits");
}
void Device::requireOpen() const {
    if (!open_) throw std::logic_error("RHI: device is closed");
}
const Device::Record& Device::buffer(BufferHandle handle) const {
    requireOpen();
    auto found = buffers_.find(handle.value);
    if (found == buffers_.end()) throw std::invalid_argument("RHI: stale or foreign buffer handle");
    return found->second;
}
BufferHandle Device::createBuffer(const BufferDesc& desc, const void* initialData) {
    requireOpen();
    constexpr uint32_t allUsage = 127;
    if (!desc.size || desc.size > limits_.maxBufferSize || desc.usage == BufferUsage::None ||
        (static_cast<uint32_t>(desc.usage) & ~allUsage))
        throw std::invalid_argument("RHI: invalid buffer descriptor");
    if (hasUsage(desc.usage, BufferUsage::Uniform) && desc.size > limits_.maxUniformBufferSize)
        throw std::invalid_argument("RHI: uniform buffer exceeds device limit");
    const auto native = createBufferImpl(desc, initialData);
    const BufferHandle handle{nextHandle.fetch_add(1)};
    try { buffers_.emplace(handle.value, Record{desc, native}); }
    catch (...) { destroyBufferImpl(native); throw; }
    return handle;
}
void Device::destroyBuffer(BufferHandle handle) noexcept {
    const auto found = buffers_.find(handle.value);
    if (found == buffers_.end()) return;
    try{waitForResourceRelease();}catch(...){return;}
    destroyBufferImpl(found->second.native);
    buffers_.erase(found);
}
void Device::writeBuffer(BufferHandle handle, size_t offset, size_t size, const void* data) {
    const auto& record = buffer(handle);
    checkRange(record.desc.size, offset, size);
    if (!hasUsage(record.desc.usage, BufferUsage::CopyDestination))
        throw std::invalid_argument("RHI: buffer lacks CopyDestination usage");
    if (size && !data) throw std::invalid_argument("RHI: null upload data");
    if (size) writeBufferImpl(record.native, offset, size, data);
}
void Device::readBuffer(BufferHandle handle, size_t offset, size_t size, void* data) {
    const auto& record = buffer(handle);
    checkRange(record.desc.size, offset, size);
    if (!hasUsage(record.desc.usage, BufferUsage::CopySource))
        throw std::invalid_argument("RHI: buffer lacks CopySource usage");
    if (size && !data) throw std::invalid_argument("RHI: null readback data");
    if (size) readBufferImpl(record.native, offset, size, data);
}
void Device::bindUniformBuffer(uint32_t slot, BufferHandle handle, size_t offset, size_t size) {
    requireOpen();
    if (slot >= limits_.maxUniformBindings) throw std::out_of_range("RHI: uniform slot exceeds device limit");
    if (!handle) {
        if (offset || size) throw std::invalid_argument("RHI: range supplied for null uniform buffer");
        bindUniformBufferImpl(slot, 0, 0, 0); return;
    }
    const auto& record = buffer(handle);
    if (!hasUsage(record.desc.usage, BufferUsage::Uniform))
        throw std::invalid_argument("RHI: buffer lacks Uniform usage");
    checkRange(record.desc.size, offset, size);
    if (!size) size = record.desc.size - offset;
    if (!size || offset % limits_.uniformOffsetAlignment)
        throw std::invalid_argument("RHI: empty or misaligned uniform range");
    bindUniformBufferImpl(slot, record.native, offset, size);
}
void Device::runRetirements(std::vector<std::function<void()>>& work){
    const bool previous=retiring_;retiring_=true;
    try{for(auto& callback:work)callback();work.clear();retiring_=previous;}catch(...){retiring_=previous;throw;}
}
void Device::collectFrames(bool all){
    while(!frames_.empty() && (all || completionReadyImpl(frames_.front().native))){auto frame=std::move(frames_.front());frames_.pop_front();runRetirements(frame.retirements);completedSerial_=frame.serial;}
}
void Device::setMaxFramesInFlight(size_t count){requireOpen();if(!count || count>3 || frameActive_)throw std::invalid_argument("RHI: frame count must be 1..3 outside a frame");waitIdle();maxFrames_=count;}
void Device::beginFrame(){
    requireOpen();if(frameActive_)throw std::logic_error("RHI: frame already active");collectFrames();
    if(frames_.size()>=maxFrames_){waitCompletionImpl(frames_.front().native);collectFrames();}
    frameActive_=backend()!=Backend::OpenGL && !legacy;try{beginFrameImpl();}catch(...){frameActive_=false;throw;}
}
CompletionToken Device::checkpoint(std::function<void()> callback){
    requireOpen();collectFrames();if(frames_.size()>=maxFrames_){waitCompletionImpl(frames_.front().native);collectFrames();}
    const auto native=signalCompletionImpl(),serial=nextSerial_++;FrameRecord record{serial,native,{}};if(callback)record.retirements.push_back(std::move(callback));frames_.push_back(std::move(record));collectFrames();return {identity_,serial};
}
CompletionToken Device::endFrame(){
    requireOpen();if(!frameActive_)throw std::logic_error("RHI: no native frame active");
    collectFrames();if(frames_.size()>=maxFrames_){waitCompletionImpl(frames_.front().native);collectFrames();}
    const auto native=signalCompletionImpl(),serial=nextSerial_++;
    frames_.push_back({serial,native,std::move(retirements_)});retirements_.clear();frameActive_=false;collectFrames();return {identity_,serial};
}
void Device::present(){requireOpen();presentImpl();if(frameActive_)endFrame();}
bool Device::isComplete(CompletionToken token){
    requireOpen();if(token.device!=identity_ || !token.serial || token.serial>=nextSerial_)throw std::invalid_argument("RHI: foreign or future completion token");collectFrames();return token.serial<=completedSerial_;
}
void Device::wait(CompletionToken token){
    if(isComplete(token))return;for(const auto& frame:frames_)if(frame.serial==token.serial){waitCompletionImpl(frame.native);collectFrames();return;}throw std::invalid_argument("RHI: missing completion token");
}
void Device::retireResources(std::function<void()> callback){
    requireOpen();if(!callback)return;
    if(frameActive_)retirements_.push_back(std::move(callback));else if(!frames_.empty())frames_.back().retirements.push_back(std::move(callback));else{std::vector<std::function<void()>> work;work.push_back(std::move(callback));runRetirements(work);}
}
void Device::waitIdle(){requireOpen();waitIdleImpl();collectFrames(true);runRetirements(retirements_);}
void Device::waitForResourceRelease(){if(!retiring_ && (frameActive_ || !frames_.empty()))waitIdle();}
void Device::close() {
    if (!open_) return;
    waitIdle();
    frameActive_=false;retiring_=true;
    releaseResourcesImpl();
    while (!buffers_.empty()) destroyBuffer(BufferHandle{buffers_.begin()->first});
    closeImpl();
    open_ = false;
}
Device::NativeBuffer Device::nativeBuffer(BufferHandle handle, BufferUsage required) const {
    const auto& record = buffer(handle);
    if (!hasUsage(record.desc.usage, required)) throw std::invalid_argument("RHI: incompatible buffer usage");
    return record.native;
}
const BufferDesc& Device::bufferDesc(BufferHandle handle) const { return buffer(handle).desc; }
bool usesNativeRenderer(){return active && active->isOpen() && active->backend()!=Backend::OpenGL && !legacy;}
void useLegacyRenderer(bool value){legacy=value;}
bool legacyRendererRequested(){return legacy;}
Backend requestedBackend(){return requested;}
void requestBackend(Backend value){if(active && active->isOpen())throw std::logic_error("RHI: backend already initialized");requested=value;}
void installDevice(std::shared_ptr<Device> value) {
    if (!value || !value->isOpen() || (active && active->isOpen()))
        throw std::logic_error("RHI: cannot install device");
    active = std::move(value);
}
std::shared_ptr<Device> device() {
    if (!active || !active->isOpen()) throw std::logic_error("RHI: device has not been initialized");
    return active;
}
void shutdown() {
    if (active) active->close();
    active.reset();
}
} // namespace rhi
