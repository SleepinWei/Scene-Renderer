#include "rhi/Validation.h"
#include "rhi/Device.h"
#include <array>
#include <stdexcept>

namespace rhi {
void validateBufferTransfers(Device& device, bool legacyBinding) {
    std::array<uint8_t, 512> expected{}, actual{};
    for (size_t i = 0; i < expected.size(); ++i) expected[i] = uint8_t(i);
    auto buffer = device.createBuffer({expected.size(),
        BufferUsage::Uniform | BufferUsage::CopySource | BufferUsage::CopyDestination,
        "RHI buffer transfer validation"}, expected.data());
    try {
        device.readBuffer(buffer, 0, actual.size(), actual.data());
        if (actual != expected) throw std::runtime_error("RHI: initial buffer upload mismatch");
        std::array<uint8_t, 32> patch{};
        patch.fill(173);
        device.beginFrame();
        device.writeBuffer(buffer, 16, patch.size(), patch.data());
        for (size_t i = 0; i < patch.size(); ++i) expected[16 + i] = patch[i];
        if (legacyBinding) device.bindUniformBuffer(0, buffer);
        if(legacyBinding || device.backend()==Backend::OpenGL)device.present();else device.endFrame();
        device.readBuffer(buffer, 0, actual.size(), actual.data());
        if (actual != expected) throw std::runtime_error("RHI: partial buffer upload mismatch");
        if (legacyBinding) device.bindUniformBuffer(0, {});
        device.destroyBuffer(buffer);
    } catch (...) {
        if (legacyBinding) device.bindUniformBuffer(0, {});
        device.destroyBuffer(buffer);
        throw;
    }
}
}
