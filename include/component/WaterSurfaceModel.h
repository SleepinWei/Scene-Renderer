#pragma once
#include <cmath>
#include <stdexcept>
struct WaterSurfaceModel {
    bool physical=false;
    float roughness=0;
    static constexpr float ior=1.333f;
    void validate(bool refraction=true) const {
        if(!std::isfinite(roughness)||roughness<0||roughness>1)
            throw std::invalid_argument("Water surface roughness must be finite and in [0,1]");
        if(physical&&!refraction)
            throw std::invalid_argument("Physical water surface requires the dielectric/refraction interface");
    }
    // Match the existing PT dielectric closure's delta/continuous threshold.
    float effectiveRoughness() const { return roughness<.02f?0:roughness; }
};
