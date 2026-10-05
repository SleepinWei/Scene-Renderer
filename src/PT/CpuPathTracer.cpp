#include "PT/CpuPathTracer.h"
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
    const float q=std::max(0.f,1-nh*nh)+a2*nh*nh,D=a2/(pi*q*q),Gv=smith(nv,s.transmissionRoughness),G=Gv*smith(std::abs(nl),s.transmissionRoughness);
    const float F=dielectricFresnel(reflection?.5f*(vh+lh):vh,etaI,etaT),normalPdf=D*Gv*vh/nv;
    if(reflection)return {F*D*G/(4*nv*nl),F*normalPdf/(4*vh),false};
    const float denominator=lh+vh/etap;if(std::abs(denominator)<1e-10f)return {};
    float value=(1-F)*D*G*std::abs(vh*lh)/(nv*std::abs(nl)*denominator*denominator);
    if(mode==TransportMode::Radiance)value/=etap*etap;
    return {value,(1-F)*normalPdf*std::abs(lh)/(denominator*denominator),true};
}
glm::vec4 texel(const std::shared_ptr<const render::ImageRGBA8> &image, glm::vec2 uv,
                 glm::vec4 fallback = glm::vec4(1)) {
    if(!image || image->pixels.empty()) return fallback;
    const int w=int(image->width), h=int(image->height);
    uv=glm::fract(uv);
    const float x=uv.x*w-.5f,y=uv.y*h-.5f; const int ix=int(std::floor(x)),iy=int(std::floor(y));
    auto pixel=[&](int px,int py) { px=(px%w+w)%w;py=(py%h+h)%h;
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
glm::vec3 evaluateBsdf(const Surface &s,glm::vec3 v,glm::vec3 l,TransportMode mode) {
    if(s.ior>0){const float foam=s.water?s.foam:0;auto f=s.transmissionRoughness>=.02f?roughDielectric(s,v,l,mode):RoughDielectric{};
        return (1-foam)*f.value*(f.transmission?s.albedo:glm::vec3(1))+(foam>0&&glm::dot(s.normal,v)>0&&glm::dot(s.normal,l)>0&&glm::dot(s.geometricNormal,l)>0?foam*s.albedo/pi:glm::vec3(0));}
    const float nv=glm::dot(s.normal,v),nl=glm::dot(s.normal,l);
    if(nv<=0 || nl<=0 || glm::dot(s.geometricNormal,v)<=0 || glm::dot(s.geometricNormal,l)<=0)return glm::vec3(0);
    auto h=unit(v+l,s.normal);float nh=std::max(0.f,glm::dot(s.normal,h)),vh=std::max(0.f,glm::dot(v,h));
    auto f0=glm::mix(glm::vec3(.04f),s.albedo,s.metallic);auto f=f0+(1.f-f0)*std::pow(1-vh,5.f);
    return (1.f-f)*s.albedo*(1-s.metallic)/pi+f*(distribution(nh,s.roughness)*smith(nv,s.roughness)*smith(nl,s.roughness)/(4*nv*nl));
}
float bsdfPdf(const Surface &s,glm::vec3 v,glm::vec3 l) {
    if(s.ior>0){const float foam=s.water?s.foam:0;return (1-foam)*(s.transmissionRoughness>=.02f?roughDielectric(s,v,l,TransportMode::Radiance).pdf:0)+(glm::dot(s.normal,v)>0?foam*std::max(0.f,glm::dot(s.normal,l))/pi:0);}
    const float nl=glm::dot(s.normal,l);if(nl<=0 || glm::dot(s.normal,v)<=0)return 0;
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
BsdfSample sampleBsdf(const Surface &s,glm::vec3 v,Random &r,TransportMode mode) {
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
    if(selection<specularChance(s)) {
        const auto half=visibleNormal(v,s.normal,s.roughness,uv);l=glm::reflect(-v,half);
        if(glm::dot(v,half)<=0 || glm::dot(l,s.normal)<=0)return {};
    }else {
        const float q=uv[0],phi=2*pi*uv[1];l=local({std::sqrt(q)*std::cos(phi),std::sqrt(q)*std::sin(phi),std::sqrt(1-q)},s.normal);
    }
    return {l,evaluateBsdf(s,v,l),bsdfPdf(s,v,l)};
}
struct CpuScene::State {
    struct Mesh {std::vector<render::MeshVertex> vertices;std::vector<uint32_t> indices;std::shared_ptr<const render::MaterialPayload> material;render::MaterialParameters parameters;render::MaterialExtension extension;float ior=0,normalScale=1,transmissionRoughness=0;glm::vec3 absorption{0},scattering{0};float g=0;uint32_t kind=0;};
    struct Primitive {uint32_t mesh,offset;};
    struct Node {glm::vec3 low;uint32_t first;glm::vec3 high;uint32_t count;};
    struct AreaLight {uint32_t primitive;float area;double cumulative;glm::vec3 normal;};
    std::vector<Mesh> meshes;std::vector<Primitive> primitives;std::vector<uint32_t> order;std::vector<Node> nodes;
    std::vector<AreaLight> emitters;std::vector<int32_t> emitterIndex;double emitterWeight=0;
    std::vector<render::LightData> lights;glm::mat4 inverseProjection;glm::vec3 camera;
    bool inverseSquare=true;glm::vec3 cameraForward{0,0,-1};std::array<uint32_t,8> cameraMedia{},cameraWinding{};float filmArea=1,time=0;glm::mat4 projection{1};
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
    std::array<const render::MeshVertex*,3> vertices(uint32_t id) const {
        const auto &p=primitives[id];const auto &m=meshes[p.mesh];return {&m.vertices[m.indices[p.offset]],&m.vertices[m.indices[p.offset+1]],&m.vertices[m.indices[p.offset+2]]};
    }
    bool triangle(uint32_t id,glm::vec3 o,glm::vec3 d,float minimum,float maximum,float &distance,glm::vec2 &bary) const {
        auto v=vertices(id);const auto e1=v[1]->position-v[0]->position,e2=v[2]->position-v[0]->position;
        auto p=glm::cross(d,e2);const double determinant=glm::dot(e1,p);
        if(std::abs(determinant)<=1e-10*std::sqrt(double(glm::dot(e1,e1))*glm::dot(e2,e2)))return false;
        auto t=o-v[0]->position;double u=glm::dot(t,p)/determinant;if(u<0 || u>1)return false;
        auto q=glm::cross(t,e1);double w=glm::dot(d,q)/determinant;if(w<0 || u+w>1)return false;
        const double dist=glm::dot(e2,q)/determinant;if(dist<minimum || dist>maximum)return false;
        const auto &m=meshes[primitives[id].mesh];
        if(m.parameters.factors.w>0 && m.material) {
            auto uv=v[0]->uv*float(1-u-w)+v[1]->uv*float(u)+v[2]->uv*float(w);
            if(texel(m.material->images[0],uv).a*m.parameters.albedoAlpha.w<m.parameters.factors.w)return false;
        }
        distance=float(dist);bary={float(u),float(w)};return true;
    }
    void surface(uint32_t id,glm::vec2 bary,float t,glm::vec3 o,glm::vec3 d,Surface &out) const {
        auto v=vertices(id);const auto &m=meshes[primitives[id].mesh];auto image=[&](size_t i){return m.material?m.material->images[i]:std::shared_ptr<const render::ImageRGBA8>{};};
        out=Surface{};out.mediumId=m.ior>0?primitives[id].mesh+1:0;out.ior=m.ior;out.transmissionRoughness=m.transmissionRoughness;out.water=m.kind==3;out.absorption=m.absorption;out.twoSided=m.extension.settings.w>0;out.distance=t;out.primitive=id;out.position=o+d*t;
        out.uv=v[0]->uv*(1-bary.x-bary.y)+v[1]->uv*bary.x+v[2]->uv*bary.y;
        const auto e1=v[1]->position-v[0]->position,e2=v[2]->position-v[0]->position;
        auto n=unit(v[0]->normal*(1-bary.x-bary.y)+v[1]->normal*bary.x+v[2]->normal*bary.y);
        auto gn=unit(glm::cross(e1,e2),n);if(glm::dot(gn,n)<0){if(m.kind==4||m.kind==3)n=-n;else gn=-gn;}
        out.frontFace=glm::dot(gn,d)<0;if(!out.frontFace){gn=-gn;n=-n;}
        out.geometricNormal=gn;out.normal=n;
        if(image(1) && m.parameters.emissiveNormal.w>0) {
            if(out.water){auto map=glm::vec3(texel(image(1),out.uv*m.normalScale))*2.f-1.f;auto mapped=unit(glm::vec3(map.x,map.z,-map.y));if(!out.frontFace)mapped=-mapped;if(glm::dot(mapped,gn)>.05f)out.normal=mapped;}
            else {
            auto a=v[1]->uv-v[0]->uv,b=v[2]->uv-v[0]->uv;const float determinant=a.x*b.y-a.y*b.x;
            if(std::abs(determinant)>1e-10f) {
                auto tangent=(e1*b.y-e2*a.y)/determinant,bitangent=(-e1*b.x+e2*a.x)/determinant;
                tangent=unit(tangent-n*glm::dot(n,tangent));bitangent=unit(bitangent-n*glm::dot(n,bitangent));
                auto map=glm::vec3(texel(image(1),out.uv))*2.f-1.f;map.x*=m.parameters.emissiveNormal.w;map.y*=m.parameters.emissiveNormal.w;
                const auto mapped=unit(tangent*map.x+bitangent*map.y+n*map.z,n);
                if(glm::dot(mapped,gn)>.05f)out.normal=mapped;
            }
            }
        }
        // A shading normal behind the incoming ray produces a black BSDF at grazing angles.
        if(glm::dot(out.normal,-d)<=0)out.normal=glm::dot(n,-d)>0?n:gn;
        const auto base=texel(image(0),out.uv)*m.parameters.albedoAlpha;
        out.albedo=glm::pow(glm::max(glm::vec3(base),glm::vec3(0)),glm::vec3(2.2f));
        out.foam=out.water&&image(2)?glm::clamp(texel(image(2),out.uv*m.normalScale).r,0.f,1.f):0;
        out.metallic=glm::clamp(texel(image(2),out.uv).b*m.parameters.factors.x,0.f,1.f);
        out.roughness=glm::clamp(texel(image(3),out.uv).g*m.parameters.factors.y,.045f,1.f);
        out.opacity=m.parameters.factors.w>0?(base.a<m.parameters.factors.w?0.f:1.f):glm::clamp(base.a,0.f,1.f);
        out.emission=(out.frontFace || m.extension.settings.w>0)?glm::vec3(m.parameters.emissiveNormal):glm::vec3(0);
        if(m.extension.settings.z>0 && luminance(out.emission)==0)out.emission=out.albedo;
    }
};
CpuScene::CpuScene(const render::RenderWorldSnapshot &input,const std::vector<DielectricMaterial> &dielectrics):state_(std::make_unique<State>()) {
    auto snapshot=input;if(snapshot.terrain){snapshot.draws.push_back(freezeTerrain(*snapshot.terrain));snapshot.terrain.reset();}
    if(!snapshot.frame.oceans.empty())throw std::invalid_argument("PT: ocean FFT must be frozen with captureProcedural before constructing CpuScene");
    auto &s=*state_;s.camera=snapshot.frame.cameraPosition;s.inverseProjection=glm::inverse(snapshot.frame.viewProjection);s.lights=snapshot.frame.lights;s.inverseSquare=snapshot.frame.inverseSquareLocalLights;s.projection=snapshot.frame.viewProjection;
    auto ray=[&](float x,float y){auto p=s.inverseProjection*glm::vec4(2*x-1,1-2*y,1,1);return glm::normalize(glm::vec3(p)/p.w-s.camera);};
    s.cameraForward=ray(.5f,.5f);auto a=ray(0,0),b=ray(1,0),c=ray(0,1);a/=glm::dot(a,s.cameraForward);b/=glm::dot(b,s.cameraForward);c/=glm::dot(c,s.cameraForward);s.filmArea=glm::length(glm::cross(b-a,c-a));
    if(!snapshot.frame.directionalEnabled)for(auto &light:s.lights)if(light.positionType.w==0)light.colorInner=glm::vec4(0);
    s.time=snapshot.frame.timeSeconds;
    size_t count=0;for(const auto &draw:snapshot.draws)if(draw.mesh)count+=draw.mesh->indices.size()/3;
    if(count>=UINT32_MAX)throw std::length_error("PT: too many triangles");s.primitives.reserve(count);
    for(const auto &draw:snapshot.draws) {
        if(!draw.mesh || draw.mesh->indices.empty())continue;
        if(draw.mesh->indices.size()>UINT32_MAX || draw.mesh->indices.size()%3)throw std::invalid_argument("PT: non-triangle mesh indices");
        const float determinant=glm::determinant(glm::mat3(draw.model));if(!std::isfinite(determinant) || std::abs(determinant)<1e-15f)throw std::invalid_argument("PT: singular mesh transform");
        if(draw.material)for(const auto &image:draw.material->images)if(image && !image->pixels.empty() && (!image->width || !image->height || image->width>INT32_MAX || image->height>INT32_MAX || uint64_t(image->width)*image->height*4!=image->pixels.size()))throw std::invalid_argument("PT: invalid material image extent");
        State::Mesh mesh;mesh.vertices=draw.mesh->vertices;mesh.indices=draw.mesh->indices;mesh.material=draw.material;mesh.parameters=draw.parameters;mesh.extension=draw.extension;
        mesh.ior=draw.pathTracingIor;mesh.absorption=draw.pathTracingAbsorption;mesh.scattering=draw.pathTracingScattering;mesh.g=draw.pathTracingAnisotropy;mesh.normalScale=draw.pathTracingNormalScale;mesh.kind=draw.pathTracingKind;
        mesh.transmissionRoughness=draw.pathTracingRoughness;
        if(!std::isfinite(mesh.transmissionRoughness)||mesh.transmissionRoughness<0||mesh.transmissionRoughness>1)throw std::invalid_argument("PT: dielectric roughness must be finite and in [0,1]");
        if(mesh.ior!=0&&(!std::isfinite(mesh.ior)||mesh.ior<=1||mesh.ior>4))throw std::invalid_argument("PT: invalid procedural IOR");
        if(!finite(mesh.scattering)||glm::any(glm::lessThan(mesh.scattering,glm::vec3(0)))||!std::isfinite(mesh.g)||std::abs(mesh.g)>=.99f)throw std::invalid_argument("PT: invalid scattering coefficient/anisotropy");
        if(!finite(mesh.absorption)||glm::any(glm::lessThan(mesh.absorption,glm::vec3(0)))||!std::isfinite(mesh.normalScale)||mesh.normalScale<=0)throw std::invalid_argument("PT: invalid procedural absorption/normal scale");
        for(const auto &material:dielectrics)if(material.objectId==draw.objectId){if(!std::isfinite(material.ior)||material.ior<=1||material.ior>4)throw std::invalid_argument("PT: invalid dielectric IOR");mesh.ior=material.ior;}
        if((mesh.kind==4||glm::any(glm::greaterThan(mesh.scattering,glm::vec3(0))))&&mesh.ior<=0)throw std::invalid_argument("PT: scattering/subsurface material requires a dielectric boundary");
        const auto normal=glm::transpose(glm::inverse(glm::mat3(draw.model)));
        for(auto &v:mesh.vertices){v.position=glm::vec3(draw.model*glm::vec4(v.position,1));v.normal=unit(normal*v.normal);if(!finite(v.position) || !std::isfinite(v.uv.x) || !std::isfinite(v.uv.y))throw std::invalid_argument("PT: invalid mesh vertex");}
        for(auto index:mesh.indices)if(index>=mesh.vertices.size())throw std::invalid_argument("PT: invalid mesh index");
        const uint32_t m=uint32_t(s.meshes.size());s.meshes.push_back(std::move(mesh));
        for(uint32_t i=0;i<s.meshes.back().indices.size();i+=3){auto &vertices=s.meshes.back().vertices;auto &indices=s.meshes.back().indices;auto cross=glm::cross(vertices[indices[i+1]].position-vertices[indices[i]].position,vertices[indices[i+2]].position-vertices[indices[i]].position);if(glm::dot(cross,cross)>1e-24f)s.primitives.push_back({m,i});}
    }
    const auto started=std::chrono::steady_clock::now();
    std::vector<Bounds> bounds(s.primitives.size());std::vector<glm::vec3> centers(s.primitives.size());s.order.resize(s.primitives.size());std::iota(s.order.begin(),s.order.end(),0u);
    s.emitterIndex.assign(s.primitives.size(),-1);
    for(uint32_t id=0;id<s.primitives.size();++id) {
        auto v=s.vertices(id);for(auto *vertex:v)bounds[id].add(vertex->position);centers[id]=(bounds[id].low+bounds[id].high)*.5f;
        const auto &m=s.meshes[s.primitives[id].mesh];const float energy=luminance(glm::vec3(m.parameters.emissiveNormal));
        if(energy>0) {auto cross=glm::cross(v[1]->position-v[0]->position,v[2]->position-v[0]->position);const float area=glm::length(cross)*.5f;auto normal=unit(cross);if(glm::dot(normal,v[0]->normal)<0)normal=-normal;
            s.emitterWeight+=double(area)*energy;s.emitterIndex[id]=int32_t(s.emitters.size());s.emitters.push_back({id,area,s.emitterWeight,normal});}
    }
    if(!s.primitives.empty()){s.nodes.reserve(s.primitives.size()/2+2);s.nodes.resize(1);s.build(0,0,uint32_t(s.primitives.size()),bounds,centers,0);}
    s.cameraMedia=initialMedia(s.camera,&s.cameraWinding);
    std::cout<<"CPU PT: "<<s.meshes.size()<<" meshes, "<<s.primitives.size()<<" triangles, "<<s.nodes.size()<<" BVH nodes; "<<memoryBytes()/1048576.0<<" MiB geometry/BVH; build "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<" s\n"<<std::flush;
}
CpuScene::~CpuScene()=default;
size_t CpuScene::triangles() const{return state_->primitives.size();}
size_t CpuScene::meshCount() const{return state_->meshes.size();}
size_t CpuScene::dielectricCount() const{return std::count_if(state_->meshes.begin(),state_->meshes.end(),[](const auto &mesh){return mesh.ior>0;});}
size_t CpuScene::proceduralCount(uint32_t kind) const{return std::count_if(state_->meshes.begin(),state_->meshes.end(),[&](const auto &mesh){return mesh.kind==kind;});}
float CpuScene::capturedTime() const{return state_->time;}
size_t CpuScene::scatteringCount() const{return std::count_if(state_->meshes.begin(),state_->meshes.end(),[](const auto &m){return glm::any(glm::greaterThan(m.scattering,glm::vec3(0)));});}
std::vector<MediumInfo> CpuScene::media() const {std::vector<MediumInfo> result;for(uint32_t i=0;i<state_->meshes.size();++i){const auto &m=state_->meshes[i];if(m.ior>0)result.push_back({i+1,m.kind,m.ior,{m.absorption,m.scattering,m.g},m.transmissionRoughness});}return result;}
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
size_t CpuScene::nodeCount() const{return state_->nodes.size();}
size_t CpuScene::memoryBytes() const {const auto &s=*state_;size_t n=s.nodes.capacity()*sizeof(State::Node)+s.order.capacity()*4+s.primitives.capacity()*sizeof(State::Primitive)+s.emitterIndex.capacity()*4;for(auto &m:s.meshes)n+=m.vertices.capacity()*sizeof(render::MeshVertex)+m.indices.capacity()*4;return n;}
SceneData CpuScene::exportData() const {
    const auto &s=*state_;SceneData data;data.inverseProjection=s.inverseProjection;data.camera=s.camera;for(int i=0;i<8;++i){data.cameraMedia[i/4][i%4]=s.cameraMedia[i];data.cameraWinding[i/4][i%4]=s.cameraWinding[i];}data.lights=s.lights;data.inverseSquare=s.inverseSquare;data.emitterWeight=s.emitterWeight;
    static_assert(sizeof(PackedVertex)==32 && sizeof(PackedNode)==32 && sizeof(PackedMaterial)==128 && sizeof(PackedEmitter)==32,"GPU scene ABI");
    auto bits=[](uint32_t n){float f;std::memcpy(&f,&n,4);return f;};
    data.nodes.reserve(s.nodes.size());for(const auto &node:s.nodes)data.nodes.push_back({glm::vec4(node.low,bits(node.first)),glm::vec4(node.high,bits(node.count))});
    std::unordered_map<const render::ImageRGBA8*,uint32_t> imageIds;
    auto image=[&](const std::shared_ptr<const render::ImageRGBA8> &value){
        if(!value || value->pixels.empty())return UINT32_MAX;
        auto it=imageIds.find(value.get());if(it!=imageIds.end())return it->second;
        if(data.texels.size()+value->pixels.size()/4>=UINT32_MAX)throw std::length_error("PT: GPU texel addressing overflow");
        const uint32_t id=uint32_t(data.images.size());imageIds[value.get()]=id;data.images.push_back({uint32_t(data.texels.size()),value->width,value->height,0});
        for(size_t i=0;i<value->pixels.size();i+=4)data.texels.push_back(uint32_t(value->pixels[i])|(uint32_t(value->pixels[i+1])<<8)|(uint32_t(value->pixels[i+2])<<16)|(uint32_t(value->pixels[i+3])<<24));return id;
    };
    std::vector<uint32_t> offsets;offsets.reserve(s.meshes.size());
    for(const auto &mesh:s.meshes){
        if(data.vertices.size()+mesh.vertices.size()>=UINT32_MAX)throw std::length_error("PT: GPU vertex addressing overflow");
        offsets.push_back(uint32_t(data.vertices.size()));for(const auto &v:mesh.vertices)data.vertices.push_back({glm::vec4(v.position,v.uv.x),glm::vec4(v.normal,v.uv.y)});
        PackedMaterial material{mesh.parameters.albedoAlpha,mesh.parameters.emissiveNormal,mesh.parameters.factors,glm::vec4(mesh.ior,mesh.kind==3?1:0,mesh.normalScale,mesh.kind==4?1:0),glm::vec4(mesh.absorption,mesh.transmissionRoughness),glm::vec4(mesh.scattering,mesh.g),glm::uvec4(UINT32_MAX),glm::uvec4(UINT32_MAX,mesh.extension.settings.w>0?1:0,mesh.extension.settings.z>0?1:0,0)};
        if(mesh.material){for(int i=0;i<4;++i)material.textures[i]=image(mesh.material->images[i]);material.extra.x=image(mesh.material->images[4]);}data.materials.push_back(material);
    }
    std::vector<uint32_t> remap(s.primitives.size());data.triangles.reserve(s.primitives.size());
    for(uint32_t i=0;i<s.order.size();++i){const uint32_t id=s.order[i];remap[id]=i;const auto &p=s.primitives[id];const auto &m=s.meshes[p.mesh];const auto offset=offsets[p.mesh];data.triangles.push_back({offset+m.indices[p.offset],offset+m.indices[p.offset+1],offset+m.indices[p.offset+2],p.mesh});}
    for(const auto &light:s.emitters)data.emitters.push_back({{bits(remap[light.primitive]),light.area,float(light.cumulative/s.emitterWeight),0},glm::vec4(light.normal,0)});
    return data;
}
void CpuScene::cameraRay(float u,float v,glm::vec3 &o,glm::vec3 &d) const {
    auto far=state_->inverseProjection*glm::vec4(u*2-1,1-v*2,1,1);o=state_->camera;d=unit(glm::vec3(far)/far.w-o,{0,0,-1});
}
EmitterSample CpuScene::sampleEmitter(Random &rng) const {
    const auto &s=*state_;if(s.emitters.empty())return {};
    double selected=rng.uniform()*s.emitterWeight;auto it=std::upper_bound(s.emitters.begin(),s.emitters.end(),selected,[](double value,const auto &light){return value<light.cumulative;});size_t index=std::min(size_t(it-s.emitters.begin()),s.emitters.size()-1);const auto &light=s.emitters[index];
    auto uv=rng.uniform2();float a=std::sqrt(uv[0]);glm::vec2 bary{a*(1-uv[1]),a*uv[1]};auto v=s.vertices(light.primitive);auto p=v[0]->position*(1-a)+v[1]->position*bary.x+v[2]->position*bary.y;Surface hit;s.surface(light.primitive,bary,0,p,-light.normal,hit);hit.normal=hit.geometricNormal;return {hit,emitterPdfArea(light.primitive)};
}
float CpuScene::emitterPdfArea(uint32_t primitive) const {
    const auto &s=*state_;if(primitive>=s.emitterIndex.size())return 0;auto index=s.emitterIndex[primitive];if(index<0)return 0;const auto &light=s.emitters[index];double weight=light.cumulative-(index?s.emitters[index-1].cumulative:0);return float(weight/s.emitterWeight/light.area);
}
float CpuScene::cameraPdf(glm::vec3 direction) const {const auto &s=*state_;float cosine=glm::dot(s.cameraForward,direction);return cosine>0?1/(s.filmArea*cosine*cosine*cosine):0;}
bool CpuScene::project(glm::vec3 point,glm::vec2 &uv,float &pdf) const {
    const auto &s=*state_;auto clip=s.projection*glm::vec4(point,1);if(clip.w<=0)return false;auto ndc=glm::vec3(clip)/clip.w;uv={ndc.x*.5f+.5f,.5f-ndc.y*.5f};pdf=cameraPdf(glm::normalize(point-s.camera));return uv.x>=0&&uv.x<1&&uv.y>=0&&uv.y<1;
}
void CpuScene::validateBidirectional() const {
    for(const auto &mesh:state_->meshes)if(mesh.ior>0&&mesh.transmissionRoughness>=.02f)throw std::invalid_argument("BDPT rough dielectric strategy densities are not implemented; use CPU/GPU unidirectional PT");
    if(scatteringCount())throw std::invalid_argument("BDPT volume/SSS strategy densities are not implemented; use CPU/GPU unidirectional PT");
    if(proceduralCount(3))throw std::invalid_argument("BDPT water-medium strategy densities are not implemented; use CPU/GPU unidirectional PT for oceans");
    const auto &s=*state_;if(environment||sunRadius>0)throw std::invalid_argument("BDPT reference currently supports finite area lights; HDR/sun require a separate endpoint implementation");
    for(const auto &light:s.lights)if(luminance(glm::vec3(light.colorInner))>0)throw std::invalid_argument("BDPT reference requires area emitters instead of local/directional lights");
    if(s.emitters.empty())throw std::invalid_argument("BDPT requires an area emitter");
    for(const auto &mesh:s.meshes){if(mesh.parameters.albedoAlpha.a<1&&mesh.parameters.factors.w==0)throw std::invalid_argument("BDPT does not support blended coverage");if(mesh.material&&mesh.parameters.factors.w==0&&mesh.material->images[0]){const auto &pixels=mesh.material->images[0]->pixels;for(size_t i=3;i<pixels.size();i+=4)if(pixels[i]<255)throw std::invalid_argument("BDPT does not support blended texture coverage");}}
}
bool CpuScene::intersect(glm::vec3 o,glm::vec3 d,float minimum,float maximum,Surface &out,bool brute) const {
    const auto &s=*state_;uint32_t id=UINT32_MAX;glm::vec2 bestBary;float closest=maximum;
    auto test=[&](uint32_t primitive){float t;glm::vec2 bary;if(s.triangle(primitive,o,d,minimum,closest,t,bary)){if(t<closest || id==UINT32_MAX){closest=t;id=primitive;bestBary=bary;}}};
    if(brute)for(uint32_t i=0;i<s.primitives.size();++i)test(i);
    else if(!s.nodes.empty()) {
        std::array<uint32_t,128> stack{};uint32_t size=1;stack[0]=0;
        while(size) {
            const auto &node=s.nodes[stack[--size]];float near;if(!boxHit(node.low,node.high,o,d,minimum,closest,near))continue;
            if(node.count){for(uint32_t i=0;i<node.count;++i)test(s.order[node.first+i]);}
            else {const auto &a=s.nodes[node.first],&b=s.nodes[node.first+1];float na,nb;bool ha=boxHit(a.low,a.high,o,d,minimum,closest,na),hb=boxHit(b.low,b.high,o,d,minimum,closest,nb);
                if(ha && hb){if(na<nb){stack[size++]=node.first+1;stack[size++]=node.first;}else{stack[size++]=node.first;stack[size++]=node.first+1;}}
                else if(ha)stack[size++]=node.first;else if(hb)stack[size++]=node.first+1;
            }
        }
    }
    if(id==UINT32_MAX)return false;s.surface(id,bestBary,closest,o,d,out);return true;
}
glm::vec3 CpuScene::trace(glm::vec3 origin,glm::vec3 direction,Random &rng,uint32_t depth,uint64_t &rays,uint64_t *volumeEvents) const {
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
    auto visibility=[&](glm::vec3 o,glm::vec3 d,float maximum){float transmission=1;Surface hit;
        for(int skip=0;skip<128;++skip){++rays;if(!intersect(o,d,epsilon,maximum,hit))return transmission;transmission*=1-hit.opacity;if(transmission<1e-5f)return 0.f;if(std::isfinite(maximum)){maximum-=hit.distance+epsilon;if(maximum<=epsilon)return transmission;}o=hit.position+d*epsilon;}
        return 0.f;
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
            if((hit.water&&mediumId&&s.meshes[mediumId-1].kind!=3)||(hit.mediumId==mediumId&&mediumId&&(hit.frontFace||winding[mediaCount-1]>1))){transition(hit);if(++transparent>128)break;origin=hit.position+direction*epsilon;continue;}
            rng.dimension(dimension+20+transparent);
            if(hit.opacity<1 && rng.uniform()>=hit.opacity){if(++transparent>128)break;origin=hit.position+direction*epsilon;continue;}
            float emissionWeight=1;const int32_t emitter=s.emitterIndex[hit.primitive];
            if(previousPdf>0 && emitter>=0){const auto &light=s.emitters[emitter];const float cosine=std::abs(glm::dot(light.normal,-direction));const double mass=light.cumulative-(emitter?s.emitters[emitter-1].cumulative:0);const float lightPdf=cosine>0?float(mass/s.emitterWeight)*glm::dot(hit.position-previousPoint,hit.position-previousPoint)/(light.area*cosine):0;emissionWeight=power(previousPdf,lightPdf);}
            radiance+=throughput*hit.emission*emissionWeight;if(bounce>=depth)break;
            hit.exteriorIor=hit.frontFace?(mediumId?s.meshes[mediumId-1].ior:1.f):(mediaCount>1&&mediumId==hit.mediumId?s.meshes[media[mediaCount-2]-1].ior:mediumId!=hit.mediumId&&mediumId?s.meshes[mediumId-1].ior:1.f);
            if(hit.ior>0){float eta=hit.frontFace?hit.exteriorIor/hit.ior:hit.ior/hit.exteriorIor;auto refracted=glm::refract(direction,hit.normal,eta);if(glm::dot(glm::reflect(direction,hit.normal),hit.geometricNormal)<=0||(glm::dot(refracted,refracted)>0&&glm::dot(refracted,hit.geometricNormal)>=0))hit.normal=hit.geometricNormal;}
            if(hit.ior>0&&hit.transmissionRoughness<.02f&&(!hit.water||hit.foam<=0)){rng.dimension(dimension+12);auto sample=sampleBsdf(hit,-direction,rng);if(sample.pdf<=0)break;throughput*=sample.value*(std::abs(glm::dot(hit.normal,sample.direction))/sample.pdf);if(sample.transmission)transition(hit);previousPdf=0;previousPoint=hit.position;origin=offset(hit,sample.direction);direction=sample.direction;++bounce;continue;}
        }
        const auto view=-direction;
        auto direct=[&](glm::vec3 l,glm::vec3 energy,float pdf,float distance,bool delta){
            if(pdf<=0)return;const float cosine=volume?1.f:hit.ior>0?std::abs(glm::dot(hit.normal,l)):std::max(0.f,glm::dot(hit.normal,l));if(cosine==0)return;
            const auto f=volume?glm::vec3(phaseHG(glm::dot(direction,l),medium.g)):evaluateBsdf(hit,view,l);if(luminance(f)<=0)return;
            auto bias=[&](glm::vec3 point){return std::max(epsilon,8*std::numeric_limits<float>::epsilon()*std::max({std::abs(point.x),std::abs(point.y),std::abs(point.z)}));};
            const float maximum=std::isfinite(distance)?std::max(epsilon,distance-2*std::max(bias(hit.position),bias(hit.position+l*distance))):distance;
            const float transmission=visibility(volume?hit.position+l*bias(hit.position):offset(hit,l),l,maximum);
            const float weight=delta?1:power(pdf,volume?phaseHG(glm::dot(direction,l),medium.g):bsdfPdf(hit,view,l));
            uint32_t shadowMedium=mediumId;if(!volume&&hit.ior>0&&glm::dot(l,hit.geometricNormal)<0)shadowMedium=hit.frontFace?hit.mediumId:(mediaCount>1?media[mediaCount-2]:0);
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
            auto vertices=s.vertices(light.primitive);const auto uv=rng.uniform2();const float a=std::sqrt(uv[0]),b=uv[1];const glm::vec2 bary{a*(1-b),a*b};auto p=vertices[0]->position*(1-a)+vertices[1]->position*bary.x+vertices[2]->position*bary.y;
            auto to=p-hit.position;float distance=glm::length(to);auto l=unit(to);
            Surface emitterSurface;s.surface(light.primitive,bary,distance,hit.position,l,emitterSurface);
            const float cosine=std::abs(glm::dot(light.normal,-l));if(cosine>1e-8f && distance>2*epsilon){const double mass=light.cumulative-(index?s.emitters[index-1].cumulative:0);direct(l,emitterSurface.emission*emitterSurface.opacity,float(mass/s.emitterWeight)*distance*distance/(light.area*cosine),distance,false);}
        }
        if(volume){rng.dimension(dimension+36);auto l=samplePhaseHG(direction,medium.g,rng);previousPdf=phaseHG(glm::dot(direction,l),medium.g);previousPoint=hit.position;origin=hit.position;direction=l;++bounce;rng.dimension(dimension+16);if(bounce>=3){float survive=glm::clamp(std::max({throughput.r,throughput.g,throughput.b}),.05f,.95f);if(rng.uniform()>=survive)break;throughput/=survive;}continue;}
        rng.dimension(dimension+12);
        auto sample=sampleBsdf(hit,view,rng);if(sample.pdf<=1e-20f || luminance(sample.value)<=0)break;
        if(hit.ior>0){throughput*=sample.value*(std::abs(glm::dot(hit.normal,sample.direction))/sample.pdf);if(sample.transmission)transition(hit);previousPdf=sample.delta?0:sample.pdf;previousPoint=hit.position;origin=offset(hit,sample.direction);direction=sample.direction;++bounce;continue;}
        throughput*=sample.value*(std::max(0.f,glm::dot(hit.normal,sample.direction))/sample.pdf);
        if(!finite(throughput))return glm::vec3(std::numeric_limits<float>::quiet_NaN());
        previousPdf=sample.pdf;previousPoint=hit.position;origin=offset(hit,sample.direction);direction=sample.direction;++bounce;
        rng.dimension(dimension+16);
        if(bounce>=3){const float survive=glm::clamp(std::max({throughput.r,throughput.g,throughput.b}),.05f,.95f);if(rng.uniform()>=survive)break;throughput/=survive;}
    }
    return radiance;
}
void validateOptions(const Options &options) {
    if(!options.width || !options.height || options.width>16384 || options.height>16384 || !options.samples || options.samples>1048576 || !options.maxDepth || options.maxDepth>128 || options.threads>256 || !std::isfinite(options.exposure) || options.exposure<=0 || !options.minimumSamples || !std::isfinite(options.relativeError) || options.relativeError<=0 || !std::isfinite(options.absoluteError) || options.absoluteError<0)throw std::invalid_argument("PT: invalid render options");
    if(!options.trainingSamples || options.trainingSamples>512 || !options.cacheMinimum || options.cacheMinimum>8192 || !options.cacheDepth || options.cacheDepth>128 || !std::isfinite(options.guideCellSize) || options.guideCellSize<0)throw std::invalid_argument("PT: invalid learning options");
    if(options.bdpt&&(options.adaptive||options.guiding||options.radianceCache||options.maxDepth>32))throw std::invalid_argument("BDPT requires fixed spp, depth <=32, without guiding/cache");
    if((options.guiding||options.radianceCache)&&uint64_t(options.width)*options.height*options.trainingSamples>268435456)throw std::invalid_argument("PT: training path budget exceeds atomic counter limit");
    if(uint64_t(options.width)*options.height>67108864)throw std::invalid_argument("PT: image exceeds CPU allocation limit");
}
Image render(const CpuScene &scene,const Options &options,const std::function<void(const Image &)> &progress) {
    if(options.bdpt)return renderBdpt(scene,options,progress);
    if(options.guiding||options.radianceCache)throw std::invalid_argument("Guiding/cache require GPU PT");
    validateOptions(options);const size_t pixels=size_t(options.width)*options.height;
    Image image;image.width=options.width;image.height=options.height;image.radiance.resize(pixels,glm::vec3(0));image.albedo.resize(pixels,glm::vec3(0));image.normal.resize(pixels,glm::vec3(0));image.sampleCounts.resize(pixels,0);
    std::vector<glm::vec3> m2(pixels,glm::vec3(0));std::vector<uint8_t> stable(pixels,0);
    const uint32_t tilesX=(options.width+15)/16,tilesY=(options.height+15)/16;
    const uint32_t workers=std::min(tilesX*tilesY,options.threads?options.threads:std::max(1u,std::thread::hardware_concurrency()>1?std::thread::hardware_concurrency()-1:1));
    const auto started=std::chrono::steady_clock::now();
    for(uint32_t first=0;first<options.samples;) {
        const uint32_t end=std::min(options.samples,first<4?4u:first<16?16u:first+32);
        std::atomic<uint32_t> next{0};std::atomic<uint64_t> rays{0},invalid{0},volumeEvents{0};std::vector<std::thread> threads;std::exception_ptr failure;std::mutex failureMutex;
        auto job=[&]{try {uint64_t localRays=0,localInvalid=0,localVolumeEvents=0;
            while(true){const uint32_t tile=next.fetch_add(1);if(tile>=tilesX*tilesY)break;const uint32_t x0=tile%tilesX*16,y0=tile/tilesX*16;
                for(uint32_t y=y0;y<std::min(y0+16,options.height);++y)for(uint32_t x=x0;x<std::min(x0+16,options.width);++x){const size_t pixel=size_t(y)*options.width+x;
                    if(stable[pixel]>=2)continue;
                    if(first==0){glm::vec3 o,d;scene.cameraRay((x+.5f)/options.width,(y+.5f)/options.height,o,d);Surface h;++localRays;if(scene.intersect(o,d,1e-4f,std::numeric_limits<float>::infinity(),h)){image.albedo[pixel]=h.albedo;image.normal[pixel]=h.normal;}}
                    for(uint32_t sample=first;sample<end;++sample){auto random=Random::forPixel(options.seed,uint32_t(pixel),sample,options.sobol);glm::vec3 o,d;const float u=(x+random.uniform())/options.width,v=(y+random.uniform())/options.height;scene.cameraRay(u,v,o,d);auto value=scene.trace(o,d,random,options.maxDepth,localRays,&localVolumeEvents);if(!finite(value)){++localInvalid;continue;}const uint32_t n=++image.sampleCounts[pixel];auto delta=value-image.radiance[pixel];image.radiance[pixel]+=delta/float(n);m2[pixel]+=delta*(value-image.radiance[pixel]);}
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
        if(progress)progress(image);first=end;if(image.convergedPixels==pixels)break;
    }
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
    data["volume_scattering_events"]=image.volumeEvents;data["scattering_meshes"]=scene.scatteringCount();data["subsurface_meshes"]=scene.proceduralCount(4);data["dielectric_meshes"]=scene.dielectricCount();data["terrain_meshes"]=scene.proceduralCount(1);data["grass_meshes"]=scene.proceduralCount(2);data["ocean_interfaces"]=scene.proceduralCount(3);data["frozen_time_seconds"]=scene.capturedTime();
    data["media"]=nlohmann::json::array();for(const auto &m:scene.media())data["media"].push_back({{"id",m.id},{"kind",m.kind},{"ior",m.ior},{"dielectric_roughness",m.roughness},{"sigma_a",{m.volume.absorption.x,m.volume.absorption.y,m.volume.absorption.z}},{"sigma_s",{m.volume.scattering.x,m.volume.scattering.y,m.volume.scattering.z}},{"hg_g",m.volume.g}});
    data["guiding"]=options.guiding;data["radiance_cache"]=options.radianceCache;data["bdpt"]=options.bdpt;data["training_spp"]=options.guiding||options.radianceCache?options.trainingSamples:0;data["training_seconds"]=image.trainingSeconds;data["training_rays"]=image.trainingRays;data["guide_hits"]=image.guideHits;data["cache_hits"]=image.cacheHits;data["trained_cells"]=image.trainedCells;data["guide_cell_size"]=image.guideCellSize;data["trace_and_training_seconds"]=image.seconds+image.trainingSeconds;data["cache_minimum"]=options.cacheMinimum;data["cache_depth"]=options.cacheDepth;
    data["setup_seconds"]=image.setupSeconds;data["gpu_buffer_bytes"]=image.gpuBufferBytes;
    data["denoiser"]=image.denoiser;data["denoise_device"]=image.denoiseDevice;data["denoise_seconds"]=image.denoiseSeconds;data["denoise_auxiliary"]=image.denoiseAuxiliary;
    data["trace_training_denoise_seconds"]=image.seconds+image.trainingSeconds+image.denoiseSeconds;
    if(!image.caustics.empty()){double caustic=0,total=0;for(auto value:image.caustics)caustic+=luminance(value);for(auto value:image.radiance)total+=luminance(value);data["caustic_mean_luminance"]=caustic/image.caustics.size();data["caustic_energy_fraction"]=total>0?caustic/total:0;}
    data["execution"]=image.execution;data["sampler"]=options.sobol?"dyadic-permuted padded 2D Sobol":"PCG";data["adaptive"]=options.adaptive;data["sample_budget"]=options.samples;data["total_samples"]=image.totalSamples;data["average_spp"]=image.sampleCounts.empty()?double(image.samples):double(image.totalSamples)/image.sampleCounts.size();data["converged_pixels"]=image.convergedPixels;data["minimum_samples"]=options.minimumSamples;data["relative_error"]=options.relativeError;data["absolute_error"]=options.absoluteError;
    data["sun_radius_degrees"]=glm::degrees(scene.sunRadius);data["sun_irradiance"]={scene.sunIrradiance.x,scene.sunIrradiance.y,scene.sunIrradiance.z};
    if(scene.environment)data["environment"]={{"width",scene.environment->width()},{"height",scene.environment->height()},{"type","linear HDR equirectangular, importance sampled"}};
    std::ofstream file(prefix+".json");file<<data.dump(2)<<'\n';if(!file)throw std::runtime_error("PT: cannot write render report");
}
} // namespace pt
