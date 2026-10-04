#pragma once
#include "PT/Sampler.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
namespace pt {
struct Medium {glm::vec3 absorption{0},scattering{0};float g=0;};
struct MediumSample {float distance=0;glm::vec3 weight{1};bool scattered=false;};
inline glm::vec3 transmittance(glm::vec3 extinction,float distance){
    if(!std::isfinite(distance)||distance==std::numeric_limits<float>::max())return {extinction.x>0?0.f:1.f,extinction.y>0?0.f:1.f,extinction.z>0?0.f:1.f};
    return glm::exp(-glm::min(extinction*distance,glm::vec3(80)));
}
// RGB channel mixture: both collision and boundary probabilities use the same
// spectral average. Absorption-only segments remain deterministic.
inline MediumSample sampleMedium(const Medium &m,float maximum,Random &random){
    if(glm::all(glm::equal(m.scattering,glm::vec3(0))))return {maximum,transmittance(m.absorption,maximum),false};
    const auto extinction=m.absorption+m.scattering;int channel=std::min(int(random.uniform()*3),2);float u=random.uniform();
    const float distance=extinction[channel]>0?-std::log(std::max(1e-7f,1-u))/extinction[channel]:std::numeric_limits<float>::infinity();
    const bool collision=distance<maximum;const float t=std::min(distance,maximum);const auto T=transmittance(extinction,t),density=collision?T*extinction:T;const float pdf=(density.x+density.y+density.z)/3;
    return {t,pdf>0?(collision?T*m.scattering:T)/pdf:glm::vec3(0),collision};
}
inline float phaseHG(float cosine,float g){float d=std::max(1e-8f,1+g*g-2*g*cosine);return (1-g*g)/(12.566370614359172f*d*std::sqrt(d));}
inline glm::vec3 samplePhaseHG(glm::vec3 forward,float g,Random &random){
    auto uv=random.uniform2();float cosine=2*uv[0]-1;if(std::abs(g)>.001f){float s=(1-g*g)/(1-g+2*g*uv[0]);cosine=glm::clamp((1+g*g-s*s)/(2*g),-1.f,1.f);}
    else{float x=cosine;cosine=glm::clamp((2*x+g*(x*x+3)+2*g*g*x+g*g*g*(x*x-1))/(2*(1+g*x)*(1+g*x)),-1.f,1.f);}
    float sine=std::sqrt(std::max(0.f,1-cosine*cosine)),phi=6.283185307179586f*uv[1];auto tangent=glm::normalize(glm::cross(std::abs(forward.y)<.99f?glm::vec3(0,1,0):glm::vec3(1,0,0),forward));
    return tangent*(sine*std::cos(phi))+glm::cross(forward,tangent)*(sine*std::sin(phi))+forward*cosine;
}
}
