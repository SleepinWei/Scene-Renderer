#pragma once
#include <glm/glm.hpp>
#include <cmath>
#include <stdexcept>
#include <cstdint>
namespace render {
// Bounded local volume; PT reference capture reuses the camera integrator.
struct DdgiSettings {
    bool enabled=false, visibility=true, multiBounce=true;
    bool freezeUpdates=false; // Keep a converged field identical across diagnostic surface variants.
    bool pathTracedCapture=false;
    int pathSamples=4,pathDepth=8;
    int debugProbes=0,debugLayer=-1; // 0 off, 1 directional E/pi, 2 classification; -1 all Y layers.
    bool debugXray=false;
    float debugRadius=.16f;
    glm::vec3 origin{-12,-2,-12};
    glm::ivec3 counts{8,5,8};
    float spacing=4, normalBias=.12f, viewBias=.04f, visibilityPower=3;
    float hysteresis=.85f, intensity=1, bounceGain=.85f;
    int probesPerFrame=2, captureResolution=12;
    float maxDistance=60, backfaceThreshold=.25f;
    uint32_t resetSerial=0;
    // Directional cache: 8x8 irradiance + 16x16 distance moments, both octahedral.
    static constexpr uint32_t directionCount=64, distanceCount=256, stride=321;
    uint32_t probeCount() const {return uint32_t(counts.x)*uint32_t(counts.y)*uint32_t(counts.z);}
    size_t cacheBytes() const {return size_t(probeCount())*stride*16*2;}
    void validate() const {
        if(debugProbes<0||debugProbes>2||debugLayer<-1||debugLayer>=counts.y||!std::isfinite(debugRadius)||debugRadius<.01f||debugRadius>10)
            throw std::invalid_argument("DDGI probe visualization outside domain");
        if(glm::any(glm::lessThan(counts,glm::ivec3(2)))||glm::any(glm::greaterThan(counts,glm::ivec3(32))))throw std::invalid_argument("DDGI grid must have 2..32 probes per axis");
        if(!std::isfinite(origin.x)||!std::isfinite(origin.y)||!std::isfinite(origin.z))throw std::invalid_argument("DDGI origin must be finite");
        for(float v:{spacing,normalBias,viewBias,visibilityPower,hysteresis,intensity,bounceGain,maxDistance,backfaceThreshold})if(!std::isfinite(v))throw std::invalid_argument("DDGI parameters must be finite");
        if(visibilityPower<1||visibilityPower>8||spacing<=0||normalBias<0||viewBias<0||normalBias>spacing*.5f||viewBias>spacing*.5f||hysteresis<0||hysteresis>.98f||intensity<0||intensity>4||bounceGain<0||bounceGain>.95f||maxDistance<spacing||maxDistance>100000||backfaceThreshold<=0||backfaceThreshold>1)throw std::invalid_argument("DDGI parameters out of range");
        if(probesPerFrame<1||probesPerFrame>64||captureResolution<4||captureResolution>32)throw std::invalid_argument("DDGI update budget out of range");
        if(pathSamples<1||pathSamples>4096||pathDepth<1||pathDepth>32)throw std::invalid_argument("DDGI PT budget out of range");
        if(cacheBytes()>64*1024*1024)throw std::invalid_argument("DDGI local volume exceeds 64 MiB cache budget; reduce grid or use coarser spacing");
    }
};
struct DdgiStats {
    uint32_t probes=0,updated=0,sweepFrames=0,cursor=0;
    uint64_t cacheBytes=0,scratchBytes=0,frame=0;
};
}
