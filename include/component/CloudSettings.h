#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <optional>
namespace render {
struct CloudSettings {
    bool enabled=false, temporal=true, voxel=true, distanceSkipping=true,coreIntegration=true;
    float baseHeight=1200, thickness=1600, coverage=.55f, density=.006f;
    float shapeScale=3500, weatherScale=45000, erosion=.28f, maxDistance=60000;
    glm::vec2 wind{12,4};
    // Runtime snapshot input; scene JSON keeps only the baseline wind.
    std::optional<glm::dvec2> advection;
    bool dynamicCoverage=false; // Runtime envelope/light cache for continuous coverage; not scene JSON.
    glm::vec3 volumeCenter{0,2200,-4000},volumeSize{6000,3600,6000};
    uint32_t voxelResolution=128;
    float storm=0,lightning=0;
    uint32_t steps=72, lightSteps=6, downsample=2, seed=7;
    void validate() const;
    bool operator==(const CloudSettings&) const;
};
}
