#include "renderer/rhi/WaterTransport.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>
#include <iostream>

namespace render {
WaterSlabEstimate traceWaterSlab(float tau,float albedo,float g,float airCosine,uint32_t paths,uint32_t seed) {
    if(!std::isfinite(tau)||tau<0||!std::isfinite(albedo)||albedo<0||albedo>1||!std::isfinite(g)||std::abs(g)>=1||!paths)throw std::invalid_argument("Invalid water slab reference");
    std::mt19937 generator(seed);auto random=[&](){return (double(generator())+.5)/4294967296.;};
    double mu=std::sqrt(1.-(1.-std::clamp(double(airCosine),0.,1.)*std::clamp(double(airCosine),0.,1.))/(1.333*1.333));
    WaterSlabEstimate out;double square=0;
    for(uint32_t path=0;path<paths;++path) {
        glm::dvec3 direction(std::sqrt(1.-mu*mu),0,mu);double depth=0,weight=1,multiple=0;
        for(int scatter=0;scatter<256;++scatter) {
            double next=depth-direction.z*std::log(random());
            if(next<=0){if(scatter>=2)multiple=weight;else if(scatter==1)out.single+=weight;break;}
            if(next>=tau){out.bottom+=weight;break;}
            depth=next;weight*=albedo;if(weight<1e-10)break;
            double u=random(),cosine;
            if(std::abs(g)<.001)cosine=1.-2.*u;
            else {double q=(1.-g*g)/(1.-g+2.*g*u);cosine=(1.+g*g-q*q)/(2.*g);}
            cosine=std::clamp(cosine,-1.,1.);double phi=6.283185307179586*random(),sine=std::sqrt(1.-cosine*cosine);
            glm::dvec3 tangent=glm::normalize(glm::cross(std::abs(direction.z)<.9?glm::dvec3(0,0,1):glm::dvec3(0,1,0),direction));
            direction=glm::normalize(direction*cosine+(tangent*std::cos(phi)+glm::cross(direction,tangent)*std::sin(phi))*sine);
        }
        out.multiple+=multiple;square+=multiple*multiple;
    }
    out.multiple/=paths;out.single/=paths;out.bottom/=paths;
    out.standardError=std::sqrt(std::max(0.,square/paths-out.multiple*out.multiple)/paths);return out;
}
const std::vector<glm::vec4>& waterTransportLut() {
    static const auto data=[] {
#ifndef WATER_TRANSPORT_GENERATE
        const glm::vec4 baked[]={
#include "water-transport-lut.inl"
        };
        static_assert(sizeof(baked)/sizeof(baked[0])==size_t(WaterTauSamples)*WaterAlbedoSamples*WaterGSamples*WaterAngleSamples,"Baked water LUT dimensions");
        return std::vector<glm::vec4>(std::begin(baked),std::end(baked));
#else
        std::vector<glm::vec4> result(size_t(WaterTauSamples)*WaterAlbedoSamples*WaterGSamples*WaterAngleSamples);
        for(uint32_t a=0;a<WaterAngleSamples;++a)for(uint32_t g=0;g<WaterGSamples;++g)for(uint32_t w=0;w<WaterAlbedoSamples;++w)for(uint32_t t=0;t<WaterTauSamples;++t) {
            const float tau=std::expm1(std::log(33.f)*t/(WaterTauSamples-1));
            const float remaining=1.f-float(w)/(WaterAlbedoSamples-1);
            auto v=traceWaterSlab(tau,1.f-remaining*remaining,.85f*g/(WaterGSamples-1),.1f+.3f*a,2048,1337);
            result[((a*WaterGSamples+g)*WaterAlbedoSamples+w)*WaterTauSamples+t]={float(v.multiple),float(v.single),float(v.bottom),float(v.standardError)};
        }
        return result;
#endif
    }();return data;
}
float waterMultipleEstimate(float tau,float albedo,float g,float cosine) {
    const auto& data=waterTransportLut();float t=std::log1p(std::clamp(tau,0.f,32.f))/std::log(33.f)*(WaterTauSamples-1);
    float w=(1.f-std::sqrt(1.f-std::clamp(albedo,0.f,1.f)))*(WaterAlbedoSamples-1),gi=std::clamp(g,0.f,.85f)/.85f*(WaterGSamples-1),a=std::clamp((cosine-.1f)/.9f,0.f,1.f)*(WaterAngleSamples-1);
    float value=0;
    for(uint32_t ai=0;ai<2;++ai)for(uint32_t gg=0;gg<2;++gg)for(uint32_t wi=0;wi<2;++wi)for(uint32_t ti=0;ti<2;++ti) {
        auto index=[](float x,uint32_t offset,uint32_t n){return std::min(uint32_t(x)+offset,n-1);};
        auto weight=[](float x,uint32_t offset){float f=x-std::floor(x);return offset?f:1-f;};
        auto row=(index(a,ai,WaterAngleSamples)*WaterGSamples+index(gi,gg,WaterGSamples))*WaterAlbedoSamples+index(w,wi,WaterAlbedoSamples);
        value+=data[row*WaterTauSamples+index(t,ti,WaterTauSamples)].x*weight(a,ai)*weight(gi,gg)*weight(w,wi)*weight(t,ti);
    }return value;
}
void validateWaterTransport() {
    auto require=[](bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);};
    auto clear=traceWaterSlab(2,0,.65f,.7f,100000,984);
    const double mu=std::sqrt(1.-(1.-.7*.7)/(1.333*1.333));
    require(clear.multiple==0&&clear.single==0&&std::abs(clear.bottom-std::exp(-2/mu))<.003,"Slab pure absorption / Beer failure");
    double maxError=0;
    for(const auto& p:std::vector<glm::vec4>{{.5f,.2f,0,.5f},{2,.6f,.65f,.7f},{8,.95f,.4f,.4f}}) {
        auto reference=traceWaterSlab(p.x,p.y,p.z,p.w,100000,9082);
        const auto estimate=waterMultipleEstimate(p.x,p.y,p.z,p.w);
        maxError=std::max(maxError,std::abs(estimate-reference.multiple));
        require(std::abs(estimate-reference.multiple)<.025,"Water multi-scattering LUT differs from independent MC seed");
    }
    for(const auto& v:waterTransportLut())require(v.x>=0&&v.y>=0&&v.z>=0&&v.x+v.y+v.z<=1.00001f&&std::isfinite(v.w),"Slab energy or variance failure");
    require(waterMultipleEstimate(4,0,.65f,.7f)==0,"LUT added energy to non-scattering water");
    std::cout<<"Water multiple-scattering slab LUT: Beer limit, bounded energy, independent 100000-path MC reference; max absolute flux error "<<maxError<<"\n";
}
}
