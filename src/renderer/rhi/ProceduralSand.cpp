#include "renderer/rhi/ProceduralSand.h"
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <cmath>
namespace render {
namespace {
constexpr float pi=3.14159265358979323846f;
uint32_t hash(uint32_t v){v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;return v^(v>>16);}
float random(uint32_t v){return float(hash(v)>>8)*(1.f/16777216.f);}
int wrap(int v,int n){return (v%n+n)%n;}
float noise(glm::vec2 p,int cells,uint32_t seed){
    p*=float(cells)/ProceduralSand::length;glm::ivec2 cell(glm::floor(p));auto t=glm::fract(p);t=t*t*t*(t*(t*6.f-15.f)+10.f);
    auto at=[&](int x,int y){return random(uint32_t(wrap(x,cells))*65537u+uint32_t(wrap(y,cells))*104729u+seed);};
    return glm::mix(glm::mix(at(cell.x,cell.y),at(cell.x+1,cell.y),t.x),glm::mix(at(cell.x,cell.y+1),at(cell.x+1,cell.y+1),t.x),t.y);
}
}
ProceduralSand::ProceduralSand(uint32_t seed):seed_(seed){
    for(int y=0;y<64;y++)for(int x=0;x<64;x++)for(int j=0;j<2;j++){
        uint32_t s=hash(uint32_t(x)*65537u+uint32_t(y)*104729u+uint32_t(j)*7919u+seed);
        float angle=.22f+(random(s+3)-.5f)*.9f,frequency=1.15f+.75f*random(s+4);
        kernels_[(y*64+x)*2+j]={{2*random(s+1),2*random(s+2)},
            {2*pi*frequency*std::cos(angle),2*pi*frequency*std::sin(angle)},2*pi*random(s+5),.7f+.3f*random(s+6)};
    }
}
SandSample ProceduralSand::sample(glm::vec2 p)const{
    auto cell=glm::ivec2(glm::floor(p*.5f));float value=0;glm::vec2 gradient(0);
    for(int y=cell.y-1;y<=cell.y+1;y++)for(int x=cell.x-1;x<=cell.x+1;x++)for(int j=0;j<2;j++){
        const auto& k=kernels_[(wrap(y,64)*64+wrap(x,64))*2+j];auto d=p-(glm::vec2(x,y)*2.f+k.offset);
        float r2=glm::dot(d,d);if(r2>=4)continue;
        // Compact C1 envelope avoids hard support seams in the normal map.
        float q=1-r2*.25f,e=std::exp(-1.3f*r2),envelope=e*q*q;
        float phase=glm::dot(k.frequency,d)+k.phase,c=std::cos(phase),s=std::sin(phase);
        value+=k.weight*envelope*c;
        gradient+=k.weight*(e*(-2.6f*q*q-q)*d*c-envelope*s*k.frequency);
    }
    float broad=noise(p,8,seed_+10)-.5f,fine=noise(p,256,seed_+20)-.5f;
    // Relief carries the ripple; albedo contrast stays low to avoid zebra bands.
    float brightness=1+.075f*broad+.025f*fine+.018f*value;
    return {.009f*value,.009f*gradient,glm::vec3(.76f,.69f,.54f)*brightness};
}
}
