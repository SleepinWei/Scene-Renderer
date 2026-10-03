#include "buffer/UniformBuffer.h"
#include <stdexcept>

UniformBuffer::UniformBuffer(int bytes) : size(bytes), binding(-1), dirty(true), owner_(rhi::device()) {
    if (bytes <= 0) throw std::invalid_argument("UniformBuffer: size must be positive");
    handle_ = owner_->createBuffer({size_t(bytes),
        rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDestination,
        "Renderer uniform buffer"});
}
UniformBuffer::~UniformBuffer() { owner_->destroyBuffer(handle_); }
void UniformBuffer::write(size_t offset, size_t bytes, const void* data) {
    owner_->writeBuffer(handle_, offset, bytes, data);
}
void UniformBuffer::setBinding(int slot) {
    if (slot < 0) throw std::invalid_argument("UniformBuffer: negative binding slot");
    owner_->bindUniformBuffer(uint32_t(slot), handle_);
    binding = slot;
}
