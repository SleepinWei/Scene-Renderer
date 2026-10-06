#pragma once
#include "PT/CpuPathTracer.h"

namespace pt {
// Immutable std430 payload shared with Metal/Vulkan. Flux is unnormalized;
// normalization uses the number of launched paths, including every miss.
struct alignas(16) Photon {
    glm::vec4 positionDepth, normal, incoming, fluxCaustic;
};
struct alignas(16) PhotonCell { glm::ivec4 keyFirst; glm::uvec4 count; };
struct PhotonEstimate { glm::vec3 radiance{0}, caustics{0}; };
class PhotonMap {
  public:
    PhotonMap(const CpuScene &,const Options &);
    PhotonEstimate estimate(const Surface &,glm::vec3 view,uint32_t remainingDepth) const;
    const std::vector<Photon> &photons() const { return photons_; }
    const std::vector<PhotonCell> &cells() const { return cells_; }
    uint64_t rays=0, causticCount=0;
    double seconds=0;
    uint32_t paths=0;
    float radius=0;
    uint64_t memoryBytes() const;
  private:
    std::vector<Photon> photons_;
    std::vector<PhotonCell> cells_;
};
} // namespace pt
