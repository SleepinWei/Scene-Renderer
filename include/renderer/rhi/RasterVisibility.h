#pragma once
#include "renderer/rhi/ForwardPbrRenderer.h"
#include <algorithm>
#include <array>
#include <cmath>
namespace render {
// Conservative homogeneous clip test for static affine geometry, Z in [0,W].
// Unknown/generated bounds and invalid transforms stay visible so the renderer
// still performs its normal validation. No camera list may feed auxiliary views.
inline bool rasterPacketVisible(const DrawPacket& packet,const FrameData& frame) {
    if(!packet.mesh || !packet.mesh->ready() || packet.mesh->indirectBuffer() || !packet.material ||
       packet.material->transparent() || packet.material->extension().lobes.w!=0 ||
       !frame.viewportWidth || !frame.viewportHeight) return true;
    const auto& model=packet.model;
    if(model[0].w!=0 || model[1].w!=0 || model[2].w!=0 || model[3].w!=1)return true;
    for(int c=0;c<4;++c)for(int r=0;r<4;++r)
        if(!std::isfinite(model[c][r]) || !std::isfinite(frame.viewProjection[c][r]))return true;
    const auto determinant=glm::determinant(model);
    if(!std::isfinite(determinant) || std::abs(determinant)<1e-12f)return true;
    auto inverse=glm::inverse(model);
    for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(!std::isfinite(inverse[c][r]))return true;
    const auto a=packet.mesh->boundsMin(),b=packet.mesh->boundsMax();
    for(int i=0;i<3;++i)if(!std::isfinite(a[i]) || !std::isfinite(b[i]) || a[i]>b[i])return true;
    const auto clip=glm::dmat4(frame.viewProjection)*glm::dmat4(model);
    const auto center=glm::dvec4((glm::dvec3(a)+glm::dvec3(b))*.5,1);
    const auto extent=(glm::dvec3(b)-glm::dvec3(a))*.5;
    const auto localMagnitude=glm::dvec4(glm::max(glm::abs(glm::dvec3(a)),glm::abs(glm::dvec3(b))),1);
    glm::dvec4 clipMagnitude(0),worldMagnitude(0),error(0);
    for(int c=0;c<4;++c){clipMagnitude+=glm::abs(clip[c])*localMagnitude[c];worldMagnitude+=glm::abs(glm::dvec4(model[c]))*localMagnitude[c];}
    for(int c=0;c<4;++c)error+=glm::abs(glm::dvec4(frame.viewProjection[c]))*worldMagnitude[c];
    double scale=1,roundoff=0;
    for(int j=0;j<4;++j){if(!std::isfinite(clipMagnitude[j]) || !std::isfinite(error[j]))return true;scale=std::max(scale,clipMagnitude[j]);roundoff=std::max(roundoff,error[j]);}
    // Two pixels of lateral guard plus relative roundoff padding. Never divide
    // by W: crossing the eye or near plane is handled conservatively.
    const double x=1+4.0/frame.viewportWidth,y=1+4.0/frame.viewportHeight,epsilon=scale*1e-6+roundoff*(1+std::max(x,y))*1e-6;
    const auto rows=glm::transpose(clip);
    const std::array<glm::dvec4,6> planes{rows[0]+x*rows[3],x*rows[3]-rows[0],rows[1]+y*rows[3],y*rows[3]-rows[1],rows[2],rows[3]-rows[2]};
    for(const auto& plane:planes){
        // AABB support point gives the maximal signed distance without eight
        // corner transforms, including nonuniform / mirrored affine models.
        if(glm::dot(plane,center)+glm::dot(glm::abs(glm::dvec3(plane)),extent)<-epsilon)return false;
    }
    return true;
}
}
