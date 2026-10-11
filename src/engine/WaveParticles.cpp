#include "engine/WaveParticles.h"
#include <algorithm>
namespace engine {
void WaveParticles::clear(){particles_.clear();time_=-1;rejected_=deferred_=0;}
glm::vec3 WaveParticles::kernel(const WaveParticle& p,glm::vec2 x){
    auto delta=x-p.position;float d=glm::length(delta);if(d>=p.radius)return glm::vec3(0);
    float k=3.14159265359f/p.radius;
    glm::vec2 gradient=d>1e-6f?delta*(-.5f*p.amplitude*k*std::sin(k*d)/d):glm::vec2(0);
    return {.5f*p.amplitude*(1+std::cos(k*d)),gradient.x,gradient.y};
}
glm::vec3 WaveParticles::sample(glm::vec2 x)const{glm::vec3 q(0);for(auto& p:particles_)q+=kernel(p,x);return q;}
void WaveParticles::emit(const WaveImpulse& q,const WaveParticleSettings& s){
    if(s.reflectBounds&&(q.position.x<s.boundaryMin.x||q.position.y<s.boundaryMin.y||q.position.x>s.boundaryMax.x||q.position.y>s.boundaryMax.y)){++rejected_;return;}
    const uint32_t n=std::max(1u,uint32_t(std::ceil(q.spread/.3926990817f)));
    if(particles_.size()+n>s.capacity){++rejected_;return;}
    const float angle=std::atan2(q.direction.y,q.direction.x);
    for(uint32_t i=0;i<n;i++){float a=angle+q.spread*((float(i)+.5f)/n-.5f);
        particles_.push_back({q.position,{std::cos(a),std::sin(a)},q.amplitude/float(n),q.radius,q.spread/float(n)});}
}
void WaveParticles::step(float dt,const WaveParticleSettings& s){
    particles_.erase(std::remove_if(particles_.begin(),particles_.end(),[&](auto& p){return p.age>=s.lifetime||std::abs(p.amplitude)<s.minAmplitude;}),particles_.end());
    const size_t original=particles_.size();
    for(size_t i=0;i<original;i++){
        auto& p=particles_[i];p.age+=dt;p.position+=p.direction*(s.speed*dt);p.travel+=s.speed*dt;p.amplitude*=std::exp(-s.damping*dt);
        if(s.reflectBounds)for(int axis=0;axis<2;axis++){
            // dt <= 1/60 and speed <= 30; repeat handles narrow user bounds.
            float span=s.boundaryMax[axis]-s.boundaryMin[axis];float u=(p.position[axis]-s.boundaryMin[axis])/span;
            int segment=int(std::floor(u));float f=u-std::floor(u);bool reversed=(segment%2)!=0;
            p.position[axis]=s.boundaryMin[axis]+span*(reversed?1-f:f);if(reversed)p.direction[axis]=-p.direction[axis];
        }
        if(p.travel*p.spread>p.radius*.5f){
            if(particles_.size()+2>s.capacity){++deferred_;continue;}
            auto parent=p;parent.amplitude/=3;parent.spread/=3;parent.travel=0;
            // Offset children along the widening arc, retaining radius and
            // the sum of amplitudes. New spread controls their next split.
            float a=std::atan2(parent.direction.y,parent.direction.x);
            WaveParticle left=parent,right=parent;float delta=p.spread/3;
            left.direction={std::cos(a-delta),std::sin(a-delta)};right.direction={std::cos(a+delta),std::sin(a+delta)};
            left.position+=p.travel*(left.direction-p.direction);right.position+=p.travel*(right.direction-p.direction);
            p=parent;particles_.push_back(left);particles_.push_back(right);
        }
    }
}
void WaveParticles::advance(double seconds,const WaveParticleSettings& s){
    s.validate();if(!std::isfinite(seconds)||seconds<0||seconds>1e7)throw std::invalid_argument("Invalid wave particle time");
    seconds=std::floor(seconds*60+1e-7)/60;
    if(seconds<time_||time_<0||seconds-time_>s.lifetime){clear();time_=std::max(0.,seconds-double(s.lifetime));}
    if(particles_.size()>s.capacity)particles_.resize(s.capacity);
    auto events=[&](double a,double b){
        for(auto& q:s.impulses)if(q.time>=a&&q.time<b)emit(q,s);
        for(auto& e:s.emitters){
            auto first=std::max(0ll,static_cast<long long>(std::ceil((a-e.begin)/e.interval-1e-8)));
            for(auto k=first;;k++){double t=e.begin+double(k)*e.interval;if(t>=b||t>e.end)break;
                if(t<a)continue;glm::vec2 direction=glm::length(e.velocity)>.001f?glm::normalize(e.velocity):glm::vec2(1,0);
                glm::vec2 at=e.position+e.velocity*float(t-e.begin);
                // Signed displaced-water pair with backward-facing wavefronts.
                emit({1,t,at+direction*e.radius*.35f,-direction,e.amplitude,e.radius,3.14159265f},s);
                emit({1,t,at-direction*e.radius*.35f,-direction,-e.amplitude,e.radius,3.14159265f},s);
            }
        }
    };
    // Absolute 60 Hz boundaries make results independent of render frame rate.
    while(time_+1e-9<seconds){double next=std::min(seconds,(std::floor(time_*60+1e-7)+1)/60);events(time_,next);step(float(next-time_),s);time_=next;}
}
}
