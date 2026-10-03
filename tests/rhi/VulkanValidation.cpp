#include "rhi/GraphicsDevice.h"
#include "rhi/Validation.h"
#include "rhi/ShaderAssets.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "renderer/rhi/GpuOcean.h"
#include "renderer/rhi/GpuTerrain.h"
#include "renderer/rhi/GpuSubdivision.h"
#include <iostream>
int main() {
    try {
        auto device = rhi::makeVulkanDevice();rhi::installDevice(device);
        rhi::validateBufferTransfers(*device, false);rhi::validateTexturedRendering(*device);
        render::validateForwardRendering(device, SR_RHI_SHADER_DIR);
        render::validateSceneEffects(device,SR_RHI_SHADER_DIR);
        render::validateAtmosphereRhi(device,SR_RHI_SHADER_DIR);
        render::validateTerrainRhi(device,SR_RHI_SHADER_DIR);
        render::validateSubdivisionRhi(device,rhi::defaultShaderDirectory());
        render::validateTemporalRhi(device,SR_RHI_SHADER_DIR);
        rhi::validateComputeAndIndirect(*device);
        render::validateOceanRhi(device,SR_RHI_SHADER_DIR);
            rhi::validateFrameLifecycle(device);
        rhi::shutdown();return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';try { rhi::shutdown(); } catch (...) {}return 1;
    }
}
