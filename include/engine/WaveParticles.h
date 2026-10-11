#pragma once
#include "component/WaveParticleSettings.h"
namespace engine {
struct WaveParticle {
    glm::vec2 position,direction;
    float amplitude,radius,spread,travel=0,age=0;
};
// Bounded, deterministic fixed-step wavefront simulation. Rendering bins are
// rebuilt separately; particles remain in world space as the camera moves.
class WaveParticles {
public:
    void advance(double seconds,const WaveParticleSettings&);
    void clear();
    const std::vector<WaveParticle>& particles()const{return particles_;}
    uint32_t rejectedSources()const{return rejected_;}
    uint32_t deferredSplits()const{return deferred_;}
    static glm::vec3 kernel(const WaveParticle&,glm::vec2); // height,dH/dx,dH/dz
    glm::vec3 sample(glm::vec2)const;
private:
    void emit(const WaveImpulse&,const WaveParticleSettings&);
    void step(float,const WaveParticleSettings&);
    std::vector<WaveParticle> particles_;
    double time_=-1;uint32_t rejected_=0,deferred_=0;
};
}
