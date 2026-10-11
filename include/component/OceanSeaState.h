#pragma once
#include <glm/glm.hpp>
#include <cmath>
#include <stdexcept>
// Statistical sea state. Hs = 4 sqrt(height variance), before HeightScale.
// Directions point toward wave propagation in world XZ; depth=0 is deep water.
struct OceanSeaState {
    bool enabled=false;
    float windHeight=1.6f,windPeriod=5.5f,windGamma=3.3f,windSpread=4;
    float swellHeight=1.2f,swellPeriod=8,swellSpread=20;
    glm::vec2 swellDirection{-.8f,.6f};
    float depth=0;
    void validate() const {
        for(float v:{windHeight,windPeriod,windGamma,windSpread,swellHeight,swellPeriod,swellSpread,swellDirection.x,swellDirection.y,depth})
            if(!std::isfinite(v))throw std::invalid_argument("Ocean: nonfinite sea state");
        if(windHeight<0||swellHeight<0||windHeight>20||swellHeight>20||windPeriod<1||windPeriod>30||swellPeriod<1||swellPeriod>30||
           windGamma<1||windGamma>10||windSpread<0||windSpread>100||swellSpread<0||swellSpread>100||depth<0||depth>1e6f||(depth>0&&depth<.1f))
            throw std::invalid_argument("Ocean: invalid sea state");
        if(enabled&&swellHeight>0&&glm::length(swellDirection)<1e-6f)throw std::invalid_argument("Ocean: swell needs a propagation direction");
    }
};
