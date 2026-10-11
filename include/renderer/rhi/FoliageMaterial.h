#pragma once
#include "renderer/rhi/SceneSnapshot.h"
#include <algorithm>
#include <cmath>
namespace render {
inline uint8_t encodeFoliageByte(float v){return uint8_t(std::clamp(std::lround(v*255),0l,255l));}
inline void configureFoliagePathTracing(SnapshotDraw& draw){
    if(draw.pathTracingBsdfModel==3||!(draw.extension.features.x&int(MaterialFeature::Foliage))||!draw.material||!draw.material->images[0])return;
    auto material=std::make_shared<MaterialPayload>(*draw.material);auto reflection=std::make_shared<ImageRGBA8>(*material->images[0]);auto transmission=std::make_shared<ImageRGBA8>(*reflection);auto special=material->special;
    for(uint32_t y=0;y<reflection->height;y++)for(uint32_t x=0;x<reflection->width;x++){
        size_t i=(size_t(y)*reflection->width+x)*4;float weight=draw.extension.lobes.w;
        if(special&&special->width&&special->height){uint32_t sx=x*special->width/reflection->width,sy=y*special->height/reflection->height;weight*=special->pixels[(size_t(sy)*special->width+sx)*4+3]/255.f;}
        weight=std::clamp(weight,0.f,1.f);
        for(int c=0;c<3;c++){float albedo=std::pow(reflection->pixels[i+c]/255.f*draw.parameters.albedoAlpha[c],2.2f);reflection->pixels[i+c]=encodeFoliageByte(std::pow(albedo*(1-weight),1.f/2.2f));transmission->pixels[i+c]=encodeFoliageByte(std::pow(std::sqrt(albedo)*weight*.96f,1.f/2.2f));}
    }
    material->images[0]=reflection;draw.material=material;draw.parameters.albedoAlpha=glm::vec4(1,1,1,draw.parameters.albedoAlpha.w);draw.pathTracingBsdfModel=3;draw.extension.specularShape=glm::vec4(0);draw.pathTracingSpecularAnisotropy=draw.pathTracingSpecularRotation=0;draw.pathTracingDiffuseTransmission=glm::vec3(1);draw.pathTracingTransmissionTexture=transmission;draw.pathTracingNormalScale=draw.parameters.emissiveNormal.w;
}
}
