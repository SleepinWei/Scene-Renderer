#pragma once
#include "renderer/rhi/ForwardPbrRenderer.h"
namespace render {
struct BakedAtmosphere {
    uint32_t width = 512, height = 256;
    std::vector<glm::vec3> radiance; // Equirectangular HDR, top row north, longitude zero -Z.
    glm::vec3 sunDirection{0,1,0}, sunIrradiance{0};
    float sunRadius = 0;
};
// Call on the device owner thread. Sky scattering excludes the analytic solar disk.
BakedAtmosphere bakeAtmosphere(std::shared_ptr<rhi::GraphicsDevice>, const FrameData &);
} // namespace render
