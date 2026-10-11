#pragma once
#include "renderer/rhi/GpuOcean.h"
#include <algorithm>
#include <cmath>
namespace render {
namespace oceanSpectrum {
constexpr double pi=3.141592653589793,g=9.81;
inline double omega(double k,double depth){return std::sqrt(g*k*(depth>0?std::tanh(k*depth):1.));}
inline double derivative(double k,double depth){
    const double t=depth>0?std::tanh(k*depth):1.;
    return g*(t+(depth>0?k*depth*(1-t*t):0.))/(2*omega(k,depth));
}
inline double depthFactor(double k,double depth){
    if(depth<=0||k*depth>20)return 1.;
    const double x=k*depth,t=std::tanh(x);
    return t*t/(1+2*x/std::sinh(2*x));
}
inline double shape(glm::dvec2 k,glm::vec2 direction,double period,double gamma,double spread,const OceanSettings& s){
    const double magnitude=glm::length(k),dirLength=glm::length(glm::dvec2(direction));
    if(magnitude<1e-12||dirLength<1e-6)return 0;
    const double w=omega(magnitude,s.seaState.depth),peak=2*pi/period,sigma=w<=peak?.07:.09;
    const double ratio=peak/w,enhancement=std::exp(-std::pow((w-peak)/(sigma*peak),2)/2);
    const double frequency=g*g/std::pow(w,5)*std::exp(-1.25*std::pow(ratio,4))*std::pow(gamma,enhancement);
    // +omega*time in the Fourier phase travels toward -k.
    const double alignment=-(k.x*direction.x+k.y*direction.y)/(magnitude*dirLength);
    const double angular=spread<=0?1.:std::pow(std::max(0.,(1+alignment)/2),spread);
    double band=1;
    if(s.minWavelength>0){
        auto smooth=[](double a,double b,double v){double t=std::clamp((v-a)/(b-a),0.,1.);return t*t*(3-2*t);};
        double low=2*pi/s.maxWavelength,high=2*pi/s.minWavelength;
        band=smooth(low,low*1.25,magnitude)*(1-smooth(high*.8,high,magnitude));
    }
    // Polar frequency density -> Cartesian wave-vector density.
    return frequency*angular*derivative(magnitude,s.seaState.depth)/magnitude*depthFactor(magnitude,s.seaState.depth)*band;
}
inline double normalization(const OceanSettings& s,bool swell=false){
    const auto& a=s.seaState;const double height=swell?a.swellHeight:a.windHeight;
    if(height==0)return 0;
    double sum=0,dk=2*pi/s.length;
    for(uint32_t y=1;y<s.size;++y)for(uint32_t x=1;x<s.size;++x)
        sum+=shape(glm::dvec2(int(x)-int(s.size/2),int(y)-int(s.size/2))*dk,swell?a.swellDirection:s.windDirection,
                   swell?a.swellPeriod:a.windPeriod,swell?1:a.windGamma,swell?a.swellSpread:a.windSpread,s);
    const double variance=2*dk*dk*sum;
    return variance>1e-30?(height*height/16)/variance:0;
}
inline bool same(const OceanSettings& a,const OceanSettings& b){
    const auto& x=a.seaState;const auto& y=b.seaState;
    return a.size==b.size&&a.length==b.length&&a.minWavelength==b.minWavelength&&a.maxWavelength==b.maxWavelength&&a.windDirection==b.windDirection&&
        x.windHeight==y.windHeight&&x.windPeriod==y.windPeriod&&x.windGamma==y.windGamma&&x.windSpread==y.windSpread&&
        x.swellHeight==y.swellHeight&&x.swellPeriod==y.swellPeriod&&x.swellSpread==y.swellSpread&&x.swellDirection==y.swellDirection&&x.depth==y.depth;
}
} // oceanSpectrum
} // render
