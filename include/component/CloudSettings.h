#pragma once
#include <glm/glm.hpp>
#include <cstdint>
namespace render {
struct CloudSettings {
    bool enabled=false, temporal=true;
    float baseHeight=1200, thickness=1600, coverage=.55f, density=.006f;
    float shapeScale=3500, weatherScale=45000, erosion=.28f, maxDistance=60000;
    glm::vec2 wind{12,4};
    uint32_t steps=72, lightSteps=6, downsample=2, seed=7;
    void validate() const;
    bool operator==(const CloudSettings&) const;
};
}
