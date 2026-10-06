#pragma once
#include <glm/glm.hpp>
#include <cstdint>

// Metres and seconds; detached values shared by the editor and native renderer.
struct ShoreWaterSettings {
    bool enabled=false,foam=true,wetSand=true;
    bool boundaryForcing=true; // Closed reflective mode is used by conservation fixtures.
    uint32_t resolution=256;
    float length=128,maxDepth=8;
    float swellHeight=.3f,swellPeriod=6;
    glm::vec2 swellDirection{0,1};
    float foamStrength=1,foamLifetime=8,dryingTime=25;
};
