// Optional independent appearance check linked against unmodified upstream PBRT v3.
#include "materials/hair.h"
#include "../../src/PT/FiberScattering.h"
#include <iostream>
#include <random>
#include <iomanip>

int main() {
    std::mt19937 generator(27);
    std::uniform_real_distribution<float> random(0.f,1.f);
    auto direction=[&](){float x=2*random(generator)-1,phi=6.28318530718f*random(generator),r=std::sqrt(1-x*x);return glm::vec3(x,r*std::cos(phi),r*std::sin(phi));};
    double difference=0,referenceEnergy=0,maxAbsolute=0,maxRelative=0;
    size_t count=0;
    glm::vec3 sigma(.2f,.45f,.8f);
    pbrt::Spectrum referenceSigma;float rgb[3]={sigma.x,sigma.y,sigma.z};referenceSigma=pbrt::Spectrum::FromRGB(rgb);
    for(float bm:{.1f,.3f,.6f,.8f})for(float bn:{.1f,.3f,.6f,.8f})for(float offset:{-.99f,-.5f,0.f,.5f,.99f})for(float tilt:{-.034906585f,0.f,.034906585f}) {
        pbrt::HairBSDF reference(offset,1.55f,referenceSigma,bm,bn,tilt*180.f/3.141592653589793f);
        for(int i=0;i<256;++i){auto v=direction(),l=direction();auto h=pt::fiber::fiberHair(v,glm::vec3(1),{bm,bn,1.55f,tilt},offset,sigma);
            auto ours=pt::fiber::fiberHairKernel(h,l);
            pbrt::Spectrum ref=reference.f({v.x,v.y,v.z},{l.x,l.y,l.z})*std::abs(l.z);float expected[3];ref.ToRGB(expected);
            for(int c=0;c<3;++c){if(!std::isfinite(ours[c])||!std::isfinite(expected[c]))return 2;
                double delta=std::abs(ours[c]-expected[c]);difference+=delta;referenceEnergy+=std::abs(expected[c]);
                maxAbsolute=std::max(maxAbsolute,delta);if(expected[c]>1e-6f)maxRelative=std::max(maxRelative,delta/expected[c]);}
            ++count;
        }
    }
    double relative=difference/referenceEnergy;
    std::cout<<std::setprecision(12)<<"{\"direction_pairs\":"<<count<<",\"relative_rgb_l1\":"<<relative<<",\"maximum_absolute_component\":"<<maxAbsolute<<",\"maximum_relative_component_above_1e_6\":"<<maxRelative<<",\"domain\":\"Hair f times abs(local wi.z), explicit sigma_a, eta 1.55; independent PBRT v3 RGB; geometry/transport excluded\"}\n";
    return relative<.01?0:1;
}
