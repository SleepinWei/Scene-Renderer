#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cmath>
#include <cstdint>
#include <stdexcept>
struct WaveImpulse {
    uint64_t id=1;double time=0;
    glm::vec2 position{0},direction{1,0};
    float amplitude=.3f,radius=1.5f,spread=6.2831853f;
};
struct WaveEmitter {
    glm::vec2 position{0},velocity{2,0};
    double begin=0,end=60;float interval=.2f,amplitude=.12f,radius=1.5f;
};
struct WaveParticleSettings {
    bool enabled=false,reflectBounds=false;
    uint32_t resolution=256,capacity=8192;
    float length=64,speed=3,damping=.15f,lifetime=16,minAmplitude=.0001f;
    glm::vec2 boundaryMin{-32},boundaryMax{32};
    std::vector<WaveImpulse> impulses;
    std::vector<WaveEmitter> emitters;
    void validate()const {
        if(resolution<32||resolution>512||resolution%16||capacity<32||capacity>65536||impulses.size()>256||emitters.size()>16)throw std::invalid_argument("Wave particle resource budget");
        for(float v:{length,speed,damping,lifetime,minAmplitude,boundaryMin.x,boundaryMin.y,boundaryMax.x,boundaryMax.y})if(!std::isfinite(v))throw std::invalid_argument("Nonfinite wave particles");
        if(length<8||length>512||speed<=0||speed>30||damping<0||damping>5||lifetime<=0||lifetime>60||minAmplitude<=0||boundaryMax.x<=boundaryMin.x||boundaryMax.y<=boundaryMin.y||(reflectBounds&&(boundaryMax.x-boundaryMin.x<1||boundaryMax.y-boundaryMin.y<1))||glm::length(boundaryMin)>1e6f||glm::length(boundaryMax)>1e6f)throw std::invalid_argument("Invalid wave particles");
        auto source=[&](glm::vec2 p,float a,float r){if(!std::isfinite(p.x)||!std::isfinite(p.y)||glm::length(p)>1e6f||!std::isfinite(a)||!std::isfinite(r)||std::abs(a)>2||r<.2f||r>length/8)throw std::invalid_argument("Invalid wave source");};
        for(size_t i=0;i<impulses.size();i++){auto& q=impulses[i];source(q.position,q.amplitude,q.radius);
            if(!q.id||!std::isfinite(q.time)||q.time<0||q.time>1e7||!std::isfinite(q.spread)||q.spread<=0||q.spread>6.283186f||!std::isfinite(q.direction.x)||!std::isfinite(q.direction.y)||glm::length(q.direction)<.001f)throw std::invalid_argument("Invalid wave impulse");
            for(size_t j=0;j<i;j++)if(impulses[j].id==q.id)throw std::invalid_argument("Duplicate wave impulse ID");}
        for(auto& q:emitters){source(q.position,q.amplitude,q.radius);if(!std::isfinite(q.begin)||!std::isfinite(q.end)||!std::isfinite(q.interval)||q.begin<0||q.end<q.begin||q.end>1e7||q.interval<.05f||!std::isfinite(q.velocity.x)||!std::isfinite(q.velocity.y)||glm::length(q.velocity)>30)throw std::invalid_argument("Invalid wave emitter");}
    }
};
