#include "rhi/GraphicsDevice.h"
#include "rhi/Validation.h"
#include "rhi/ShaderAssets.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "renderer/rhi/GpuOcean.h"
#include "renderer/rhi/GpuTerrain.h"
#include "renderer/rhi/GpuClouds.h"
#include "renderer/rhi/GpuSubdivision.h"
#include "renderer/rhi/GpuDdgi.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>
#include <iostream>
int main(int argc,char** argv) {
    try {
        auto device = rhi::makeVulkanDevice();rhi::installDevice(device);
        if(argc>1 && std::string(argv[1])=="--ddgi-self-test"){render::validateDdgi(device,SR_RHI_SHADER_DIR);rhi::shutdown();return 0;}
        rhi::validateBufferTransfers(*device, false);rhi::validateTexturedRendering(*device);
        render::validateForwardRendering(device, SR_RHI_SHADER_DIR);
        render::validateSceneEffects(device,SR_RHI_SHADER_DIR);
        render::validateAtmosphereRhi(device,SR_RHI_SHADER_DIR);
        render::validateCloudsRhi(device,SR_RHI_SHADER_DIR);
        render::validateTerrainRhi(device,SR_RHI_SHADER_DIR);
        render::validateSubdivisionRhi(device,rhi::defaultShaderDirectory());
        render::validateTemporalRhi(device,SR_RHI_SHADER_DIR);
        render::validatePostProcessing(device,SR_RHI_SHADER_DIR);
        rhi::validateComputeAndIndirect(*device);
        render::validateOceanRhi(device,SR_RHI_SHADER_DIR);
            rhi::validateFrameLifecycle(device);
        rhi::shutdown();return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';try { rhi::shutdown(); } catch (...) {}return 1;
    }
}
