#include "PT/CpuPathTracer.h"
#include "PT/PhotonMap.h"
#include "PT/ProceduralCapture.h"
#include <stb/stb_image_write.h>
#include <json/json.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <cstring>

namespace pt {
namespace {
constexpr float pi = 3.14159265358979323846f;
float luminance(glm::vec3 c) { return glm::dot(c, glm::vec3(.2126f,.7152f,.0722f)); }
bool finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
glm::vec3 unit(glm::vec3 v, glm::vec3 fallback = {0,1,0}) {
    const float n = glm::dot(v,v); return n > 1e-20f && std::isfinite(n) ? v/std::sqrt(n) : fallback;
}
glm::vec3 local(glm::vec3 v, glm::vec3 n) {
    auto t = unit(glm::cross(std::abs(n.y)<.99f ? glm::vec3(0,1,0) : glm::vec3(1,0,0),n));
    return t*v.x+glm::cross(n,t)*v.y+n*v.z;
}
float power(float a, float b) { if (a == 0) return 0; const float r=b/a; return 1/(1+r*r); }
float specularChance(const Surface &s) {
    const float spec = luminance(glm::mix(glm::vec3(.04f),s.albedo,s.metallic));
    const float diffuse = luminance(s.albedo)*(1-s.metallic);
    return glm::clamp(spec/(spec+diffuse+1e-20f),.1f,.9f);
}
glm::vec3 diffuseNormal(const Surface &s){return glm::dot(s.diffuseNormal,s.diffuseNormal)>0?s.diffuseNormal:s.normal;}
float bumpShadowing(const Surface &s,glm::vec3 n,glm::vec3 l){
    if(glm::dot(s.smoothNormal,s.smoothNormal)==0)return 1;
    const float i=glm::dot(s.smoothNormal,l),d=glm::dot(s.smoothNormal,n),c=glm::dot(n,l);
    if(i*d*c<0)return 0;
    const float ci=std::abs(i),cd=std::abs(d);if(cd>=1||ci>=1)return 1;if(ci<1e-6f)return 0;
    const float a2=glm::clamp(.125f*(1/std::max(cd*cd,1e-20f)-1),0.f,1.f);
    return 2*ci/(ci+std::sqrt(a2+(1-a2)*ci*ci));
}
float distribution(float nh, float roughness) {
    const float a=roughness*roughness, a2=a*a, q=nh*nh*(a2-1)+1;
    return a2/(pi*q*q);
}
float smith(float cosine, float roughness) {
    if(cosine <= 0) return 0;
    const float a=roughness*roughness;
    return 2*cosine/(cosine+std::sqrt(a*a+(1-a*a)*cosine*cosine));
}
glm::vec3 visibleNormal(glm::vec3 v,glm::vec3 n,float roughness,std::array<float,2> uv){
    const float a=roughness*roughness;
    auto t=unit(glm::cross(std::abs(n.y)<.99f?glm::vec3(0,1,0):glm::vec3(1,0,0),n));auto b=glm::cross(n,t);
    auto vh=unit(glm::vec3(a*glm::dot(v,t),a*glm::dot(v,b),glm::dot(v,n)),{0,0,1});
    float length=vh.x*vh.x+vh.y*vh.y;auto t1=length>0?glm::vec3(-vh.y,vh.x,0)/std::sqrt(length):glm::vec3(1,0,0),t2=glm::cross(vh,t1);
    float radius=std::sqrt(uv[0]),phi=2*pi*uv[1],x=radius*std::cos(phi),blend=.5f*(1+vh.z);
    float y=glm::mix(std::sqrt(std::max(0.f,1-x*x)),radius*std::sin(phi),blend);
    auto nh=t1*x+t2*y+vh*std::sqrt(std::max(0.f,1-x*x-y*y));auto ne=unit(glm::vec3(a*nh.x,a*nh.y,std::max(0.f,nh.z)),{0,0,1});
    return t*ne.x+b*ne.y+n*ne.z;
}
struct RoughDielectric {float value=0,pdf=0;bool transmission=false;};
RoughDielectric roughDielectric(const Surface &s,glm::vec3 v,glm::vec3 l,TransportMode mode){
    const float nv=glm::dot(s.normal,v),nl=glm::dot(s.normal,l);if(nv<=0||nl==0||glm::dot(v,s.geometricNormal)<=0)return {};
    const bool reflection=nl>0;if((glm::dot(l,s.geometricNormal)>0)!=reflection)return {};
    const float etaI=s.frontFace?s.exteriorIor:s.ior,etaT=s.frontFace?s.ior:s.exteriorIor,etap=etaT/etaI;
    auto sum=v+l*(reflection?1.f:etap);if(glm::dot(sum,sum)<1e-20f)return {};auto h=unit(sum);if(glm::dot(h,s.normal)<0)h=-h;
    const float vh=glm::dot(v,h),lh=glm::dot(l,h);if(vh<=0||lh*nl<=0)return {};
    float nh=glm::clamp(glm::dot(h,s.normal),0.f,1.f),a=s.transmissionRoughness*s.transmissionRoughness,a2=a*a;
    const float q=std::max(0.f,1-nh*nh)+a2*nh*nh,D=a2/(pi*q*q),Gv=smith(nv,s.transmissionRoughness),Gl=smith(std::abs(nl),s.transmissionRoughness),G=s.bsdfModel==2?Gv*Gl/(Gv+Gl-Gv*Gl):Gv*Gl;
    const float F=dielectricFresnel(reflection?.5f*(vh+lh):vh,etaI,etaT),normalPdf=D*Gv*vh/nv;
    if(reflection)return {F*D*G/(4*nv*nl),F*normalPdf/(4*vh),false};
    const float denominator=lh+vh/etap;if(std::abs(denominator)<1e-10f)return {};
    float value=(1-F)*D*G*std::abs(vh*lh)/(nv*std::abs(nl)*denominator*denominator);
    if(mode==TransportMode::Radiance)value/=etap*etap;
    return {value,(1-F)*normalPdf*std::abs(lh)/(denominator*denominator),true};
}
glm::vec4 texel(const std::shared_ptr<const render::ImageRGBA8> &image, glm::vec2 uv,
                 glm::vec4 fallback = glm::vec4(1),bool repeat=true) {
    if(!image || image->pixels.empty()) return fallback;
    const int w=int(image->width), h=int(image->height);
    uv=repeat?glm::fract(uv):glm::clamp(uv,0.f,1.f);
    const float x=uv.x*w-.5f,y=uv.y*h-.5f; const int ix=int(std::floor(x)),iy=int(std::floor(y));
    auto pixel=[&](int px,int py) { px=repeat?(px%w+w)%w:std::clamp(px,0,w-1);py=repeat?(py%h+h)%h:std::clamp(py,0,h-1);
        const auto *p=image->pixels.data()+(size_t(py)*w+px)*4;
        return glm::vec4(p[0],p[1],p[2],p[3])/255.f;
    };
    return glm::mix(glm::mix(pixel(ix,iy),pixel(ix+1,iy),x-ix),
                    glm::mix(pixel(ix,iy+1),pixel(ix+1,iy+1),x-ix),y-iy);
}
struct Bounds {
    glm::vec3 low{std::numeric_limits<float>::infinity()}, high{-std::numeric_limits<float>::infinity()};
    void add(glm::vec3 p) {low=glm::min(low,p);high=glm::max(high,p);}
    void add(const Bounds &b) {low=glm::min(low,b.low);high=glm::max(high,b.high);}
    float area() const {auto d=glm::max(high-low,glm::vec3(0));return 2*(d.x*d.y+d.x*d.z+d.y*d.z);}
};
bool boxHit(glm::vec3 low,glm::vec3 high,glm::vec3 origin,glm::vec3 direction,float minimum,
            float maximum,float &near) {
    for(int i=0;i<3;++i) {
        if(std::abs(direction[i])<1e-30f) {if(origin[i]<low[i] || origin[i]>high[i])return false;continue;}
        float a=(low[i]-origin[i])/direction[i],b=(high[i]-origin[i])/direction[i];
        if(a>b)std::swap(a,b);minimum=std::max(minimum,a);maximum=std::min(maximum,b);
        if(maximum<minimum)return false;
    }
    near=minimum;return true;
}
glm::vec2 directionUV(glm::vec3 d) {
    d=unit(d);float u=std::atan2(d.x,-d.z)/(2*pi);u-=std::floor(u);
    return {u,std::acos(glm::clamp(d.y,-1.f,1.f))/pi};
}
double solidAngle(uint32_t row,uint32_t w,uint32_t h) {
    constexpr double dpi=3.14159265358979323846;
    return 2*dpi/w*(std::cos(dpi*row/h)-std::cos(dpi*(row+1)/h));
}
}
Environment::Environment(uint32_t w,uint32_t h,std::vector<glm::vec3> pixels)
    :width_(w),height_(h),pixels_(std::move(pixels)) {
    if(!w || !h || pixels_.size()!=size_t(w)*h)throw std::invalid_argument("PT: invalid environment extent");
    double mean=0;
    for(auto &p:pixels_) {
        if(!finite(p) || glm::any(glm::lessThan(p,glm::vec3(0))))throw std::invalid_argument("PT: invalid HDR environment");
        mean+=luminance(p);
    }
    mean/=pixels_.size();cdf_.reserve(pixels_.size());
    // Full support also covers bilinear radiance spilling into black texels.
    const double floor=std::max(mean*.001,1e-12);
    for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x) {
        total_+=(luminance(pixels_[size_t(y)*w+x])+floor)*solidAngle(y,w,h);cdf_.push_back(total_);
    }
}
glm::vec3 Environment::evaluate(glm::vec3 d) const {
    const auto uv=directionUV(d);float x=uv.x*width_-.5f,y=uv.y*height_-.5f;
    int ix=int(std::floor(x)),iy=int(std::floor(y));
    auto p=[&](int px,int py){px=(px%int(width_)+width_)%width_;py=std::clamp(py,0,int(height_)-1);return pixels_[size_t(py)*width_+px];};
    return glm::mix(glm::mix(p(ix,iy),p(ix+1,iy),x-ix),glm::mix(p(ix,iy+1),p(ix+1,iy+1),x-ix),y-iy);
}
float Environment::pdf(glm::vec3 d) const {
    auto uv=directionUV(d);uint32_t x=std::min(uint32_t(uv.x*width_),width_-1),y=std::min(uint32_t(uv.y*height_),height_-1);
    const size_t i=size_t(y)*width_+x;return float((cdf_[i]-(i?cdf_[i-1]:0))/(total_*solidAngle(y,width_,height_)));
}
EnvironmentSample Environment::sample(Random &r) const {
    const auto it=std::upper_bound(cdf_.begin(),cdf_.end(),double(r.uniform())*total_);
    const size_t i=std::min(size_t(it-cdf_.begin()),cdf_.size()-1);const uint32_t x=uint32_t(i%width_),y=uint32_t(i/width_);
    const auto uv=r.uniform2();
    const float phi=(x+uv[0])/width_*2*pi;
    const float cosine=glm::mix(std::cos(pi*y/height_),std::cos(pi*(y+1)/height_),uv[1]);
    const float sine=std::sqrt(std::max(0.f,1-cosine*cosine));glm::vec3 d{sine*std::sin(phi),cosine,-sine*std::cos(phi)};
    return {d,evaluate(d),float((cdf_[i]-(i?cdf_[i-1]:0))/(total_*solidAngle(y,width_,height_)))};
}
glm::vec3 correctedReflectionNormal(glm::vec3 geometric,glm::vec3 view,glm::vec3 shading){
    const float z=glm::dot(view,geometric),threshold=std::min(.9f*z,.01f);
    if(z<=0||glm::dot(geometric,glm::reflect(-view,shading))>=threshold)return shading;
    const auto side=shading-geometric*glm::dot(shading,geometric);
    if(glm::dot(side,side)<1e-20f)return geometric;
    const auto x=unit(side);const float vx=glm::dot(view,x),radius=std::sqrt(vx*vx+z*z);
    // R.z = z*cos(2*theta) + vx*sin(2*theta). Choose the first positive
    // boundary angle while rotating the shading normal towards the geometry.
    const float angle=.5f*(std::atan2(vx,z)+std::acos(glm::clamp(threshold/radius,-1.f,1.f)));
    return unit(x*std::sin(angle)+geometric*std::cos(angle),geometric);
}
float surfaceCosine(const Surface &s,glm::vec3 l) {
    if(s.ior>0)return std::abs(glm::dot(s.normal,l));
    const float cosine=glm::dot(s.bsdfModel>0?s.geometricNormal:s.normal,l);
    return s.bsdfModel==3?std::abs(cosine):std::max(0.f,cosine);
}
glm::vec3 evaluateBsdf(const Surface &s,glm::vec3 v,glm::vec3 l,TransportMode mode) {
    if(s.thinDielectric)return glm::vec3(0); // Two discrete delta directions.
    if(s.ior>0){const float foam=s.water?s.foam:0;auto f=s.transmissionRoughness>=.02f?roughDielectric(s,v,l,mode):RoughDielectric{};
        return (1-foam)*f.value*(f.transmission?s.albedo:glm::vec3(1))+(foam>0&&glm::dot(s.normal,v)>0&&glm::dot(s.normal,l)>0&&glm::dot(s.geometricNormal,l)>0?foam*s.albedo/pi:glm::vec3(0));}
    const float nv=glm::dot(s.normal,v),nl=glm::dot(s.normal,l),ngl=glm::dot(s.geometricNormal,l);
    const auto dn=diffuseNormal(s);const float dl=std::max(0.f,glm::dot(dn,l));
    if(glm::dot(s.geometricNormal,v)<=0)return glm::vec3(0);
    if(s.bsdfModel==3){
        if(ngl>0)return s.albedo*(dl/std::max(ngl,1e-20f)*bumpShadowing(s,dn,l)/pi);
        if(ngl<0&&nl<0)return s.diffuseTransmission*(nl/ngl*bumpShadowing(s,s.normal,l)/pi);
        return glm::vec3(0);
    }
    if(s.bsdfModel==1)return ngl>0?s.albedo*(dl*bumpShadowing(s,dn,l)/std::max(ngl,1e-20f)/pi):glm::vec3(0);
    if(s.bsdfModel==2){
        if(ngl<=0)return glm::vec3(0);
        auto result=.96f*s.albedo*(1-s.metallic)*(dl/std::max(ngl,1e-20f)*bumpShadowing(s,dn,l)/pi);
        if(nv>0&&nl>0){auto h=unit(v+l,s.normal);const float gv=smith(nv,s.roughness),gl=smith(nl,s.roughness);auto f0=glm::mix(glm::vec3(.04f),s.albedo,s.metallic);result+=f0*(distribution(std::max(0.f,glm::dot(s.normal,h)),s.roughness)*gv*gl/((gv+gl-gv*gl)*4*nv*ngl));}
        return result;
    }
    if(nv<=0||nl<=0||ngl<=0)return glm::vec3(0);
    auto h=unit(v+l,s.normal);float nh=std::max(0.f,glm::dot(s.normal,h)),vh=std::max(0.f,glm::dot(v,h));
    auto f0=glm::mix(glm::vec3(.04f),s.albedo,s.metallic);
    auto f=f0+(1.f-f0)*std::pow(1-vh,5.f);
    return (1.f-f)*s.albedo*(1-s.metallic)/pi+f*(distribution(nh,s.roughness)*smith(nv,s.roughness)*smith(nl,s.roughness)/(4*nv*nl));
}
float bsdfPdf(const Surface &s,glm::vec3 v,glm::vec3 l) {
    if(s.thinDielectric)return 0;
    if(s.ior>0){const float foam=s.water?s.foam:0;return (1-foam)*(s.transmissionRoughness>=.02f?roughDielectric(s,v,l,TransportMode::Radiance).pdf:0)+(glm::dot(s.normal,v)>0?foam*std::max(0.f,glm::dot(s.normal,l))/pi:0);}
    const float nl=glm::dot(s.normal,l),dl=std::max(0.f,glm::dot(diffuseNormal(s),l));
    if(s.bsdfModel==3){const float r=luminance(s.albedo),t=luminance(s.diffuseTransmission),sum=r+t;if(sum<=0)return 0;return glm::dot(s.geometricNormal,l)>0?r/sum*dl/pi:glm::dot(s.geometricNormal,l)<0?t/sum*std::max(0.f,-nl)/pi:0;}
    if(s.bsdfModel==1)return dl/pi;
    if(s.bsdfModel==2){const float p=specularChance(s),nv=glm::dot(s.normal,v);float pdf=(1-p)*dl/pi;
        if(nl>0&&nv>0){auto h=unit(v+l,s.normal);if(glm::dot(v,h)>0)pdf+=p*distribution(std::max(0.f,glm::dot(s.normal,h)),s.roughness)*smith(nv,s.roughness)/(4*nv);}
        return pdf;
    }
    if(nl<=0 || glm::dot(s.normal,v)<=0)return 0;
    auto h=unit(v+l,s.normal);const float vh=glm::dot(v,h);if(vh<=0)return 0;
    const float p=specularChance(s);
    return (1-p)*nl/pi+p*distribution(std::max(0.f,glm::dot(s.normal,h)),s.roughness)*smith(glm::dot(s.normal,v),s.roughness)/(4*glm::dot(s.normal,v));
}
float dielectricFresnel(float cosine,float etaI,float etaT) {
    cosine=glm::clamp(std::abs(cosine),0.f,1.f);const float sineT=etaI/etaT*std::sqrt(std::max(0.f,1-cosine*cosine));
    if(sineT>=1)return 1;const float cosineT=std::sqrt(std::max(0.f,1-sineT*sineT));
    const float parallel=(etaT*cosine-etaI*cosineT)/(etaT*cosine+etaI*cosineT),perpendicular=(etaI*cosine-etaT*cosineT)/(etaI*cosine+etaT*cosineT);
    return (parallel*parallel+perpendicular*perpendicular)*.5f;
}
float thinDielectricReflectance(float cosine,float exteriorIor,float sheetIor) {
    const float f=dielectricFresnel(cosine,exteriorIor,sheetIor);
    // Sum all incoherent reflections between reciprocal parallel interfaces.
    return 2*f/(1+f);
}
BsdfSample sampleBsdf(const Surface &s,glm::vec3 v,Random &r,TransportMode mode) {
    if(s.thinDielectric){
        const float f=thinDielectricReflectance(glm::dot(s.geometricNormal,v),s.exteriorIor,s.ior);
        const bool transmission=r.uniform()>=f;const auto l=transmission?-v:glm::reflect(-v,s.geometricNormal);
        const float probability=transmission?1-f:f;
        return {l,(transmission?s.albedo:glm::vec3(1))*(probability/std::max(1e-8f,std::abs(glm::dot(s.geometricNormal,l)))),probability,true,transmission};
    }
    if(s.ior>0&&s.transmissionRoughness>=.02f&&s.ior!=s.exteriorIor){
        const float choice=r.uniform();auto uv=r.uniform2();glm::vec3 l;bool transmission=false;
        if(s.water&&choice<s.foam){float radius=std::sqrt(uv[0]),phi=2*pi*uv[1];l=local({radius*std::cos(phi),radius*std::sin(phi),std::sqrt(1-uv[0])},s.normal);}
        else {auto h=visibleNormal(v,s.normal,s.transmissionRoughness,uv);const float etaI=s.frontFace?s.exteriorIor:s.ior,etaT=s.frontFace?s.ior:s.exteriorIor;
            const float F=dielectricFresnel(glm::dot(v,h),etaI,etaT);if(r.uniform()<F)l=glm::reflect(-v,h);else{l=glm::refract(-v,h,etaI/etaT);transmission=true;}}
        if(glm::dot(l,l)<1e-12f||(glm::dot(l,s.normal)>0)==transmission||(glm::dot(l,s.geometricNormal)>0)==transmission)return {};
        return {l,evaluateBsdf(s,v,l,mode),bsdfPdf(s,v,l),false,transmission};
    }
    if(s.ior>0){const float etaI=s.frontFace?s.exteriorIor:s.ior,etaT=s.frontFace?s.ior:s.exteriorIor,eta=etaI/etaT;const float f=dielectricFresnel(glm::dot(s.normal,v),etaI,etaT);
        float weight=1;if(s.water&&s.foam>0){if(r.uniform()<s.foam){auto uv=r.uniform2();float radius=std::sqrt(uv[0]),phi=2*pi*uv[1];auto l=local({radius*std::cos(phi),radius*std::sin(phi),std::sqrt(1-uv[0])},s.normal);if(glm::dot(s.geometricNormal,l)<=0)return {};return {l,s.foam*s.albedo/pi,s.foam*std::max(0.f,glm::dot(l,s.normal))/pi,false,false};}weight=1-s.foam;}
        if(r.uniform()<f){auto l=glm::reflect(-v,s.normal);return {l,glm::vec3(weight*f/std::max(1e-8f,std::abs(glm::dot(s.normal,l)))),weight*f,true};}
        auto l=glm::refract(-v,s.normal,eta);if(glm::dot(l,l)<1e-12f)return {};
        const float scale=mode==TransportMode::Radiance?eta*eta:1;return {l,s.albedo*(weight*(1-f)*scale/std::max(1e-8f,std::abs(glm::dot(s.normal,l)))),weight*(1-f),true,true};
    }
    glm::vec3 l;
    const float selection=r.uniform();const auto uv=r.uniform2();
    if(s.bsdfModel==3){float total=luminance(s.albedo+s.diffuseTransmission),chance=total>0?luminance(s.diffuseTransmission)/total:0;const bool transmit=selection<chance;float q=uv[0],phi=2*pi*uv[1];l=local({std::sqrt(q)*std::cos(phi),std::sqrt(q)*std::sin(phi),(transmit?-1.f:1.f)*std::sqrt(1-q)},transmit?s.normal:diffuseNormal(s));if((glm::dot(s.geometricNormal,l)<0)!=transmit)return {};return {l,evaluateBsdf(s,v,l),bsdfPdf(s,v,l),false,transmit};}
    if(s.bsdfModel!=1&&selection<specularChance(s)) {
        const auto half=visibleNormal(v,s.normal,s.roughness,uv);l=glm::reflect(-v,half);
        if(glm::dot(v,half)<=0 || glm::dot(l,s.normal)<=0)return {};
    }else {
        const float q=uv[0],phi=2*pi*uv[1];l=local({std::sqrt(q)*std::cos(phi),std::sqrt(q)*std::sin(phi),std::sqrt(1-q)},s.bsdfModel==1||s.bsdfModel==2?diffuseNormal(s):s.normal);
    }
    return {l,evaluateBsdf(s,v,l),bsdfPdf(s,v,l)};
}
struct CpuScene::State {
    bool shadowOpaque=true;
    struct Mesh {std::array<std::shared_ptr<const render::ImageRGBA8>,4> shoreline;uint32_t geometry=0,primitiveBase=0;glm::mat4 model{1},inverse{1};glm::mat3 normal{1};bool mirrored=false,shadowOpaque=true;std::shared_ptr<const render::MaterialPayload> material;render::MaterialParameters parameters;render::MaterialExtension extension;float ior=0,normalScale=1,transmissionRoughness=0;glm::vec3 absorption{0},scattering{0};float g=0;uint32_t kind=0,bsdfModel=0;bool roughnessTexture=false;glm::vec3 diffuseTransmission{0};std::shared_ptr<const render::ImageRGBA8> transmissionTexture;};
    struct Primitive {uint32_t mesh,offset;};
    struct Node {glm::vec3 low;uint32_t first;glm::vec3 high;uint32_t count;};
    struct AreaLight {uint32_t primitive;float area;double cumulative;glm::vec3 normal;};
    struct Tree {
    std::vector<uint32_t> order;std::vector<Node> nodes;
    uint32_t build(uint32_t node,uint32_t start,uint32_t end,std::vector<Bounds> &bounds,
                   std::vector<glm::vec3> &centers,uint32_t depth) {
        Bounds total,centroid;for(uint32_t i=start;i<end;++i){total.add(bounds[order[i]]);centroid.add(centers[order[i]]);}
        nodes[node]={total.low,start,total.high,end-start};
        if(end-start<=8 || depth>=60)return node;
        constexpr uint32_t bins=12;float best=std::numeric_limits<float>::infinity();int axis=-1,split=-1;
        for(int a=0;a<3;++a) {
            const float extent=centroid.high[a]-centroid.low[a];if(extent<1e-8f)continue;
            std::array<Bounds,bins> b;std::array<uint32_t,bins> count{};
            for(uint32_t i=start;i<end;++i){const uint32_t id=order[i],slot=std::min(uint32_t((centers[id][a]-centroid.low[a])/extent*bins),bins-1);b[slot].add(bounds[id]);++count[slot];}
            std::array<float,bins> right{};Bounds tail;uint32_t n=0;
            for(int j=bins-1;j>=0;--j){if(count[j])tail.add(b[j]);n+=count[j];right[j]=n?tail.area()*n:0;}
            Bounds head;n=0;
            for(uint32_t j=0;j<bins-1;++j){if(count[j])head.add(b[j]);n+=count[j];const float cost=(n?head.area()*n:0)+right[j+1];if(cost<best){best=cost;axis=a;split=int(j);}}
        }
        uint32_t mid=start;
        if(axis>=0) {
            const float extent=centroid.high[axis]-centroid.low[axis];
            auto partition=std::partition(order.begin()+start,order.begin()+end,[&](uint32_t id){return std::min(uint32_t((centers[id][axis]-centroid.low[axis])/extent*bins),bins-1)<=uint32_t(split);});mid=uint32_t(partition-order.begin());
        }
        if(mid==start || mid==end){axis=0;auto e=centroid.high-centroid.low;if(e.y>e.x)axis=1;if(e.z>e[axis])axis=2;mid=start+(end-start)/2;std::nth_element(order.begin()+start,order.begin()+mid,order.begin()+end,[&](auto a,auto b){return centers[a][axis]<centers[b][axis];});}
        const uint32_t child=uint32_t(nodes.size());nodes.resize(nodes.size()+2);nodes[node].first=child;nodes[node].count=0;
        build(child,start,mid,bounds,centers,depth+1);build(child+1,mid,end,bounds,centers,depth+1);return node;
    }
    void construct(std::vector<Bounds> &bounds) {
        order.resize(bounds.size());std::iota(order.begin(),order.end(),0u);if(bounds.empty())return;
        std::vector<glm::vec3> centers;centers.reserve(bounds.size());for(const auto &b:bounds)centers.push_back((b.low+b.high)*.5f);
        nodes.reserve(bounds.size()/2+2);nodes.resize(1);build(0,0,uint32_t(bounds.size()),bounds,centers,0);
    }
    };
    struct Geometry {std::shared_ptr<const render::MeshPayload> source;std::vector<uint32_t> offsets;Tree blas;Bounds bounds;};
    std::vector<Mesh> meshes;std::unordered_map<uint32_t,size_t> kindCounts;size_t scatteringMeshes=0,dielectricMeshes=0,reflectionInterfaces=0;std::vector<Geometry> geometries;Tree tlas;uint32_t primitiveCount=0;
    std::vector<glm::vec3> thinSolarNormals;
    std::vector<AreaLight> emitters;std::unordered_map<uint32_t,uint32_t> emitterIndex;double emitterWeight=0;
    AccelerationStats statistics;
    std::vector<render::LightData> lights;glm::mat4 inverseProjection;glm::vec3 camera;
    bool inverseSquare=true;glm::vec3 cameraForward{0,0,-1};std::array<uint32_t,8> cameraMedia{},cameraWinding{};float filmArea=1,time=0;glm::mat4 projection{1};
    Primitive primitive(uint32_t id) const {
        auto it=std::upper_bound(meshes.begin(),meshes.end(),id,[](uint32_t value,const Mesh &m){return value<m.primitiveBase;});
        const uint32_t instance=uint32_t(it-meshes.begin()-1);const auto &m=meshes[instance];return {instance,id-m.primitiveBase};
    }
    std::array<render::MeshVertex,3> localVertices(uint32_t instance,uint32_t triangle) const {
        const auto &m=meshes[instance];const auto &g=geometries[m.geometry];const auto &source=*g.source;const uint32_t offset=g.offsets[triangle];
        std::array<render::MeshVertex,3> v={source.vertices[source.indices[offset]],source.vertices[source.indices[offset+1]],source.vertices[source.indices[offset+2]]};
        if(m.mirrored)std::swap(v[1],v[2]);return v;
    }
    std::array<render::MeshVertex,3> vertices(uint32_t id) const {
        auto p=primitive(id);auto v=localVertices(p.mesh,p.offset);const auto &m=meshes[p.mesh];
        for(auto &a:v){a.position=glm::vec3(m.model*glm::vec4(a.position,1));a.normal=m.normal*a.normal;if(geometries[m.geometry].source->pathTracingTangents.empty())a.normal=unit(a.normal);}return v;
    }
    bool triangle(uint32_t id,glm::vec3 o,glm::vec3 d,float minimum,float maximum,float &distance,glm::vec2 &bary,bool localSpace=false,uint32_t instance=UINT32_MAX) const {
        const auto ref=instance==UINT32_MAX?primitive(id):Primitive{instance,id-meshes[instance].primitiveBase};auto v=localSpace?localVertices(ref.mesh,ref.offset):vertices(id);const auto e1=v[1].position-v[0].position,e2=v[2].position-v[0].position;
        auto p=glm::cross(d,e2);const double determinant=glm::dot(e1,p);
        if(std::abs(determinant)<=1e-10*std::sqrt(double(glm::dot(e1,e1))*glm::dot(e2,e2)*glm::dot(d,d)))return false;
        auto t=o-v[0].position;double u=glm::dot(t,p)/determinant;if(u<0 || u>1)return false;
        auto q=glm::cross(t,e1);double w=glm::dot(d,q)/determinant;if(w<0 || u+w>1)return false;
        const double dist=glm::dot(e2,q)/determinant;if(dist<minimum || dist>maximum)return false;
        const auto &m=meshes[ref.mesh];
        if(m.parameters.factors.w>0) {
            auto uv=v[0].uv*float(1-u-w)+v[1].uv*float(u)+v[2].uv*float(w);
            if(texel(m.material?m.material->images[0]:nullptr,uv).a*m.parameters.albedoAlpha.w<m.parameters.factors.w)return false;
        }
        distance=float(dist);bary={float(u),float(w)};return true;
    }
    void surface(uint32_t id,glm::vec2 bary,float t,glm::vec3 o,glm::vec3 d,Surface &out) const {
        auto v=vertices(id);const auto ref=primitive(id);const auto &m=meshes[ref.mesh];auto image=[&](size_t i){return m.material?m.material->images[i]:std::shared_ptr<const render::ImageRGBA8>{};};
        out=Surface{};out.bsdfModel=m.bsdfModel;out.thinDielectric=m.kind==5;out.mediumId=m.ior>0&&!out.thinDielectric?ref.mesh+1:0;out.ior=m.ior;out.transmissionRoughness=m.transmissionRoughness;out.water=m.kind==3;out.absorption=m.absorption;out.twoSided=m.extension.settings.w>0;out.distance=t;out.primitive=id;out.position=o+d*t;
        out.uv=v[0].uv*(1-bary.x-bary.y)+v[1].uv*bary.x+v[2].uv*bary.y;
        const auto e1=v[1].position-v[0].position,e2=v[2].position-v[0].position;
        auto n=unit(v[0].normal*(1-bary.x-bary.y)+v[1].normal*bary.x+v[2].normal*bary.y);
        auto gn=unit(glm::cross(e1,e2),n);if(glm::dot(gn,n)<0){if(m.kind==4||m.kind==3)n=-n;else gn=-gn;}
        out.frontFace=glm::dot(gn,d)<0;if(!out.frontFace){gn=-gn;n=-n;}
        out.geometricNormal=gn;out.normal=n;
        if(image(1) && m.parameters.emissiveNormal.w>0) {
            if(out.water){auto map=glm::vec3(texel(image(1),out.uv*m.normalScale))*2.f-1.f;auto mapped=unit(glm::vec3(map.x,map.z,-map.y));if(!out.frontFace)mapped=-mapped;if(glm::dot(mapped,gn)>.05f)out.normal=mapped;}
            else {
            const auto &g=geometries[m.geometry];const auto &source=*g.source;bool explicitFrame=false;
            if(!source.pathTracingTangents.empty()){
                const uint32_t at=g.offsets[ref.offset];glm::vec4 frame(0);glm::vec3 localNormal(0);
                const float weights[]={1-bary.x-bary.y,bary.x,bary.y};
                for(int i=0;i<3;++i){const int slot=m.mirrored&&i>0?3-i:i;const auto index=source.indices[at+slot];frame+=source.pathTracingTangents[index]*weights[i];localNormal+=source.vertices[index].normal*weights[i];}
                const auto tangent=glm::vec3(frame);
                if(glm::dot(tangent,tangent)>1e-12f&&std::abs(frame.w)>.5f){
                    // Cycles composes the unnormalized corner frame in object space,
                    // then transforms the complete mapped normal with inverse transpose.
                    const auto bitangent=glm::cross(localNormal,tangent)*frame.w;
                    auto map=glm::vec3(texel(image(1),out.uv))*2.f-1.f;const float strength=m.parameters.emissiveNormal.w;
                    map.x*=strength;map.y*=strength;map.z=glm::mix(1.f,map.z,glm::clamp(strength,0.f,1.f));
                    auto mapped=unit(m.normal*(tangent*map.x+bitangent*map.y+localNormal*map.z),n);
                    if(glm::dot(m.normal*localNormal,out.frontFace?n:-n)<0)mapped=-mapped;
                    if(!out.frontFace)mapped=-mapped;
                    if(glm::dot(mapped,gn)>.05f)out.normal=mapped;explicitFrame=true;
                }
            }
            auto a=v[1].uv-v[0].uv,b=v[2].uv-v[0].uv;const float determinant=a.x*b.y-a.y*b.x;
            if(!explicitFrame&&std::abs(determinant)>1e-10f) {
                auto tangent=(e1*b.y-e2*a.y)/determinant,bitangent=(-e1*b.x+e2*a.x)/determinant;
                tangent=unit(tangent-n*glm::dot(n,tangent));bitangent=unit(bitangent-n*glm::dot(n,bitangent));
                auto map=glm::vec3(texel(image(1),out.uv))*2.f-1.f;map.x*=m.parameters.emissiveNormal.w;map.y*=m.parameters.emissiveNormal.w;if(!out.frontFace){map.x=-map.x;map.y=-map.y;}
                const auto mapped=unit(tangent*map.x+bitangent*map.y+n*map.z,n);
                if(glm::dot(mapped,gn)>.05f)out.normal=mapped;
            }
            }
        }
        // Legacy PBR / dielectric closures require a shading normal towards the view.
        if(!(out.ior==0&&out.bsdfModel>0)&&glm::dot(out.normal,-d)<=0)out.normal=glm::dot(n,-d)>0?n:gn;
        if(out.thinDielectric)out.normal=gn;
        const auto base=texel(image(0),out.uv)*m.parameters.albedoAlpha;
        out.albedo=glm::pow(glm::max(glm::vec3(base),glm::vec3(0)),glm::vec3(2.2f));
        if(m.bsdfModel==3)out.diffuseTransmission=m.diffuseTransmission*glm::pow(glm::max(glm::vec3(texel(m.transmissionTexture,out.uv)),glm::vec3(0)),glm::vec3(2.2f));
        out.foam=out.water&&image(2)?glm::clamp(texel(image(2),out.uv*m.normalScale).r,0.f,1.f):0;
        out.metallic=glm::clamp(texel(image(2),out.uv).b*m.parameters.factors.x,0.f,1.f);
        out.roughness=glm::clamp(texel(image(3),out.uv).g*m.parameters.factors.y,.045f,1.f);
        if(m.shoreline[0]){
            const auto H=m.extension.shoreHeight,S=m.extension.shoreSurface;
            const float altitude=out.position.y-H.x;
            const float weight=texel(m.shoreline[3],out.uv,glm::vec4(1),false).r*(1-glm::smoothstep(H.y*.55f,H.y,altitude))*glm::smoothstep(S.y,S.z,std::abs(n.y))*glm::smoothstep(-4.f,-.5f,altitude);
            const float wet=1-glm::smoothstep(-H.z,H.w,altitude);const glm::vec2 beachUV=glm::vec2(out.position.x,-out.position.z)/S.x;
            const auto sand=texel(m.shoreline[0],beachUV),orm=texel(m.shoreline[2],beachUV);
            out.albedo=glm::mix(out.albedo,glm::pow(glm::vec3(sand),glm::vec3(2.2f))*glm::mix(1.f,.45f,wet),weight);
            out.roughness=glm::clamp(glm::mix(out.roughness,glm::mix(orm.g,std::max(.28f,orm.g*.5f),wet),weight),.045f,1.f);
            out.metallic=glm::mix(out.metallic,0.f,weight);
            auto tangent=glm::vec3(1,0,0)-n*n.x;
            if(glm::dot(tangent,tangent)>1e-8f){
                tangent=unit(tangent);auto map=unit(glm::vec3(texel(m.shoreline[1],beachUV))*2.f-1.f);map.x*=S.w;map.y*=S.w;if(!out.frontFace){map.x=-map.x;map.y=-map.y;}
                auto mapped=unit(glm::mix(out.normal,unit(tangent*map.x+glm::cross(n,tangent)*map.y+n*map.z),weight));
                if(glm::dot(mapped,gn)>.05f&&glm::dot(mapped,-d)>0)out.normal=mapped;
            }
        }
        if(out.ior==0&&out.bsdfModel>0){out.diffuseNormal=out.normal;out.smoothNormal=n;
            if(out.bsdfModel==2||out.bsdfModel==3)out.normal=correctedReflectionNormal(gn,-d,out.normal);}
        if(m.roughnessTexture&&image(3))out.transmissionRoughness=glm::clamp(texel(image(3),out.uv).g,0.f,1.f);
        out.opacity=m.parameters.factors.w>0?(base.a<m.parameters.factors.w?0.f:1.f):glm::clamp(base.a,0.f,1.f);
        out.emission=(out.frontFace || m.extension.settings.w>0)?glm::vec3(m.parameters.emissiveNormal):glm::vec3(0);
        if(m.extension.settings.z>0 && luminance(out.emission)==0)out.emission=out.albedo;
    }
};
CpuScene::CpuScene(const render::RenderWorldSnapshot &input,const std::vector<DielectricMaterial> &dielectrics):state_(std::make_unique<State>()) {
    const auto totalStarted=std::chrono::steady_clock::now();
    auto snapshot=input;if(snapshot.terrain){snapshot.draws.push_back(freezeTerrain(*snapshot.terrain));snapshot.terrain.reset();}
    if(!snapshot.frame.oceans.empty())throw std::invalid_argument("PT: ocean FFT must be frozen with captureProcedural before constructing CpuScene");
    auto &s=*state_;s.camera=snapshot.frame.cameraPosition;s.inverseProjection=glm::inverse(snapshot.frame.viewProjection);s.lights=snapshot.frame.lights;s.inverseSquare=snapshot.frame.inverseSquareLocalLights;s.projection=snapshot.frame.viewProjection;
    auto ray=[&](float x,float y){auto p=s.inverseProjection*glm::vec4(2*x-1,1-2*y,1,1);return glm::normalize(glm::vec3(p)/p.w-s.camera);};
    s.cameraForward=ray(.5f,.5f);auto a=ray(0,0),b=ray(1,0),c=ray(0,1);a/=glm::dot(a,s.cameraForward);b/=glm::dot(b,s.cameraForward);c/=glm::dot(c,s.cameraForward);s.filmArea=glm::length(glm::cross(b-a,c-a));
    if(!snapshot.frame.directionalEnabled)for(auto &light:s.lights)if(light.positionType.w==0)light.colorInner=glm::vec4(0);
    s.time=snapshot.frame.timeSeconds;
    const auto geometryStarted=std::chrono::steady_clock::now();std::unordered_map<const render::MeshPayload*,uint32_t> geometryIds;std::vector<Bounds> instanceBounds;
    for(const auto &draw:snapshot.draws) {
        if(!draw.mesh || draw.mesh->indices.empty())continue;
        if(draw.mesh->indices.size()>UINT32_MAX || draw.mesh->indices.size()%3)throw std::invalid_argument("PT: non-triangle mesh indices");
        const float determinant=glm::determinant(glm::mat3(draw.model));if(!std::isfinite(determinant) || std::abs(determinant)<1e-15f)throw std::invalid_argument("PT: singular mesh transform");
        if(draw.material)for(const auto &image:draw.material->images)if(image && !image->pixels.empty() && (!image->width || !image->height || image->width>INT32_MAX || image->height>INT32_MAX || uint64_t(image->width)*image->height*4!=image->pixels.size()))throw std::invalid_argument("PT: invalid material image extent");
        if(draw.pathTracingShoreline[0]){
            for(const auto &image:draw.pathTracingShoreline)if(!image||!image->width||!image->height||uint64_t(image->width)*image->height*4!=image->pixels.size())throw std::invalid_argument("PT: invalid shoreline image");
            const auto H=draw.extension.shoreHeight,S=draw.extension.shoreSurface;
            if(!finite(glm::vec3(H))||!std::isfinite(H.w)||!finite(glm::vec3(S))||!std::isfinite(S.w)||S.x<=0||H.y<=0||H.z+H.w<=0||S.y>=S.z)throw std::invalid_argument("PT: invalid shoreline parameters");
        }
        State::Mesh mesh;mesh.material=draw.material;mesh.parameters=draw.parameters;mesh.extension=draw.extension;mesh.shoreline=draw.pathTracingShoreline;
        mesh.ior=draw.pathTracingIor;mesh.absorption=draw.pathTracingAbsorption;mesh.scattering=draw.pathTracingScattering;mesh.g=draw.pathTracingAnisotropy;mesh.normalScale=draw.pathTracingNormalScale;mesh.kind=draw.pathTracingKind;
        mesh.bsdfModel=draw.pathTracingBsdfModel;if(mesh.bsdfModel>3)throw std::invalid_argument("PT: unsupported BSDF model");mesh.transmissionRoughness=draw.pathTracingRoughness;mesh.roughnessTexture=draw.pathTracingRoughnessTexture;mesh.diffuseTransmission=draw.pathTracingDiffuseTransmission;mesh.transmissionTexture=draw.pathTracingTransmissionTexture;
        if(!finite(mesh.diffuseTransmission)||glm::any(glm::lessThan(mesh.diffuseTransmission,glm::vec3(0)))||glm::any(glm::greaterThan(mesh.diffuseTransmission,glm::vec3(1)))||(mesh.bsdfModel==3&&(mesh.ior>0||mesh.kind==5)))throw std::invalid_argument("PT: invalid thin diffuse transmission");
        if(mesh.transmissionTexture&&(!mesh.transmissionTexture->width||!mesh.transmissionTexture->height||uint64_t(mesh.transmissionTexture->width)*mesh.transmissionTexture->height*4!=mesh.transmissionTexture->pixels.size()))throw std::invalid_argument("PT: invalid transmission texture");
        if(mesh.kind==5&&(mesh.ior<=1||mesh.transmissionRoughness!=0||glm::any(glm::notEqual(mesh.absorption,glm::vec3(0)))||glm::any(glm::notEqual(mesh.scattering,glm::vec3(0)))))throw std::invalid_argument("PT: thin dielectric requires smooth sheet and no volume coefficients");
        if(!std::isfinite(mesh.transmissionRoughness)||mesh.transmissionRoughness<0||mesh.transmissionRoughness>1)throw std::invalid_argument("PT: dielectric roughness must be finite and in [0,1]");
        if(mesh.ior!=0&&(!std::isfinite(mesh.ior)||mesh.ior<=1||mesh.ior>4))throw std::invalid_argument("PT: invalid procedural IOR");
        if(!finite(mesh.scattering)||glm::any(glm::lessThan(mesh.scattering,glm::vec3(0)))||!std::isfinite(mesh.g)||std::abs(mesh.g)>=.99f)throw std::invalid_argument("PT: invalid scattering coefficient/anisotropy");
        if(!finite(mesh.absorption)||glm::any(glm::lessThan(mesh.absorption,glm::vec3(0)))||!std::isfinite(mesh.normalScale)||mesh.normalScale<=0)throw std::invalid_argument("PT: invalid procedural absorption/normal scale");
        for(const auto &material:dielectrics)if(material.objectId==draw.objectId){if(!std::isfinite(material.ior)||material.ior<=1||material.ior>4)throw std::invalid_argument("PT: invalid dielectric IOR");mesh.ior=material.ior;}
        if((mesh.kind==4||glm::any(glm::greaterThan(mesh.scattering,glm::vec3(0))))&&mesh.ior<=0)throw std::invalid_argument("PT: scattering/subsurface material requires a dielectric boundary");
        for(int column=0;column<4;++column)for(int row=0;row<4;++row)if(!std::isfinite(draw.model[column][row]))throw std::invalid_argument("PT: non-finite instance transform");
        if(draw.model[0][3]!=0||draw.model[1][3]!=0||draw.model[2][3]!=0||draw.model[3][3]!=1)throw std::invalid_argument("PT: instance transform must be affine");
        mesh.model=draw.model;mesh.inverse=glm::inverse(draw.model);mesh.normal=glm::transpose(glm::mat3(mesh.inverse));mesh.mirrored=determinant<0;
        for(int column=0;column<4;++column)for(int row=0;row<4;++row)if(!std::isfinite(mesh.inverse[column][row]))throw std::invalid_argument("PT: non-finite inverse instance transform");
        auto found=geometryIds.find(draw.mesh.get());
        if(found==geometryIds.end()){
            State::Geometry geometry;geometry.source=draw.mesh;const auto &source=*draw.mesh;
            if(!source.pathTracingTangents.empty()&&source.pathTracingTangents.size()!=source.vertices.size())throw std::invalid_argument("PT: tangent count differs from vertices");
            for(const auto &t:source.pathTracingTangents)if(!finite(glm::vec3(t))||!std::isfinite(t.w)||(t.w!=0&&t.w!=1&&t.w!=-1))throw std::invalid_argument("PT: invalid tangent frame");
            for(const auto &v:source.vertices)if(!finite(v.position)||!finite(v.normal)||!std::isfinite(v.uv.x)||!std::isfinite(v.uv.y))throw std::invalid_argument("PT: invalid mesh vertex");
            for(auto index:source.indices)if(index>=source.vertices.size())throw std::invalid_argument("PT: invalid mesh index");
            std::vector<Bounds> bounds;bounds.reserve(source.indices.size()/3);
            for(uint32_t i=0;i<source.indices.size();i+=3){const auto a=source.vertices[source.indices[i]].position,b=source.vertices[source.indices[i+1]].position,c=source.vertices[source.indices[i+2]].position;auto cross=glm::cross(b-a,c-a);if(glm::dot(cross,cross)<=1e-24f)continue;
                Bounds box;box.add(a);box.add(b);box.add(c);bounds.push_back(box);geometry.bounds.add(box);geometry.offsets.push_back(i);}
            const auto blasStarted=std::chrono::steady_clock::now();geometry.blas.construct(bounds);s.statistics.blasSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-blasStarted).count();
            auto offsets=geometry.offsets;for(size_t i=0;i<offsets.size();++i)geometry.offsets[i]=offsets[geometry.blas.order[i]];
            geometry.blas.order.clear();geometry.blas.order.shrink_to_fit();mesh.geometry=uint32_t(s.geometries.size());geometryIds[draw.mesh.get()]=mesh.geometry;s.geometries.push_back(std::move(geometry));
        }else mesh.geometry=found->second;
        const auto &geometry=s.geometries[mesh.geometry];if(geometry.offsets.empty())continue;
        if(uint64_t(s.primitiveCount)+geometry.offsets.size()>=UINT32_MAX)throw std::length_error("PT: too many instanced triangles");
        mesh.primitiveBase=s.primitiveCount;s.primitiveCount+=uint32_t(geometry.offsets.size());Bounds world;
        for(int corner=0;corner<8;++corner){glm::vec3 p;for(int axis=0;axis<3;++axis)p[axis]=(corner&(1<<axis))?geometry.bounds.high[axis]:geometry.bounds.low[axis];p=glm::vec3(mesh.model*glm::vec4(p,1));if(!finite(p))throw std::invalid_argument("PT: invalid transformed bounds");world.add(p);}
        instanceBounds.push_back(world);++s.kindCounts[mesh.kind];s.scatteringMeshes+=glm::any(glm::greaterThan(mesh.scattering,glm::vec3(0)));s.dielectricMeshes+=mesh.ior>0;s.reflectionInterfaces+=mesh.ior>0&&mesh.kind!=5;s.meshes.push_back(std::move(mesh));
    }
    for(auto &m:s.meshes){m.shadowOpaque=m.kind!=5&&(m.parameters.factors.w>0||m.parameters.albedoAlpha.a>=1);
        if(m.parameters.factors.w==0&&m.material&&m.material->images[0]){const auto &pixels=m.material->images[0]->pixels;for(size_t i=3;i<pixels.size();i+=4)if(pixels[i]<255){m.shadowOpaque=false;break;}}
        s.shadowOpaque&=m.shadowOpaque;
    }
    // Mirror reflection is invariant under normal sign. Group near-identical
    // world-space orientations, then keep the largest eight by transformed area.
    // Omitted/curved sheets retain support through the original BSDF proposal.
    std::map<std::array<int,3>,std::pair<glm::vec3,double>> sheetOrientations;
    for(const auto &m:s.meshes)if(m.kind==5&&m.ior>0){
        const auto &g=s.geometries[m.geometry];
        for(uint32_t i=0;i<g.offsets.size();++i){auto v=s.vertices(m.primitiveBase+i);auto cross=glm::cross(v[1].position-v[0].position,v[2].position-v[0].position);float length=glm::length(cross);if(length<=1e-12f)continue;auto n=cross/length;
            int axis=0;if(std::abs(n.y)>std::abs(n.x))axis=1;if(std::abs(n.z)>std::abs(n[axis]))axis=2;if(n[axis]<0)n=-n;
            std::array<int,3> key{int(std::round(n.x*10000)),int(std::round(n.y*10000)),int(std::round(n.z*10000))};auto &group=sheetOrientations[key];if(group.second==0)group.first=n;group.second+=length*.5;
        }
    }
    std::vector<std::pair<glm::vec3,double>> ranked;for(const auto &entry:sheetOrientations)ranked.push_back(entry.second);
    std::stable_sort(ranked.begin(),ranked.end(),[](const auto &a,const auto &b){return a.second>b.second;});
    for(size_t i=0;i<std::min(size_t(8),ranked.size());++i)s.thinSolarNormals.push_back(ranked[i].first);
    s.statistics.geometrySeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-geometryStarted).count()-s.statistics.blasSeconds;
    auto stage=std::chrono::steady_clock::now();s.tlas.construct(instanceBounds);s.statistics.tlasSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-stage).count();
    stage=std::chrono::steady_clock::now();
    for(const auto &m:s.meshes){const float energy=luminance(glm::vec3(m.parameters.emissiveNormal));if(energy<=0)continue;
        const auto &g=s.geometries[m.geometry];std::vector<uint32_t> sourceOrder(g.offsets.size());std::iota(sourceOrder.begin(),sourceOrder.end(),0u);std::sort(sourceOrder.begin(),sourceOrder.end(),[&](uint32_t a,uint32_t b){return g.offsets[a]<g.offsets[b];});
        for(uint32_t i:sourceOrder){const uint32_t id=m.primitiveBase+i;auto v=s.vertices(id);auto cross=glm::cross(v[1].position-v[0].position,v[2].position-v[0].position);const float area=glm::length(cross)*.5f;auto normal=unit(cross);if(glm::dot(normal,v[0].normal)<0)normal=-normal;
            s.emitterWeight+=double(area)*energy;s.emitterIndex[id]=uint32_t(s.emitters.size());s.emitters.push_back({id,area,s.emitterWeight,normal});}}
    s.statistics.emitterSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-stage).count();
    stage=std::chrono::steady_clock::now();s.cameraMedia=initialMedia(s.camera,&s.cameraWinding);s.statistics.cameraMediaSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-stage).count();
    s.statistics.totalSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-totalStarted).count();
    std::cout<<"CPU PT: "<<s.geometries.size()<<" unique meshes, "<<s.meshes.size()<<" instances, "<<s.primitiveCount<<" instanced triangles, "<<nodeCount()<<" BLAS/TLAS nodes; "<<memoryBytes()/1048576.0<<" MiB geometry/BVH; build "<<s.statistics.totalSeconds<<" s\n"<<std::flush;
}
CpuScene::~CpuScene()=default;
size_t CpuScene::triangles() const{return state_->primitiveCount;}
size_t CpuScene::meshCount() const{return state_->meshes.size();}
size_t CpuScene::dielectricCount() const{return state_->dielectricMeshes;}
size_t CpuScene::proceduralCount(uint32_t kind) const{auto found=state_->kindCounts.find(kind);return found==state_->kindCounts.end()?0:found->second;}
float CpuScene::capturedTime() const{return state_->time;}
size_t CpuScene::scatteringCount() const{return state_->scatteringMeshes;}
const std::vector<glm::vec3> &CpuScene::thinSolarNormals() const{return state_->thinSolarNormals;}
std::vector<MediumInfo> CpuScene::media() const {std::vector<MediumInfo> result;for(uint32_t i=0;i<state_->meshes.size();++i){const auto &m=state_->meshes[i];if(m.ior>0&&m.kind!=5)result.push_back({i+1,m.kind,m.ior,{m.absorption,m.scattering,m.g},m.transmissionRoughness});}return result;}
std::array<uint32_t,8> CpuScene::initialMedia(glm::vec3 origin,std::array<uint32_t,8> *winding) const {
    std::array<uint32_t,8> result{};if(winding)winding->fill(0);if(!dielectricCount())return result;
    std::vector<int32_t> counts(state_->meshes.size(),0);std::vector<uint32_t> exits;Surface surface;
    for(int i=0;i<256;++i){if(!intersect(origin,{0,1,0},1e-6f,std::numeric_limits<float>::infinity(),surface))break;
        if(surface.mediumId){auto id=surface.mediumId;counts[id-1]+=surface.frontFace?-1:1;if(!surface.frontFace&&std::find(exits.begin(),exits.end(),id)==exits.end())exits.push_back(id);}
        origin=surface.position+glm::vec3(0,std::max(1e-6f,2*std::numeric_limits<float>::epsilon()*std::abs(surface.position.y)),0);if(i==255)throw std::runtime_error("PT: initial medium boundary scan exceeded limit");}
    std::vector<uint32_t> inside;for(auto id:exits)if(counts[id-1]>0)inside.push_back(id);
    if(inside.size()>result.size())throw std::runtime_error("PT: more than eight nested camera media");std::reverse(inside.begin(),inside.end());std::stable_partition(inside.begin(),inside.end(),[&](uint32_t id){return state_->meshes[id-1].kind==3;});
    for(size_t i=0;i<inside.size();++i){result[i]=inside[i];if(winding)(*winding)[i]=uint32_t(counts[inside[i]-1]);}return result;
}
glm::vec3 CpuScene::initialAbsorption(glm::vec3 origin) const {auto media=initialMedia(origin);uint32_t id=0;for(auto n:media)if(n)id=n;return id?state_->meshes[id-1].absorption:glm::vec3(0);}
AccelerationStats CpuScene::accelerationStats() const {
    const auto &s=*state_;auto a=s.statistics;a.uniqueMeshes=s.geometries.size();a.instances=s.meshes.size();a.expandedTriangles=s.primitiveCount;a.tlasNodes=s.tlas.nodes.size();
    a.tlasBytes=s.tlas.nodes.capacity()*sizeof(State::Node)+s.tlas.order.capacity()*4;a.instanceBytes=s.meshes.capacity()*sizeof(State::Mesh);
    a.emitterBytes=s.emitters.capacity()*sizeof(State::AreaLight)+s.emitterIndex.size()*sizeof(std::pair<const uint32_t,uint32_t>)+s.emitterIndex.bucket_count()*sizeof(void*);
    for(const auto &g:s.geometries){a.uniqueTriangles+=g.offsets.size();a.blasNodes+=g.blas.nodes.size();a.blasBytes+=g.blas.nodes.capacity()*sizeof(State::Node)+g.offsets.capacity()*4;a.geometryBytes+=g.source->vertices.capacity()*sizeof(render::MeshVertex)+g.source->indices.capacity()*4+g.source->pathTracingTangents.capacity()*sizeof(glm::vec4);}
    return a;
}
size_t CpuScene::nodeCount() const{auto a=accelerationStats();return a.blasNodes+a.tlasNodes;}
size_t CpuScene::memoryBytes() const {auto a=accelerationStats();return a.geometryBytes+a.blasBytes+a.tlasBytes+a.instanceBytes+a.emitterBytes;}
SceneData CpuScene::exportData() const {
    const auto &s=*state_;SceneData data;data.inverseProjection=s.inverseProjection;data.camera=s.camera;for(int i=0;i<8;++i){data.cameraMedia[i/4][i%4]=s.cameraMedia[i];data.cameraWinding[i/4][i%4]=s.cameraWinding[i];}data.lights=s.lights;data.inverseSquare=s.inverseSquare;data.emitterWeight=s.emitterWeight;
    static_assert(sizeof(PackedVertex)==48 && sizeof(PackedNode)==32 && sizeof(PackedInstance)==160 && sizeof(PackedMaterial)==192 && sizeof(PackedEmitter)==32,"GPU scene ABI");
    auto bits=[](uint32_t n){float f;std::memcpy(&f,&n,4);return f;};
    data.nodes.reserve(nodeCount());data.instanceOrder=s.tlas.order;
    for(const auto &node:s.tlas.nodes)data.nodes.push_back({glm::vec4(node.low,bits(node.first)),glm::vec4(node.high,bits(node.count))});
    std::vector<uint32_t> roots,triangleBases;
    for(const auto &geometry:s.geometries){
        const auto nodeBase=uint32_t(data.nodes.size()),triangleBase=uint32_t(data.triangles.size()),vertexBase=uint32_t(data.vertices.size());roots.push_back(nodeBase);triangleBases.push_back(triangleBase);
        if(uint64_t(vertexBase)+geometry.source->vertices.size()>=UINT32_MAX)throw std::length_error("PT: GPU vertex addressing overflow");
        for(size_t i=0;i<geometry.source->vertices.size();++i){const auto &v=geometry.source->vertices[i];data.vertices.push_back({glm::vec4(v.position,v.uv.x),glm::vec4(v.normal,v.uv.y),geometry.source->pathTracingTangents.empty()?glm::vec4(0):geometry.source->pathTracingTangents[i]});}
        for(const auto offset:geometry.offsets){const auto &indices=geometry.source->indices;data.triangles.push_back({vertexBase+indices[offset],vertexBase+indices[offset+1],vertexBase+indices[offset+2],0});}
        for(const auto &node:geometry.blas.nodes){const auto first=node.first+(node.count?triangleBase:nodeBase);data.nodes.push_back({glm::vec4(node.low,bits(first)),glm::vec4(node.high,bits(node.count))});}
    }
    std::unordered_map<const render::ImageRGBA8*,uint32_t> imageIds;
    auto image=[&](const std::shared_ptr<const render::ImageRGBA8> &value){
        if(!value || value->pixels.empty())return UINT32_MAX;
        auto it=imageIds.find(value.get());if(it!=imageIds.end())return it->second;
        if(data.texels.size()+value->pixels.size()/4>=UINT32_MAX)throw std::length_error("PT: GPU texel addressing overflow");
        const uint32_t id=uint32_t(data.images.size());imageIds[value.get()]=id;data.images.push_back({uint32_t(data.texels.size()),value->width,value->height,0});
        for(size_t i=0;i<value->pixels.size();i+=4)data.texels.push_back(uint32_t(value->pixels[i])|(uint32_t(value->pixels[i+1])<<8)|(uint32_t(value->pixels[i+2])<<16)|(uint32_t(value->pixels[i+3])<<24));return id;
    };
    for(const auto &mesh:s.meshes){
        data.instances.push_back({mesh.model,mesh.inverse,{roots[mesh.geometry],uint32_t(data.materials.size()),mesh.primitiveBase,triangleBases[mesh.geometry]},{uint32_t(s.geometries[mesh.geometry].offsets.size()),mesh.mirrored?1u:0u,mesh.geometry,s.geometries[mesh.geometry].source->pathTracingTangents.empty()?0u:1u}});
        PackedMaterial material{mesh.parameters.albedoAlpha,mesh.parameters.emissiveNormal,mesh.parameters.factors,glm::vec4(mesh.ior,mesh.kind==3?1:0,mesh.normalScale,float((mesh.kind==4?1u:0u)|(mesh.roughnessTexture?2u:0u)|(mesh.shadowOpaque?4u:0u))),glm::vec4(mesh.absorption,mesh.transmissionRoughness),glm::vec4(mesh.scattering,mesh.g),glm::uvec4(UINT32_MAX),glm::uvec4(UINT32_MAX,mesh.extension.settings.w>0?1:0,mesh.extension.settings.z>0?1:0,(mesh.kind==5?1u:0u)|(mesh.bsdfModel<<1u))};
        if(mesh.material){for(int i=0;i<4;++i)material.textures[i]=image(mesh.material->images[i]);material.extra.x=image(mesh.material->images[4]);}if(mesh.shoreline[0]){material.shoreHeight=mesh.extension.shoreHeight;material.shoreSurface=mesh.extension.shoreSurface;for(int i=0;i<4;++i)material.shoreTextures[i]=image(mesh.shoreline[i]);}material.diffuseTransmission=glm::vec4(mesh.diffuseTransmission,bits(image(mesh.transmissionTexture)));data.materials.push_back(material);
    }
    for(const auto &light:s.emitters)data.emitters.push_back({{bits(light.primitive),light.area,float(light.cumulative/s.emitterWeight),0},glm::vec4(light.normal,0)});
    return data;
}
void CpuScene::cameraRay(float u,float v,glm::vec3 &o,glm::vec3 &d) const {
    auto far=state_->inverseProjection*glm::vec4(u*2-1,1-v*2,1,1);o=state_->camera;d=unit(glm::vec3(far)/far.w-o,{0,0,-1});
}
EmitterSample CpuScene::sampleEmitter(Random &rng) const {
    const auto &s=*state_;if(s.emitters.empty())return {};
    double selected=rng.uniform()*s.emitterWeight;auto it=std::upper_bound(s.emitters.begin(),s.emitters.end(),selected,[](double value,const auto &light){return value<light.cumulative;});size_t index=std::min(size_t(it-s.emitters.begin()),s.emitters.size()-1);const auto &light=s.emitters[index];
    auto uv=rng.uniform2();float a=std::sqrt(uv[0]);glm::vec2 bary{a*(1-uv[1]),a*uv[1]};auto v=s.vertices(light.primitive);auto p=v[0].position*(1-a)+v[1].position*bary.x+v[2].position*bary.y;Surface hit;s.surface(light.primitive,bary,0,p,-light.normal,hit);hit.normal=hit.geometricNormal;return {hit,emitterPdfArea(light.primitive)};
}
float CpuScene::emitterPdfArea(uint32_t primitive) const {
    const auto &s=*state_;auto found=s.emitterIndex.find(primitive);if(found==s.emitterIndex.end())return 0;auto index=found->second;const auto &light=s.emitters[index];double weight=light.cumulative-(index?s.emitters[index-1].cumulative:0);return float(weight/s.emitterWeight/light.area);
}
float CpuScene::cameraPdf(glm::vec3 direction) const {const auto &s=*state_;float cosine=glm::dot(s.cameraForward,direction);return cosine>0?1/(s.filmArea*cosine*cosine*cosine):0;}
bool CpuScene::project(glm::vec3 point,glm::vec2 &uv,float &pdf) const {
    const auto &s=*state_;auto clip=s.projection*glm::vec4(point,1);if(clip.w<=0)return false;auto ndc=glm::vec3(clip)/clip.w;uv={ndc.x*.5f+.5f,.5f-ndc.y*.5f};pdf=cameraPdf(glm::normalize(point-s.camera));return uv.x>=0&&uv.x<1&&uv.y>=0&&uv.y<1;
}
void CpuScene::validateBidirectional() const {
    for(const auto &mesh:state_->meshes)if(mesh.bsdfModel==3)throw std::invalid_argument("BDPT thin diffuse strategy densities are not yet validated; use CPU/GPU unidirectional PT");
    for(const auto &mesh:state_->meshes)if(mesh.ior==0&&(mesh.bsdfModel==1||mesh.bsdfModel==2))throw std::invalid_argument("BDPT bump-corrected closure adjoints are not yet validated; use CPU/GPU unidirectional PT");
    if(proceduralCount(5))throw std::invalid_argument("BDPT thin dielectric strategy densities are not implemented; use CPU/GPU unidirectional PT");
    for(const auto &mesh:state_->meshes)if(mesh.ior>0&&(mesh.transmissionRoughness>=.02f||mesh.roughnessTexture))throw std::invalid_argument("BDPT rough dielectric strategy densities are not implemented; use CPU/GPU unidirectional PT");
    if(scatteringCount())throw std::invalid_argument("BDPT volume/SSS strategy densities are not implemented; use CPU/GPU unidirectional PT");
    if(proceduralCount(3))throw std::invalid_argument("BDPT water-medium strategy densities are not implemented; use CPU/GPU unidirectional PT for oceans");
    const auto &s=*state_;if(environment||sunRadius>0)throw std::invalid_argument("BDPT reference currently supports finite area lights; HDR/sun require a separate endpoint implementation");
    for(const auto &light:s.lights)if(luminance(glm::vec3(light.colorInner))>0)throw std::invalid_argument("BDPT reference requires area emitters instead of local/directional lights");
    if(s.emitters.empty())throw std::invalid_argument("BDPT requires an area emitter");
    for(const auto &mesh:s.meshes){if(mesh.parameters.albedoAlpha.a<1&&mesh.parameters.factors.w==0)throw std::invalid_argument("BDPT does not support blended coverage");if(mesh.material&&mesh.parameters.factors.w==0&&mesh.material->images[0]){const auto &pixels=mesh.material->images[0]->pixels;for(size_t i=3;i<pixels.size();i+=4)if(pixels[i]<255)throw std::invalid_argument("BDPT does not support blended texture coverage");}}
}
bool CpuScene::opaqueShadows() const {return state_->shadowOpaque;}
bool CpuScene::intersectsAny(glm::vec3 o,glm::vec3 d,float minimum,float maximum) const {
    const auto &s=*state_;
    auto traverse=[&](const State::Tree &tree,glm::vec3 origin,glm::vec3 direction,const auto &leaf){
        if(tree.nodes.empty())return false;std::array<uint32_t,64> stack{};uint32_t size=1;stack[0]=0;
        while(size){const auto &node=tree.nodes[stack[--size]];float near;if(!boxHit(node.low,node.high,origin,direction,minimum,maximum,near))continue;
            if(node.count){for(uint32_t i=0;i<node.count;++i)if(leaf(node.first+i))return true;}
            else {const auto &a=tree.nodes[node.first],&b=tree.nodes[node.first+1];float na,nb;bool ha=boxHit(a.low,a.high,origin,direction,minimum,maximum,na),hb=boxHit(b.low,b.high,origin,direction,minimum,maximum,nb);
                if(ha&&hb){if(na<nb){stack[size++]=node.first+1;stack[size++]=node.first;}else{stack[size++]=node.first;stack[size++]=node.first+1;}}else if(ha)stack[size++]=node.first;else if(hb)stack[size++]=node.first+1;
            }
        }return false;
    };
    return traverse(s.tlas,o,d,[&](uint32_t ordered){auto instance=s.tlas.order[ordered];const auto &m=s.meshes[instance];auto origin=glm::vec3(m.inverse*glm::vec4(o,1)),direction=glm::vec3(m.inverse*glm::vec4(d,0));
        return traverse(s.geometries[m.geometry].blas,origin,direction,[&](uint32_t local){float t;glm::vec2 bary;return s.triangle(m.primitiveBase+local,origin,direction,minimum,maximum,t,bary,true,instance);});
    });
}
bool CpuScene::intersectShadow(glm::vec3 o,glm::vec3 d,float minimum,float maximum,Surface &transparent,bool &opaque) const {
    const auto &s=*state_;uint32_t closestId=UINT32_MAX;glm::vec2 closestBary{};float closest=maximum;opaque=false;
    auto traverse=[&](const State::Tree &tree,glm::vec3 origin,glm::vec3 direction,const auto &leaf){
        if(tree.nodes.empty())return false;std::array<uint32_t,64> stack{};uint32_t size=1;stack[0]=0;
        while(size){const auto &node=tree.nodes[stack[--size]];float near;if(!boxHit(node.low,node.high,origin,direction,minimum,maximum,near))continue;
            if(node.count){for(uint32_t i=0;i<node.count;++i)if(leaf(node.first+i))return true;}
            else {const auto &a=tree.nodes[node.first],&b=tree.nodes[node.first+1];float na,nb;bool ha=boxHit(a.low,a.high,origin,direction,minimum,maximum,na),hb=boxHit(b.low,b.high,origin,direction,minimum,maximum,nb);
                if(ha&&hb){if(na<nb){stack[size++]=node.first+1;stack[size++]=node.first;}else{stack[size++]=node.first;stack[size++]=node.first+1;}}else if(ha)stack[size++]=node.first;else if(hb)stack[size++]=node.first+1;
            }
        }return false;
    };
    opaque=traverse(s.tlas,o,d,[&](uint32_t ordered){auto instance=s.tlas.order[ordered];const auto &m=s.meshes[instance];auto origin=glm::vec3(m.inverse*glm::vec4(o,1)),direction=glm::vec3(m.inverse*glm::vec4(d,0));
        return traverse(s.geometries[m.geometry].blas,origin,direction,[&](uint32_t local){float t;glm::vec2 bary;auto id=m.primitiveBase+local;if(!s.triangle(id,origin,direction,minimum,maximum,t,bary,true,instance))return false;
            if(m.shadowOpaque)return true;
            if(m.kind!=5){auto v=s.vertices(id);auto uv=v[0].uv*(1-bary.x-bary.y)+v[1].uv*bary.x+v[2].uv*bary.y;if(texel(m.material?m.material->images[0]:nullptr,uv).a*m.parameters.albedoAlpha.a>=1)return true;}
            if(t<closest||closestId==UINT32_MAX){closest=t;closestId=id;closestBary=bary;}return false;
        });
    });
    if(opaque)return true;if(closestId==UINT32_MAX)return false;s.surface(closestId,closestBary,closest,o,d,transparent);return true;
}
std::pair<glm::vec3,glm::vec3> CpuScene::bounds() const {
    if(state_->tlas.nodes.empty())return {{0,0,0},{0,0,0}};
    return {state_->tlas.nodes[0].low,state_->tlas.nodes[0].high};
}
void CpuScene::validatePhotonMapping() const {
    if(scatteringCount())throw std::invalid_argument("Photon mapping currently supports absorption-only media; volume photon gathering is not implemented");
    for(const auto &mesh:state_->meshes){
        if(mesh.bsdfModel>0||mesh.kind==5)throw std::invalid_argument("Photon mapping reference requires default PBR/solid dielectric closures; thin and controlled closure adjoints are not implemented");
        if(mesh.parameters.albedoAlpha.a<1&&mesh.parameters.factors.w==0)throw std::invalid_argument("Photon mapping does not support blended coverage");
        if(mesh.material&&mesh.parameters.factors.w==0&&mesh.material->images[0]){const auto &pixels=mesh.material->images[0]->pixels;for(size_t i=3;i<pixels.size();i+=4)if(pixels[i]<255)throw std::invalid_argument("Photon mapping does not support blended texture coverage");}
    }
    for(const auto &light:state_->lights)if(luminance(glm::vec3(light.colorInner))>0)throw std::invalid_argument("Photon mapping requires area/HDR/finite sun endpoints; analytic point/spot/directional light emission is not implemented");
}
bool CpuScene::intersect(glm::vec3 o,glm::vec3 d,float minimum,float maximum,Surface &out,bool brute) const {
    const auto &s=*state_;uint32_t id=UINT32_MAX;glm::vec2 bestBary;float closest=maximum;
    auto test=[&](uint32_t primitive){float t;glm::vec2 bary;if(s.triangle(primitive,o,d,minimum,closest,t,bary)){if(t<closest || id==UINT32_MAX){closest=t;id=primitive;bestBary=bary;}}};
    if(brute)for(uint32_t i=0;i<s.primitiveCount;++i)test(i);
    else {
        auto traverse=[&](const State::Tree &tree,glm::vec3 origin,glm::vec3 direction,const auto &leaf){
            if(tree.nodes.empty())return;std::array<uint32_t,64> stack{};uint32_t size=1;stack[0]=0;
            while(size){const auto &node=tree.nodes[stack[--size]];float near;if(!boxHit(node.low,node.high,origin,direction,minimum,closest,near))continue;
                if(node.count){for(uint32_t i=0;i<node.count;++i)leaf(node.first+i);}
                else{const auto &a=tree.nodes[node.first],&b=tree.nodes[node.first+1];float na,nb;bool ha=boxHit(a.low,a.high,origin,direction,minimum,closest,na),hb=boxHit(b.low,b.high,origin,direction,minimum,closest,nb);
                    if(ha&&hb){if(na<nb){stack[size++]=node.first+1;stack[size++]=node.first;}else{stack[size++]=node.first;stack[size++]=node.first+1;}}else if(ha)stack[size++]=node.first;else if(hb)stack[size++]=node.first+1;}}
        };
        traverse(s.tlas,o,d,[&](uint32_t ordered){const auto instance=s.tlas.order[ordered];const auto &m=s.meshes[instance];const auto localOrigin=glm::vec3(m.inverse*glm::vec4(o,1)),localDirection=glm::vec3(m.inverse*glm::vec4(d,0));
            // Do not normalize: the local ray parameter remains world-space t.
            traverse(s.geometries[m.geometry].blas,localOrigin,localDirection,[&](uint32_t local){const uint32_t primitive=m.primitiveBase+local;float t;glm::vec2 bary;if(s.triangle(primitive,localOrigin,localDirection,minimum,closest,t,bary,true,instance)&&(t<closest||id==UINT32_MAX)){closest=t;id=primitive;bestBary=bary;}});
        });
    }
    if(id==UINT32_MAX)return false;s.surface(id,bestBary,closest,o,d,out);return true;
}
glm::vec3 CpuScene::trace(glm::vec3 origin,glm::vec3 direction,Random &rng,uint32_t depth,uint64_t &rays,uint64_t *volumeEvents,bool waterSunProposal,bool thinSunProposal,const PhotonMap *photons,bool shadowAnyHit,glm::vec3 *caustics) const {
    const auto &s=*state_;glm::vec3 throughput(1),radiance(0),previousPoint(0);float previousPdf=0;
    std::array<uint32_t,8> winding{};auto media=origin==s.camera?s.cameraMedia:initialMedia(origin,&winding);if(origin==s.camera)winding=s.cameraWinding;uint32_t mediaCount=0;while(mediaCount<8&&media[mediaCount])++mediaCount;
    auto transition=[&](const Surface &h){
        if(!h.mediumId)return;uint32_t found=mediaCount;for(uint32_t i=0;i<mediaCount;++i)if(media[i]==h.mediumId)found=i;
        if(h.frontFace){if(found<mediaCount){if(++winding[found]>64)throw std::runtime_error("PT: excessive scan self-overlap");return;}if(mediaCount==8)throw std::runtime_error("PT: medium stack overflow");
            if(h.water){for(uint32_t i=mediaCount;i>0;--i){media[i]=media[i-1];winding[i]=winding[i-1];}media[0]=h.mediumId;winding[0]=1;++mediaCount;}else{media[mediaCount]=h.mediumId;winding[mediaCount++]=1;}
        }else if(found<mediaCount){if(winding[found]>1){--winding[found];return;}if(!h.water&&found+1!=mediaCount)throw std::runtime_error("PT: overlapping non-water media are unsupported");for(uint32_t i=found+1;i<mediaCount;++i){media[i-1]=media[i];winding[i-1]=winding[i];}--mediaCount;}
    };
    const float epsilon=scatteringCount()?1e-6f:1e-4f;const float sunCos=std::cos(sunRadius),sunPdf=sunRadius>0?1/(2*pi*(1-sunCos)):0;
    auto offset=[&](const Surface &hit,glm::vec3 d){return hit.position+hit.geometricNormal*((glm::dot(d,hit.geometricNormal)>=0?1.f:-1.f)*std::max(epsilon,8*std::numeric_limits<float>::epsilon()*std::max({std::abs(hit.position.x),std::abs(hit.position.y),std::abs(hit.position.z)})));};
    const bool fastShadows=shadowAnyHit&&opaqueShadows();
    auto visibility=[&](glm::vec3 o,glm::vec3 d,float maximum,float exteriorIor){glm::vec3 transmission(1);Surface hit;
        if(fastShadows){++rays;return intersectsAny(o,d,epsilon,maximum)?glm::vec3(0):glm::vec3(1);}
        for(int skip=0;skip<128;++skip){++rays;bool opaque=false;bool found=shadowAnyHit?intersectShadow(o,d,epsilon,maximum,hit,opaque):intersect(o,d,epsilon,maximum,hit);if(opaque)return glm::vec3(0);if(!found)return transmission;
            auto sheet=hit.thinDielectric?hit.albedo*(1-thinDielectricReflectance(glm::dot(hit.geometricNormal,d),exteriorIor,hit.ior)):glm::vec3(0);
            transmission*=glm::vec3(1-hit.opacity)+hit.opacity*sheet;if(std::max({transmission.x,transmission.y,transmission.z})<1e-5f)return glm::vec3(0);if(std::isfinite(maximum)){maximum-=hit.distance+epsilon;if(maximum<=epsilon)return transmission;}o=hit.position+d*epsilon;}
        return glm::vec3(0);
    };
    for(uint32_t bounce=0,transparent=0;;) {
        const uint32_t dimension=2+bounce*256;
        const uint32_t mediumId=mediaCount?media[mediaCount-1]:0;
        const auto medium=mediumId?Medium{s.meshes[mediumId-1].absorption,s.meshes[mediumId-1].scattering,s.meshes[mediumId-1].g}:Medium{};
        Surface hit;++rays;const bool hasSurface=intersect(origin,direction,epsilon,std::numeric_limits<float>::infinity(),hit);
        rng.dimension(dimension+32+transparent*65536);auto flight=sampleMedium(medium,hasSurface?hit.distance:std::numeric_limits<float>::infinity(),rng);throughput*=flight.weight;
        if(!finite(throughput))return glm::vec3(std::numeric_limits<float>::quiet_NaN());if(luminance(throughput)<=0)break;
        const bool volume=flight.scattered;
        if(!volume&&!hasSurface){
            if(environment){auto e=environment->evaluate(direction);const float weight=previousPdf>0?power(previousPdf,environment->pdf(direction)):1;radiance+=throughput*e*weight;}
            if(sunRadius>0 && glm::dot(direction,sunDirection)>=sunCos){const float weight=previousPdf>0?power(previousPdf,sunPdf):1;radiance+=throughput*sunIrradiance/(pi*std::sin(sunRadius)*std::sin(sunRadius))*weight;}break;
        }
        if(volume){if(bounce>=depth)break;if(volumeEvents)++*volumeEvents;hit=Surface{};hit.position=origin+direction*flight.distance;}
        else {
            // Water is the host medium: its interface inside a solid is invisible.
            if((hit.water&&mediumId&&s.meshes[mediumId-1].kind!=3)||(hit.mediumId==mediumId&&mediumId&&(hit.frontFace||winding[mediaCount-1]>1))){transition(hit);if(++transparent>128)break;origin=offset(hit,direction);continue;}
            rng.dimension(dimension+20+transparent);
            if(hit.opacity<1 && rng.uniform()>=hit.opacity){if(++transparent>128)break;origin=offset(hit,direction);continue;}
            float emissionWeight=1;auto foundEmitter=s.emitterIndex.find(hit.primitive);const int32_t emitter=foundEmitter==s.emitterIndex.end()?-1:int32_t(foundEmitter->second);
            if(previousPdf>0 && emitter>=0){const auto &light=s.emitters[emitter];const float cosine=std::abs(glm::dot(light.normal,-direction));const double mass=light.cumulative-(emitter?s.emitters[emitter-1].cumulative:0);const float lightPdf=cosine>0?float(mass/s.emitterWeight)*glm::dot(hit.position-previousPoint,hit.position-previousPoint)/(light.area*cosine):0;emissionWeight=power(previousPdf,lightPdf);}
            radiance+=throughput*hit.emission*emissionWeight;if(bounce>=depth)break;
            hit.exteriorIor=hit.frontFace?(mediumId?s.meshes[mediumId-1].ior:1.f):(mediaCount>1&&mediumId==hit.mediumId?s.meshes[media[mediaCount-2]-1].ior:mediumId!=hit.mediumId&&mediumId?s.meshes[mediumId-1].ior:1.f);
            if(hit.ior>0&&!hit.thinDielectric){float eta=hit.frontFace?hit.exteriorIor/hit.ior:hit.ior/hit.exteriorIor;auto refracted=glm::refract(direction,hit.normal,eta);if(glm::dot(glm::reflect(direction,hit.normal),hit.geometricNormal)<=0||(glm::dot(refracted,refracted)>0&&glm::dot(refracted,hit.geometricNormal)>=0))hit.normal=hit.geometricNormal;}
            if(hit.ior>0&&hit.transmissionRoughness<.02f&&(!hit.water||hit.foam<=0)){rng.dimension(dimension+12);auto sample=sampleBsdf(hit,-direction,rng);if(sample.pdf<=0)break;throughput*=sample.value*(std::abs(glm::dot(hit.normal,sample.direction))/sample.pdf);if(sample.transmission)transition(hit);
                // Straight sheet transmission retains the preceding NEE/MIS pair.
                if(!hit.thinDielectric||!sample.transmission){previousPdf=0;previousPoint=hit.position;}origin=offset(hit,sample.direction);direction=sample.direction;++bounce;continue;}
        }
        const auto view=-direction;
        const bool photonMerge=photons&&!volume&&hit.ior==0;
        if(photonMerge){auto estimate=photons->estimate(hit,view,depth-bounce);radiance+=throughput*estimate.radiance;if(caustics)*caustics+=throughput*estimate.caustics;}
        // A continuation proposal, not a straight shadow connection through a refractive boundary.
        // Retaining the original BSDF/phase component gives full support and unchanged expectation.
        glm::vec3 sunCenter(0);float coneCos=0;bool sunProposal=false;
        const bool waterExit=mediumId&&s.meshes[mediumId-1].kind==3;
        const bool waterReflection=waterSunProposal&&!mediumId&&!volume&&hit.ior==0&&s.reflectionInterfaces>0;
        if(!photonMerge&&waterSunProposal&&(waterExit||waterReflection)&&sunRadius>0&&sunDirection.y>0&&(volume||hit.ior==0)){
            sunCenter=waterExit?-glm::refract(-sunDirection,glm::vec3(0,1,0),1.f/s.meshes[mediumId-1].ior):glm::reflect(sunDirection,glm::vec3(0,1,0));
            float residual=0,width=0;bool found=false;
            for(int i=0;i<4;++i){Surface boundary;++rays;
                if(!intersect(volume?hit.position+sunCenter*epsilon:offset(hit,sunCenter),sunCenter,epsilon,std::numeric_limits<float>::infinity(),boundary))break;
                if(waterExit&&!boundary.water)break;
                if(waterExit&&(boundary.mediumId!=mediumId||boundary.frontFace))break;
                if(waterReflection&&(boundary.ior<=0||boundary.thinDielectric||!boundary.frontFace||glm::dot(boundary.geometricNormal,glm::vec3(0,1,0))<.5f))break;
                auto desired=waterExit?-glm::refract(-sunDirection,-boundary.normal,1.f/boundary.ior):glm::reflect(sunDirection,boundary.normal);
                if(glm::dot(desired,desired)<1e-12f)break;
                desired=unit(desired);residual=glm::length(desired-sunCenter);sunCenter=desired;found=true;
                if(waterReflection)width=4*boundary.transmissionRoughness*boundary.transmissionRoughness;
                if(residual<.001f)break;
            }
            if(found){coneCos=std::cos(glm::clamp(std::max(2*sunRadius,.01f)+residual+width,.01f,.15f));sunProposal=true;}
        }
        std::array<glm::vec4,9> cones{};std::array<float,9> coneWeights{};uint32_t coneCount=0;
        if(sunProposal)cones[coneCount++]=glm::vec4(sunCenter,coneCos);
        const uint32_t waterConeCount=coneCount;
        if(!photonMerge&&waterSunProposal&&thinSunProposal&&!mediumId&&!volume&&hit.ior==0&&sunRadius>0){
            const float edge=std::cos(std::max(sunRadius+2e-4f,.001f));
            for(const auto n:s.thinSolarNormals){auto center=glm::reflect(sunDirection,n);
                if(luminance(evaluateBsdf(hit,view,center))>0)cones[coneCount++]=glm::vec4(center,edge);
            }
        }
        const uint32_t thinConeCount=coneCount-waterConeCount;
        // Reserve only 10% for sheet chains; retain 90% of the existing BSDF/water mixture.
        const float thinMix=thinConeCount?.1f:0.f,baseWeight=(waterConeCount?.5f:1.f)*(1-thinMix);
        if(waterConeCount)coneWeights[0]=.5f*(1-thinMix);
        for(uint32_t i=waterConeCount;i<coneCount;++i)coneWeights[i]=thinMix/float(thinConeCount);
        sunProposal=coneCount>0;
        // Sum every overlapping component, rather than using the chosen cone's PDF.
        auto conePdf=[&](glm::vec3 l){float pdf=0;for(uint32_t i=0;i<coneCount;++i)if(glm::dot(l,glm::vec3(cones[i]))>=cones[i].w)pdf+=coneWeights[i]/(2*pi*(1-cones[i].w));return pdf;};
        auto continuation=[&](glm::vec3 l){const float base=volume?phaseHG(glm::dot(direction,l),medium.g):bsdfPdf(hit,view,l);if(!thinConeCount)return waterConeCount?.5f*(base+(glm::dot(l,sunCenter)>=coneCos?1/(2*pi*(1-coneCos)):0.f)):base;return baseWeight*base+conePdf(l);};
        // Alpha coverage occupies offsets 20..148; proposal draws must not alias it.
        auto selectCone=[&](){if(!sunProposal)return false;rng.dimension(dimension+192);return rng.uniform()<1-baseWeight;};
        auto sampleCone=[&](){uint32_t index=0;if(coneCount>1){rng.dimension(dimension+193);float selected=rng.uniform()*(1-baseWeight),mass=coneWeights[0];while(index+1<coneCount&&selected>=mass)mass+=coneWeights[++index];}const auto cone=cones[index];rng.dimension(dimension+194);const auto uv=rng.uniform2();float c=glm::mix(1.f,cone.w,uv[0]),r=std::sqrt(std::max(0.f,1-c*c)),phi=2*pi*uv[1];return local(glm::vec3(r*std::cos(phi),r*std::sin(phi),c),glm::vec3(cone));};
        auto direct=[&](glm::vec3 l,glm::vec3 energy,float pdf,float distance,bool delta){
            if(pdf<=0)return;const float cosine=volume?1.f:surfaceCosine(hit,l);if(cosine==0)return;
            const auto f=volume?glm::vec3(phaseHG(glm::dot(direction,l),medium.g)):evaluateBsdf(hit,view,l);if(luminance(f)<=0)return;
            auto bias=[&](glm::vec3 point){return std::max(epsilon,8*std::numeric_limits<float>::epsilon()*std::max({std::abs(point.x),std::abs(point.y),std::abs(point.z)}));};
            const float maximum=std::isfinite(distance)?std::max(epsilon,distance-2*std::max(bias(hit.position),bias(hit.position+l*distance))):distance;
            const float weight=delta||photonMerge?1:power(pdf,continuation(l));
            uint32_t shadowMedium=mediumId;if(!volume&&hit.ior>0&&glm::dot(l,hit.geometricNormal)<0)shadowMedium=hit.frontFace?hit.mediumId:(mediaCount>1?media[mediaCount-2]:0);
            const auto transmission=visibility(volume?hit.position+l*bias(hit.position):offset(hit,l),l,maximum,shadowMedium?s.meshes[shadowMedium-1].ior:1.f);
            const auto extinction=shadowMedium?s.meshes[shadowMedium-1].absorption+s.meshes[shadowMedium-1].scattering:glm::vec3(0);
            auto attenuation=transmittance(extinction,distance);radiance+=throughput*f*energy*attenuation*(cosine*transmission*weight/pdf);
        };
        rng.dimension(dimension);
        if(environment){auto e=environment->sample(rng);direct(e.direction,e.radiance,e.pdf,std::numeric_limits<float>::infinity(),false);}
        rng.dimension(dimension+4);
        bool atmosphericSun=false;
        for(const auto &light:s.lights) {
            glm::vec3 color(light.colorInner);const int type=int(light.positionType.w);
            if(type==0) {
                if(sunRadius>0 && !atmosphericSun){atmosphericSun=true;float cosine=glm::mix(1.f,sunCos,rng.uniform()),sine=std::sqrt(std::max(0.f,1-cosine*cosine)),phi=2*pi*rng.uniform();auto l=local({sine*std::cos(phi),sine*std::sin(phi),cosine},sunDirection);direct(l,sunIrradiance/(pi*std::sin(sunRadius)*std::sin(sunRadius)),sunPdf,std::numeric_limits<float>::infinity(),false);}
                else direct(unit(-glm::vec3(light.directionOuter)),color,1,std::numeric_limits<float>::infinity(),true);
            } else {
                auto to=glm::vec3(light.positionType)-hit.position;const float distance=glm::length(to);if(distance<epsilon)continue;auto l=to/distance;
                if(type==2){float angle=glm::dot(-l,unit(glm::vec3(light.directionOuter)));color*=glm::clamp((angle-light.directionOuter.w)/std::max(1e-6f,light.colorInner.w-light.directionOuter.w),0.f,1.f);}
                if(s.inverseSquare)color/=std::max(distance*distance,1e-6f);
                direct(l,color,1,std::max(epsilon,distance),true);
            }
        }
        if(sunRadius>0 && !atmosphericSun){float cosine=glm::mix(1.f,sunCos,rng.uniform()),sine=std::sqrt(std::max(0.f,1-cosine*cosine)),phi=2*pi*rng.uniform();direct(local({sine*std::cos(phi),sine*std::sin(phi),cosine},sunDirection),sunIrradiance/(pi*std::sin(sunRadius)*std::sin(sunRadius)),sunPdf,std::numeric_limits<float>::infinity(),false);}
        rng.dimension(dimension+8);
        if(!s.emitters.empty()) {
            const double selected=double(rng.uniform())*s.emitterWeight;const auto it=std::upper_bound(s.emitters.begin(),s.emitters.end(),selected,[](double value,const auto &light){return value<light.cumulative;});const size_t index=std::min(size_t(it-s.emitters.begin()),s.emitters.size()-1);const auto &light=s.emitters[index];
            auto vertices=s.vertices(light.primitive);const auto uv=rng.uniform2();const float a=std::sqrt(uv[0]),b=uv[1];const glm::vec2 bary{a*(1-b),a*b};auto p=vertices[0].position*(1-a)+vertices[1].position*bary.x+vertices[2].position*bary.y;
            auto to=p-hit.position;float distance=glm::length(to);auto l=unit(to);
            Surface emitterSurface;s.surface(light.primitive,bary,distance,hit.position,l,emitterSurface);
            const float cosine=std::abs(glm::dot(light.normal,-l));if(cosine>1e-8f && distance>2*epsilon){const double mass=light.cumulative-(index?s.emitters[index-1].cumulative:0);direct(l,emitterSurface.emission*emitterSurface.opacity,float(mass/s.emitterWeight)*distance*distance/(light.area*cosine),distance,false);}
        }
        if(photonMerge)break; // NEE estimates direct light; photons replace all indirect light.
        if(volume){glm::vec3 l;if(selectCone())l=sampleCone();else{rng.dimension(dimension+36);l=samplePhaseHG(direction,medium.g,rng);}previousPdf=continuation(l);throughput*=phaseHG(glm::dot(direction,l),medium.g)/previousPdf;previousPoint=hit.position;origin=hit.position;direction=l;++bounce;rng.dimension(dimension+16);if(bounce>=3){float survive=glm::clamp(std::max({throughput.r,throughput.g,throughput.b}),.05f,.95f);if(rng.uniform()>=survive)break;throughput/=survive;}continue;}
        rng.dimension(dimension+12);
        BsdfSample sample;if(selectCone()){sample.direction=sampleCone();sample.value=evaluateBsdf(hit,view,sample.direction);}else{rng.dimension(dimension+12);sample=sampleBsdf(hit,view,rng);}if(sunProposal)sample.pdf=continuation(sample.direction);if(sample.pdf<=1e-20f || luminance(sample.value)<=0)break;
        if(hit.ior>0){throughput*=sample.value*(std::abs(glm::dot(hit.normal,sample.direction))/sample.pdf);if(sample.transmission)transition(hit);previousPdf=sample.delta?0:sample.pdf;previousPoint=hit.position;origin=offset(hit,sample.direction);direction=sample.direction;++bounce;continue;}
        throughput*=sample.value*(surfaceCosine(hit,sample.direction)/sample.pdf);
        if(!finite(throughput))return glm::vec3(std::numeric_limits<float>::quiet_NaN());
        previousPdf=sample.pdf;previousPoint=hit.position;origin=offset(hit,sample.direction);direction=sample.direction;++bounce;
        rng.dimension(dimension+16);
        if(bounce>=3){const float survive=glm::clamp(std::max({throughput.r,throughput.g,throughput.b}),.05f,.95f);if(rng.uniform()>=survive)break;throughput/=survive;}
    }
    return radiance;
}
void validateOptions(const Options &options) {
    if(!options.checkpointSamples||options.checkpointSamples>1048576)throw std::invalid_argument("PT: invalid checkpoint interval");
    if(!options.width || !options.height || options.width>16384 || options.height>16384 || !options.samples || options.samples>1048576 || !options.maxDepth || options.maxDepth>128 || options.threads>256 || !std::isfinite(options.exposure) || options.exposure<=0 || !options.minimumSamples || !std::isfinite(options.relativeError) || options.relativeError<=0 || !std::isfinite(options.absoluteError) || options.absoluteError<0)throw std::invalid_argument("PT: invalid render options");
    if(!options.trainingSamples || options.trainingSamples>512 || !options.cacheMinimum || options.cacheMinimum>8192 || !options.cacheDepth || options.cacheDepth>128 || !std::isfinite(options.guideCellSize) || options.guideCellSize<0)throw std::invalid_argument("PT: invalid learning options");
    if(!options.gpuBatchSamples||options.gpuBatchSamples>64)throw std::invalid_argument("PT: GPU batch must be 1..64 samples");
    if(!options.photonPaths||options.photonPaths>4000000||!std::isfinite(options.photonRadius)||options.photonRadius<1e-5f)throw std::invalid_argument("PT: invalid photon options");
    if(options.photonMapping&&(options.bdpt||options.guiding||options.radianceCache||options.adaptive))throw std::invalid_argument("Photon mapping requires fixed spp without BDPT/guiding/cache");
    if(options.bdpt&&(options.adaptive||options.guiding||options.radianceCache||options.maxDepth>32))throw std::invalid_argument("BDPT requires fixed spp, depth <=32, without guiding/cache");
    if((options.guiding||options.radianceCache)&&uint64_t(options.width)*options.height*options.trainingSamples>268435456)throw std::invalid_argument("PT: training path budget exceeds atomic counter limit");
    if(uint64_t(options.width)*options.height>67108864)throw std::invalid_argument("PT: image exceeds CPU allocation limit");
}
Image render(const CpuScene &scene,const Options &options,const std::function<void(const Image &)> &progress) {
    if(options.bdpt)return renderBdpt(scene,options,progress);
    if(options.guiding||options.radianceCache)throw std::invalid_argument("Guiding/cache require GPU PT");
    validateOptions(options);const size_t pixels=size_t(options.width)*options.height;
    Image image;image.width=options.width;image.height=options.height;image.radiance.resize(pixels,glm::vec3(0));image.albedo.resize(pixels,glm::vec3(0));image.normal.resize(pixels,glm::vec3(0));image.sampleCounts.resize(pixels,0);
    std::unique_ptr<PhotonMap> photons;
    if(options.photonMapping){photons=std::make_unique<PhotonMap>(scene,options);image.caustics.resize(pixels,glm::vec3(0));image.photonSeconds=photons->seconds;image.photonRays=photons->rays;image.storedPhotons=photons->photons().size();image.causticPhotons=photons->causticCount;image.photonBytes=photons->memoryBytes();image.execution="CPU photon mapping (fixed-radius indirect density estimate)";}
    std::vector<glm::vec3> m2(pixels,glm::vec3(0));std::vector<uint8_t> stable(pixels,0);
    const uint32_t tilesX=(options.width+15)/16,tilesY=(options.height+15)/16;
    const uint32_t workers=std::min(tilesX*tilesY,options.threads?options.threads:std::max(1u,std::thread::hardware_concurrency()>1?std::thread::hardware_concurrency()-1:1));
    const auto started=std::chrono::steady_clock::now();
    for(uint32_t first=0;first<options.samples;) {
        const uint32_t interval=options.adaptive?32:options.checkpointSamples;
        const uint32_t end=std::min(options.samples,first<4?4u:first<16?16u:uint32_t(std::min(uint64_t(options.samples),uint64_t(first)+interval)));
        std::atomic<uint32_t> next{0};std::atomic<uint64_t> rays{0},invalid{0},volumeEvents{0};std::vector<std::thread> threads;std::exception_ptr failure;std::mutex failureMutex;
        auto job=[&]{try {uint64_t localRays=0,localInvalid=0,localVolumeEvents=0;
            while(true){const uint32_t tile=next.fetch_add(1);if(tile>=tilesX*tilesY)break;const uint32_t x0=tile%tilesX*16,y0=tile/tilesX*16;
                for(uint32_t y=y0;y<std::min(y0+16,options.height);++y)for(uint32_t x=x0;x<std::min(x0+16,options.width);++x){const size_t pixel=size_t(y)*options.width+x;
                    if(stable[pixel]>=2)continue;
                    if(first==0){glm::vec3 o,d;scene.cameraRay((x+.5f)/options.width,(y+.5f)/options.height,o,d);Surface h;++localRays;if(scene.intersect(o,d,1e-4f,std::numeric_limits<float>::infinity(),h)){image.albedo[pixel]=h.albedo;image.normal[pixel]=h.normal;}}
                    for(uint32_t sample=first;sample<end;++sample){auto random=Random::forPixel(options.seed,uint32_t(pixel),sample,options.sobol);glm::vec3 o,d;const float u=(x+random.uniform())/options.width,v=(y+random.uniform())/options.height;scene.cameraRay(u,v,o,d);glm::vec3 caustic(0);auto value=scene.trace(o,d,random,options.maxDepth,localRays,&localVolumeEvents,options.waterSunProposal,options.thinSunProposal,photons.get(),options.shadowAnyHit,&caustic);if(!finite(value)){++localInvalid;continue;}const uint32_t n=++image.sampleCounts[pixel];if(photons)image.caustics[pixel]+=(caustic-image.caustics[pixel])/float(n);auto delta=value-image.radiance[pixel];image.radiance[pixel]+=delta/float(n);m2[pixel]+=delta*(value-image.radiance[pixel]);}
                    const uint32_t n=image.sampleCounts[pixel];
                    if(options.adaptive && n>=std::min(options.minimumSamples,options.samples) && n>1){auto error=glm::sqrt(glm::max(m2[pixel],glm::vec3(0))/float(n-1)/float(n))*1.96f;auto threshold=glm::vec3(options.absoluteError)+glm::abs(image.radiance[pixel])*options.relativeError;stable[pixel]=glm::all(glm::lessThanEqual(error,threshold))?uint8_t(stable[pixel]+1):0;}
                }
            }rays+=localRays;invalid+=localInvalid;volumeEvents+=localVolumeEvents;
        }catch(...){std::lock_guard<std::mutex> lock(failureMutex);if(!failure)failure=std::current_exception();next=tilesX*tilesY;}};
        try{for(uint32_t i=0;i<workers;++i)threads.emplace_back(job);}catch(...){next=tilesX*tilesY;for(auto &thread:threads)thread.join();throw;}
        for(auto &thread:threads)thread.join();if(failure)std::rethrow_exception(failure);
        image.volumeEvents+=volumeEvents.load();image.rays+=rays.load();image.nonFiniteSamples+=invalid.load();image.samples=end;image.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        image.totalSamples=std::accumulate(image.sampleCounts.begin(),image.sampleCounts.end(),uint64_t(0));image.convergedPixels=uint32_t(std::count_if(stable.begin(),stable.end(),[](auto n){return n>=2;}));
        if(image.nonFiniteSamples)throw std::runtime_error("PT: non-finite path contribution");
        std::cout<<"CPU PT "<<end<<" spp budget, "<<double(image.totalSamples)/pixels<<" average spp, "<<image.seconds<<" s, "<<image.rays<<" rays, "<<workers<<" workers\n"<<std::flush;
        if(progress){auto checkpoint=std::chrono::steady_clock::now();progress(image);image.checkpointSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-checkpoint).count();}first=end;if(image.convergedPixels==pixels)break;
    }
    image.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    return image;
}

