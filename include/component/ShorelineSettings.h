#pragma once
#include <array>
#include <string>
#include <cmath>
#include <stdexcept>
struct ShorelineSettings {
    bool enabled=false;
    float seaLevel=0,heightRange=24,wetBelow=1,wetAbove=3;
    float textureLength=30,slopeMin=.7f,slopeMax=.95f,normalStrength=1;
    std::array<std::string,4> paths; // diffuse mip atlas, normal mip atlas, ARM mip atlas, shore proximity.
    void validate() const {
        for(float v:{seaLevel,heightRange,wetBelow,wetAbove,textureLength,slopeMin,slopeMax,normalStrength})
            if(!std::isfinite(v))throw std::invalid_argument("Nonfinite shoreline setting");
        if(heightRange<=0 || wetBelow<0 || wetAbove<=0 || textureLength<=0 || slopeMin<0 ||
           slopeMax>1 || slopeMin>=slopeMax || normalStrength<0)
            throw std::invalid_argument("Invalid shoreline setting");
        if(enabled)for(auto& path:paths)if(path.empty())throw std::invalid_argument("Shoreline needs four textures");
    }
};
