#pragma once
#include "renderer/rhi/GpuMaterial.h"
#include <algorithm>
#include <cmath>
namespace render {
inline uint8_t rainWetCell(const MaterialExtension& e,size_t cell){
    const size_t word=cell/4;return uint8_t((e.rainWetCells[word/4][word%4]>>((cell%4)*8))&255u);
}
inline void setRainWetCell(MaterialExtension& e,size_t cell,uint8_t wet){
    const size_t word=cell/4,shift=(cell%4)*8;auto& value=e.rainWetCells[word/4][word%4];
    value=(value&~(255u<<shift))|(uint32_t(wet)<<shift);
}
inline float rainWetness(const MaterialExtension& e,glm::vec2 uv){
    if(e.features.x&int(MaterialFeature::SpatialRainWetness)){
        const glm::vec2 p=glm::clamp(uv,glm::vec2(0),glm::vec2(1))*16.f-.5f,f=glm::fract(p);
        const glm::ivec2 a(glm::floor(p));auto cell=[&](int x,int y){return rainWetCell(e,size_t(std::clamp(y,0,15))*16+size_t(std::clamp(x,0,15)))/255.f;};
        return glm::mix(glm::mix(cell(a.x,a.y),cell(a.x+1,a.y),f.x),glm::mix(cell(a.x,a.y+1),cell(a.x+1,a.y+1),f.x),f.y);
    }
    return (e.features.x&int(MaterialFeature::RainWetness))?e.features.y/65535.f:0.f;
}
inline std::shared_ptr<const ImageRGBA8> rainWetImage(const MaterialExtension& e){
    if(!(e.features.x&int(MaterialFeature::SpatialRainWetness)))return {};
    auto image=std::make_shared<ImageRGBA8>();image->width=image->height=16;image->pixels.resize(256*4);
    for(size_t i=0;i<256;++i){image->pixels[i*4]=rainWetCell(e,i);image->pixels[i*4+3]=255;}return image;
}
}
