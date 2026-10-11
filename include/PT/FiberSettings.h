#pragma once
#include "renderer/rhi/SceneSnapshot.h"
#include <cmath>
#include <stdexcept>
namespace pt {
inline void validateFiberSettings(const render::SnapshotDraw& d){
    const auto k=d.pathTracingKnit;
    for(float x:{k.x,k.y,k.z,k.w})if(!std::isfinite(x))throw std::invalid_argument("PT: nonfinite knit parameters");
    if(k.x<0||k.x>1||k.y<1||k.y>512||k.z<1||k.z>512||k.w<.04f||k.w>.3f||(k.x>0&&d.pathTracingBsdfModel!=4))throw std::invalid_argument("PT: knit parameters require cloth and supported stitch density/radius");
    if(d.pathTracingSheen.w!=0&&d.pathTracingBsdfModel!=4)throw std::invalid_argument("PT: yarn specular requires cloth/knit/yarn closure");
    if(d.pathTracingBsdfModel<4)return;
    const auto h=d.pathTracingFiber,s=d.pathTracingSheen;auto c=d.pathTracingSheenColor;
    for(float x:{h.x,h.y,h.z,h.w,s.x,s.y,s.z,s.w,c.x,c.y,c.z})if(!std::isfinite(x))throw std::invalid_argument("PT: nonfinite fiber parameters");
    if(h.x<.1f||h.x>.8f||h.y<.1f||h.y>.8f||h.z<=1||h.z>2.5f||std::abs(h.w)>.2f||s.x<0||s.x>1||s.y<0||s.y>1||s.z<0||s.z>1||s.w<0||s.w>1||glm::any(glm::lessThan(c,glm::vec3(0)))||glm::any(glm::greaterThan(c,glm::vec3(1))))throw std::invalid_argument("PT: fiber parameters outside supported range");
    if(d.pathTracingIor!=0||d.pathTracingKind!=0||d.extension.specularShape.z!=0||d.pathTracingSpecularAnisotropy!=0||d.parameters.factors.x!=0||glm::any(glm::greaterThan(d.pathTracingScattering,glm::vec3(0))))throw std::invalid_argument("PT: fiber closures require nonmetal thin geometry without dielectric/volume/GGX override");
}
}