namespace {
void png(const std::vector<glm::vec3> &pixels,uint32_t width,uint32_t height,float exposure,const std::string &path,bool tone,bool normal=false) {
    std::vector<uint8_t> rgb(size_t(width)*height*3);
    for(size_t i=0;i<pixels.size();++i){auto c=normal?(glm::dot(pixels[i],pixels[i])>0?pixels[i]*.5f+.5f:glm::vec3(0)):pixels[i];
        if(tone)c=glm::pow(1.f-glm::exp(-glm::max(c,glm::vec3(0))*exposure),glm::vec3(1/2.2f));
        else if(!normal)c=glm::pow(glm::max(c,glm::vec3(0)),glm::vec3(1/2.2f));
        c=glm::clamp(c,0.f,1.f);for(int k=0;k<3;++k)rgb[i*3+k]=uint8_t(c[k]*255+.5f);
    }
    if(!stbi_write_png(path.c_str(),int(width),int(height),3,rgb.data(),int(width)*3))throw std::runtime_error("PT: cannot write "+path);
}
}
void writeImage(const Image &image,float exposure,const std::string &prefix) {
    const auto parent=std::filesystem::path(prefix).parent_path();if(!parent.empty())std::filesystem::create_directories(parent);
    png(image.radiance,image.width,image.height,exposure,prefix+".png",true);
    if(!image.caustics.empty())png(image.caustics,image.width,image.height,exposure,prefix+"-caustics.png",true);
    if(!image.albedo.empty())png(image.albedo,image.width,image.height,exposure,prefix+"-albedo.png",false);
    if(!image.sampleCounts.empty()){std::vector<glm::vec3> counts;counts.reserve(image.sampleCounts.size());for(auto n:image.sampleCounts)counts.emplace_back(float(n)/std::max(1u,image.samples));png(counts,image.width,image.height,1,prefix+"-samples.png",false);}
    if(!image.normal.empty())png(image.normal,image.width,image.height,exposure,prefix+"-normal.png",false,true);
    if(!image.denoised.empty())png(image.denoised,image.width,image.height,exposure,prefix+"-denoised.png",true);
    auto writePfm=[&](const std::vector<glm::vec3> &pixels,const std::string &path){
    std::ofstream pfm(path,std::ios::binary);const uint16_t endian=1;const bool little=*reinterpret_cast<const uint8_t*>(&endian)==1;
    pfm<<"PF\n"<<image.width<<" "<<image.height<<"\n"<<(little?"-1.0":"1.0")<<"\n";
    for(uint32_t y=image.height;y>0;--y)for(uint32_t x=0;x<image.width;++x){const auto value=pixels[size_t(y-1)*image.width+x];const float rgb[3]={value.r,value.g,value.b};pfm.write(reinterpret_cast<const char*>(rgb),sizeof(rgb));}
    if(!pfm)throw std::runtime_error("PT: cannot write linear HDR PFM");};
    writePfm(image.radiance,prefix+".pfm");if(!image.caustics.empty())writePfm(image.caustics,prefix+"-caustics.pfm");
    if(!image.albedo.empty())writePfm(image.albedo,prefix+"-albedo.pfm");
    if(!image.normal.empty())writePfm(image.normal,prefix+"-normal.pfm");
    if(!image.denoised.empty())writePfm(image.denoised,prefix+"-denoised.pfm");
}
void writeReport(const Image &image,const CpuScene &scene,const Options &options,const std::string &prefix,const std::string &name) {
    nlohmann::json data={{"scene",name},{"width",image.width},{"height",image.height},{"samples",image.samples},{"max_depth",options.maxDepth},{"seed",options.seed},{"requested_threads",options.threads},{"exposure",options.exposure},{"triangles",scene.triangles()},{"meshes",scene.meshCount()},{"bvh_nodes",scene.nodeCount()},{"geometry_bvh_bytes",scene.memoryBytes()},{"render_seconds",image.seconds},{"rays",image.rays},{"non_finite_samples",image.nonFiniteSamples},{"integrator","Lambert + GGX VNDF, environment/sun/emitter NEE, power MIS, Russian roulette"},{"terrain_ocean","frozen procedural meshes"}};
    if(options.bdpt)data["integrator"]="BDPT: finite area endpoints, pinhole camera, connection MIS, camera splats, smooth dielectric radiance/importance";
    if(scene.scatteringCount())data["integrator"]="RGB homogeneous random walk BSSRDF/volume, HG phase, dielectric boundaries, environment/sun/emitter NEE + MIS, Russian roulette";
    data["water_sun_proposal"]=options.waterSunProposal;data["volume_scattering_events"]=image.volumeEvents;data["scattering_meshes"]=scene.scatteringCount();data["subsurface_meshes"]=scene.proceduralCount(4);data["dielectric_meshes"]=scene.dielectricCount();data["terrain_meshes"]=scene.proceduralCount(1);data["grass_meshes"]=scene.proceduralCount(2);data["ocean_interfaces"]=scene.proceduralCount(3);data["frozen_time_seconds"]=scene.capturedTime();
    data["water_sun_proposal_version"]=options.waterSunProposal?(options.thinSunProposal?3:2):0;
    data["thin_sun_proposal"]=options.waterSunProposal&&options.thinSunProposal;
    data["thin_sun_proposal_fraction"]=options.waterSunProposal&&options.thinSunProposal?.1f:0.f;
    data["thin_solar_normals"]=nlohmann::json::array();for(const auto n:scene.thinSolarNormals())data["thin_solar_normals"].push_back({n.x,n.y,n.z});
    data["media"]=nlohmann::json::array();for(const auto &m:scene.media())data["media"].push_back({{"id",m.id},{"kind",m.kind},{"ior",m.ior},{"dielectric_roughness",m.roughness},{"sigma_a",{m.volume.absorption.x,m.volume.absorption.y,m.volume.absorption.z}},{"sigma_s",{m.volume.scattering.x,m.volume.scattering.y,m.volume.scattering.z}},{"hg_g",m.volume.g}});
    data["guiding"]=options.guiding;data["radiance_cache"]=options.radianceCache;data["bdpt"]=options.bdpt;data["training_spp"]=options.guiding||options.radianceCache?options.trainingSamples:0;data["training_seconds"]=image.trainingSeconds;data["training_rays"]=image.trainingRays;data["guide_hits"]=image.guideHits;data["cache_hits"]=image.cacheHits;data["trained_cells"]=image.trainedCells;data["guide_cell_size"]=image.guideCellSize;data["trace_and_training_seconds"]=image.seconds+image.trainingSeconds;data["cache_minimum"]=options.cacheMinimum;data["cache_depth"]=options.cacheDepth;
    data["photon_mapping"]=options.photonMapping;data["photon_paths"]=options.photonMapping?options.photonPaths:0;data["photon_radius"]=options.photonRadius;data["photon_seconds"]=image.photonSeconds;data["photon_rays"]=image.photonRays;data["stored_photons"]=image.storedPhotons;data["caustic_photons"]=image.causticPhotons;data["photon_bytes"]=image.photonBytes;data["shadow_any_hit"]=options.shadowAnyHit;data["opaque_shadow_scene"]=scene.opaqueShadows();data["biased_indirect"]=options.photonMapping;data["trace_photon_training_seconds"]=image.seconds+image.photonSeconds+image.trainingSeconds;
    if(options.photonMapping)data["integrator"]="PT delta camera paths + direct NEE + fixed-radius photon density indirect; biased, no volume gathering";
    data["setup_seconds"]=image.setupSeconds;data["gpu_buffer_bytes"]=image.gpuBufferBytes;
    const auto acceleration=scene.accelerationStats();
    data["acceleration"]={{"type","software BLAS/TLAS"},{"unique_meshes",acceleration.uniqueMeshes},{"unique_triangles",acceleration.uniqueTriangles},{"instances",acceleration.instances},{"instanced_triangles",acceleration.expandedTriangles},{"blas_nodes",acceleration.blasNodes},{"tlas_nodes",acceleration.tlasNodes},{"geometry_bytes",acceleration.geometryBytes},{"blas_bytes",acceleration.blasBytes},{"tlas_bytes",acceleration.tlasBytes},{"instance_bytes",acceleration.instanceBytes},{"emitter_bytes_estimate",acceleration.emitterBytes}};
    data["timings"]={{"domain","CPU wall time; submit/readback may include GPU waits; no hardware GPU timestamps"},{"scene_geometry_prepare_seconds",acceleration.geometrySeconds},{"blas_build_seconds",acceleration.blasSeconds},{"tlas_build_seconds",acceleration.tlasSeconds},{"emitter_prepare_seconds",acceleration.emitterSeconds},{"camera_media_seconds",acceleration.cameraMediaSeconds},{"scene_construct_seconds",acceleration.totalSeconds},{"scene_export_seconds",image.sceneExportSeconds},{"gpu_setup_including_export_seconds",image.setupSeconds},{"dispatch_submit_seconds",image.dispatchSeconds},{"readback_including_wait_seconds",image.readbackSeconds},{"cpu_film_update_seconds",image.filmUpdateSeconds},{"checkpoint_io_seconds",image.checkpointSeconds}};
    data["gpu_dispatches"]=image.dispatches;data["film_readbacks"]=image.readbacks;data["film_readback_bytes"]=image.readbackBytes;data["checkpoint_samples"]=options.checkpointSamples;data["gpu_batch_samples"]=options.gpuBatchSamples;
    data["denoiser"]=image.denoiser;data["denoise_device"]=image.denoiseDevice;data["denoise_seconds"]=image.denoiseSeconds;data["denoise_auxiliary"]=image.denoiseAuxiliary;
    data["trace_training_denoise_seconds"]=image.seconds+image.trainingSeconds+image.denoiseSeconds;
    if(!image.caustics.empty()){double caustic=0,total=0;for(auto value:image.caustics)caustic+=luminance(value);for(auto value:image.radiance)total+=luminance(value);data["caustic_mean_luminance"]=caustic/image.caustics.size();data["caustic_energy_fraction"]=total>0?caustic/total:0;}
    data["execution"]=image.execution;data["sampler"]=options.sobol?"dyadic-permuted padded 2D Sobol":"PCG";data["adaptive"]=options.adaptive;data["sample_budget"]=options.samples;data["total_samples"]=image.totalSamples;data["average_spp"]=image.sampleCounts.empty()?double(image.samples):double(image.totalSamples)/image.sampleCounts.size();data["converged_pixels"]=image.convergedPixels;data["minimum_samples"]=options.minimumSamples;data["relative_error"]=options.relativeError;data["absolute_error"]=options.absoluteError;
    data["sun_radius_degrees"]=glm::degrees(scene.sunRadius);data["sun_irradiance"]={scene.sunIrradiance.x,scene.sunIrradiance.y,scene.sunIrradiance.z};
    if(scene.environment)data["environment"]={{"width",scene.environment->width()},{"height",scene.environment->height()},{"type","linear HDR equirectangular, importance sampled"}};
    std::ofstream file(prefix+".json");file<<data.dump(2)<<'\n';if(!file)throw std::runtime_error("PT: cannot write render report");
}
} // namespace pt
