#include "PT/CpuPathTracer.h"
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
Random::Random(uint64_t seed):state(seed+0x9e3779b97f4a7c15ull) { bits(); }
uint32_t Random::bits() {
    const uint64_t previous=state;state=previous*6364136223846793005ull+1442695040888963407ull;
    const uint32_t x=uint32_t(((previous>>18)^previous)>>27),rotation=uint32_t(previous>>59);
    return (x>>rotation)|(x<<((0-rotation)&31));
}
float Random::uniform() { return float(bits()>>8)*0x1p-24f; }
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
    const float phi=(x+r.uniform())/width_*2*pi;
    const float cosine=glm::mix(std::cos(pi*y/height_),std::cos(pi*(y+1)/height_),r.uniform());
    const float sine=std::sqrt(std::max(0.f,1-cosine*cosine));glm::vec3 d{sine*std::sin(phi),cosine,-sine*std::cos(phi)};
    return {d,evaluate(d),float((cdf_[i]-(i?cdf_[i-1]:0))/(total_*solidAngle(y,width_,height_)))};
}
glm::vec3 evaluateBsdf(const Surface &s,glm::vec3 v,glm::vec3 l) {
    const float nv=glm::dot(s.normal,v),nl=glm::dot(s.normal,l);
    if(nv<=0 || nl<=0 || glm::dot(s.geometricNormal,v)<=0 || glm::dot(s.geometricNormal,l)<=0)return glm::vec3(0);
    auto h=unit(v+l,s.normal);float nh=std::max(0.f,glm::dot(s.normal,h)),vh=std::max(0.f,glm::dot(v,h));
    auto f0=glm::mix(glm::vec3(.04f),s.albedo,s.metallic);auto f=f0+(1.f-f0)*std::pow(1-vh,5.f);
    return (1.f-f)*s.albedo*(1-s.metallic)/pi+f*(distribution(nh,s.roughness)*smith(nv,s.roughness)*smith(nl,s.roughness)/(4*nv*nl));
}
float bsdfPdf(const Surface &s,glm::vec3 v,glm::vec3 l) {
    const float nl=glm::dot(s.normal,l);if(nl<=0 || glm::dot(s.normal,v)<=0)return 0;
    auto h=unit(v+l,s.normal);const float vh=glm::dot(v,h);if(vh<=0)return 0;
    const float p=specularChance(s);
    return (1-p)*nl/pi+p*distribution(std::max(0.f,glm::dot(s.normal,h)),s.roughness)*std::max(0.f,glm::dot(s.normal,h))/(4*vh);
}
BsdfSample sampleBsdf(const Surface &s,glm::vec3 v,Random &r) {
    glm::vec3 l;
    if(r.uniform()<specularChance(s)) {
        const float a=s.roughness*s.roughness,q=r.uniform();
        const float cosine=std::sqrt((1-q)/(1+(a*a-1)*q)),sine=std::sqrt(std::max(0.f,1-cosine*cosine)),phi=2*pi*r.uniform();
        const auto half=local({sine*std::cos(phi),sine*std::sin(phi),cosine},s.normal);l=glm::reflect(-v,half);
        // Rejected NDF samples contribute zero; retrying would bias the PDF.
        if(glm::dot(v,half)<=0 || glm::dot(l,s.normal)<=0)return {};
    }else {
        const float q=r.uniform(),phi=2*pi*r.uniform();l=local({std::sqrt(q)*std::cos(phi),std::sqrt(q)*std::sin(phi),std::sqrt(1-q)},s.normal);
    }
    return {l,evaluateBsdf(s,v,l),bsdfPdf(s,v,l)};
}
struct CpuScene::State {
    struct Mesh {std::vector<render::MeshVertex> vertices;std::vector<uint32_t> indices;std::shared_ptr<const render::MaterialPayload> material;render::MaterialParameters parameters;render::MaterialExtension extension;};
    struct Primitive {uint32_t mesh,offset;};
    struct Node {glm::vec3 low;uint32_t first;glm::vec3 high;uint32_t count;};
    struct AreaLight {uint32_t primitive;float area;double cumulative;glm::vec3 normal;};
    std::vector<Mesh> meshes;std::vector<Primitive> primitives;std::vector<uint32_t> order;std::vector<Node> nodes;
    std::vector<AreaLight> emitters;std::vector<int32_t> emitterIndex;double emitterWeight=0;
    std::vector<render::LightData> lights;glm::mat4 inverseProjection;glm::vec3 camera;
    bool inverseSquare=true;
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
        out=Surface{};out.distance=t;out.primitive=id;out.position=o+d*t;
        out.uv=v[0]->uv*(1-bary.x-bary.y)+v[1]->uv*bary.x+v[2]->uv*bary.y;
        const auto e1=v[1]->position-v[0]->position,e2=v[2]->position-v[0]->position;
        auto n=unit(v[0]->normal*(1-bary.x-bary.y)+v[1]->normal*bary.x+v[2]->normal*bary.y);
        auto gn=unit(glm::cross(e1,e2),n);if(glm::dot(gn,n)<0)gn=-gn;
        out.frontFace=glm::dot(gn,d)<0;if(!out.frontFace){gn=-gn;n=-n;}
        out.geometricNormal=gn;out.normal=n;
        if(image(1) && m.parameters.emissiveNormal.w>0) {
            auto a=v[1]->uv-v[0]->uv,b=v[2]->uv-v[0]->uv;const float determinant=a.x*b.y-a.y*b.x;
            if(std::abs(determinant)>1e-10f) {
                auto tangent=(e1*b.y-e2*a.y)/determinant,bitangent=(-e1*b.x+e2*a.x)/determinant;
                tangent=unit(tangent-n*glm::dot(n,tangent));bitangent=unit(bitangent-n*glm::dot(n,bitangent));
                auto map=glm::vec3(texel(image(1),out.uv))*2.f-1.f;map.x*=m.parameters.emissiveNormal.w;map.y*=m.parameters.emissiveNormal.w;
                const auto mapped=unit(tangent*map.x+bitangent*map.y+n*map.z,n);
                if(glm::dot(mapped,gn)>.05f)out.normal=mapped;
            }
        }
        const auto base=texel(image(0),out.uv)*m.parameters.albedoAlpha;
        out.albedo=glm::pow(glm::max(glm::vec3(base),glm::vec3(0)),glm::vec3(2.2f));
        out.metallic=glm::clamp(texel(image(2),out.uv).b*m.parameters.factors.x,0.f,1.f);
        out.roughness=glm::clamp(texel(image(3),out.uv).g*m.parameters.factors.y,.045f,1.f);
        out.opacity=m.parameters.factors.w>0?(base.a<m.parameters.factors.w?0.f:1.f):glm::clamp(base.a,0.f,1.f);
        out.emission=(out.frontFace || m.extension.settings.w>0)?glm::vec3(m.parameters.emissiveNormal):glm::vec3(0);
        if(m.extension.settings.z>0 && luminance(out.emission)==0)out.emission=out.albedo;
    }
};
CpuScene::CpuScene(const render::RenderWorldSnapshot &snapshot):state_(std::make_unique<State>()) {
    auto &s=*state_;s.camera=snapshot.frame.cameraPosition;s.inverseProjection=glm::inverse(snapshot.frame.viewProjection);s.lights=snapshot.frame.lights;s.inverseSquare=snapshot.frame.inverseSquareLocalLights;
    if(!snapshot.frame.directionalEnabled)for(auto &light:s.lights)if(light.positionType.w==0)light.colorInner=glm::vec4(0);
    if(snapshot.terrain || !snapshot.frame.oceans.empty())std::cerr<<"CPU PT traces mesh objects; procedural terrain/ocean are excluded from this capture\n";
    size_t count=0;for(const auto &draw:snapshot.draws)if(draw.mesh)count+=draw.mesh->indices.size()/3;
    if(count>=UINT32_MAX)throw std::length_error("PT: too many triangles");s.primitives.reserve(count);
    for(const auto &draw:snapshot.draws) {
        if(!draw.mesh || draw.mesh->indices.empty())continue;
        if(draw.mesh->indices.size()>UINT32_MAX || draw.mesh->indices.size()%3)throw std::invalid_argument("PT: non-triangle mesh indices");
        const float determinant=glm::determinant(glm::mat3(draw.model));if(!std::isfinite(determinant) || std::abs(determinant)<1e-15f)throw std::invalid_argument("PT: singular mesh transform");
        if(draw.material)for(const auto &image:draw.material->images)if(image && !image->pixels.empty() && (!image->width || !image->height || image->width>INT32_MAX || image->height>INT32_MAX || uint64_t(image->width)*image->height*4!=image->pixels.size()))throw std::invalid_argument("PT: invalid material image extent");
        State::Mesh mesh;mesh.vertices=draw.mesh->vertices;mesh.indices=draw.mesh->indices;mesh.material=draw.material;mesh.parameters=draw.parameters;mesh.extension=draw.extension;
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
    std::cout<<"CPU PT: "<<s.meshes.size()<<" meshes, "<<s.primitives.size()<<" triangles, "<<s.nodes.size()<<" BVH nodes; "<<memoryBytes()/1048576.0<<" MiB geometry/BVH; build "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<" s\n"<<std::flush;
}
CpuScene::~CpuScene()=default;
size_t CpuScene::triangles() const{return state_->primitives.size();}
size_t CpuScene::meshCount() const{return state_->meshes.size();}
size_t CpuScene::nodeCount() const{return state_->nodes.size();}
size_t CpuScene::memoryBytes() const {const auto &s=*state_;size_t n=s.nodes.capacity()*sizeof(State::Node)+s.order.capacity()*4+s.primitives.capacity()*sizeof(State::Primitive)+s.emitterIndex.capacity()*4;for(auto &m:s.meshes)n+=m.vertices.capacity()*sizeof(render::MeshVertex)+m.indices.capacity()*4;return n;}
void CpuScene::cameraRay(float u,float v,glm::vec3 &o,glm::vec3 &d) const {
    auto far=state_->inverseProjection*glm::vec4(u*2-1,1-v*2,1,1);o=state_->camera;d=unit(glm::vec3(far)/far.w-o,{0,0,-1});
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
glm::vec3 CpuScene::trace(glm::vec3 origin,glm::vec3 direction,Random &rng,uint32_t depth,uint64_t &rays) const {
    const auto &s=*state_;glm::vec3 throughput(1),radiance(0),previousPoint(0);float previousPdf=0;
    const float epsilon=1e-4f;const float sunCos=std::cos(sunRadius),sunPdf=sunRadius>0?1/(2*pi*(1-sunCos)):0;
    auto offset=[&](const Surface &hit,glm::vec3 d){return hit.position+hit.geometricNormal*(glm::dot(d,hit.geometricNormal)>=0?epsilon:-epsilon);};
    auto visibility=[&](glm::vec3 o,glm::vec3 d,float maximum){float transmission=1;Surface hit;
        for(int skip=0;skip<128;++skip){++rays;if(!intersect(o,d,epsilon,maximum,hit))return transmission;transmission*=1-hit.opacity;if(transmission<1e-5f)return 0.f;if(std::isfinite(maximum)){maximum-=hit.distance+epsilon;if(maximum<=epsilon)return transmission;}o=hit.position+d*epsilon;}
        return 0.f;
    };
    for(uint32_t bounce=0,transparent=0;bounce<depth;) {
        Surface hit;++rays;
        if(!intersect(origin,direction,epsilon,std::numeric_limits<float>::infinity(),hit)) {
            if(environment){auto e=environment->evaluate(direction);const float weight=previousPdf>0?power(previousPdf,environment->pdf(direction)):1;radiance+=throughput*e*weight;}
            if(sunRadius>0 && glm::dot(direction,sunDirection)>=sunCos){const float weight=previousPdf>0?power(previousPdf,sunPdf):1;radiance+=throughput*sunIrradiance/(pi*std::sin(sunRadius)*std::sin(sunRadius))*weight;}
            break;
        }
        if(hit.opacity<1 && rng.uniform()>=hit.opacity){if(++transparent>128)break;origin=hit.position+direction*epsilon;continue;}
        float emissionWeight=1;
        const int32_t emitter=s.emitterIndex[hit.primitive];
        if(previousPdf>0 && emitter>=0) {
            const auto &light=s.emitters[emitter];const float cosine=std::abs(glm::dot(light.normal,-direction));
            const double mass=light.cumulative-(emitter?s.emitters[emitter-1].cumulative:0);
            const float lightPdf=cosine>0?float(mass/s.emitterWeight)*glm::dot(hit.position-previousPoint,hit.position-previousPoint)/(light.area*cosine):0;
            emissionWeight=power(previousPdf,lightPdf);
        }
        radiance+=throughput*hit.emission*emissionWeight;
        const auto view=-direction;
        auto direct=[&](glm::vec3 l,glm::vec3 energy,float pdf,float distance,bool delta){
            if(pdf<=0)return;const float cosine=std::max(0.f,glm::dot(hit.normal,l));if(cosine==0)return;
            const auto f=evaluateBsdf(hit,view,l);if(luminance(f)<=0)return;
            const float transmission=visibility(offset(hit,l),l,distance);
            const float weight=delta?1:power(pdf,bsdfPdf(hit,view,l));radiance+=throughput*f*energy*(cosine*transmission*weight/pdf);
        };
        if(environment){auto e=environment->sample(rng);direct(e.direction,e.radiance,e.pdf,std::numeric_limits<float>::infinity(),false);}
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
                direct(l,color,1,std::max(epsilon,distance-2*epsilon),true);
            }
        }
        if(sunRadius>0 && !atmosphericSun){float cosine=glm::mix(1.f,sunCos,rng.uniform()),sine=std::sqrt(std::max(0.f,1-cosine*cosine)),phi=2*pi*rng.uniform();direct(local({sine*std::cos(phi),sine*std::sin(phi),cosine},sunDirection),sunIrradiance/(pi*std::sin(sunRadius)*std::sin(sunRadius)),sunPdf,std::numeric_limits<float>::infinity(),false);}
        if(!s.emitters.empty()) {
            const double selected=double(rng.uniform())*s.emitterWeight;const auto it=std::upper_bound(s.emitters.begin(),s.emitters.end(),selected,[](double value,const auto &light){return value<light.cumulative;});const size_t index=std::min(size_t(it-s.emitters.begin()),s.emitters.size()-1);const auto &light=s.emitters[index];
            auto vertices=s.vertices(light.primitive);const float a=std::sqrt(rng.uniform()),b=rng.uniform();const glm::vec2 bary{a*(1-b),a*b};auto p=vertices[0]->position*(1-a)+vertices[1]->position*bary.x+vertices[2]->position*bary.y;
            auto to=p-hit.position;float distance=glm::length(to);auto l=unit(to);
            Surface emitterSurface;s.surface(light.primitive,bary,distance,hit.position,l,emitterSurface);
            const float cosine=std::abs(glm::dot(light.normal,-l));if(cosine>1e-8f && distance>2*epsilon){const double mass=light.cumulative-(index?s.emitters[index-1].cumulative:0);direct(l,emitterSurface.emission*emitterSurface.opacity,float(mass/s.emitterWeight)*distance*distance/(light.area*cosine),distance-2*epsilon,false);}
        }
        auto sample=sampleBsdf(hit,view,rng);if(sample.pdf<=1e-20f || luminance(sample.value)<=0)break;
        throughput*=sample.value*(std::max(0.f,glm::dot(hit.normal,sample.direction))/sample.pdf);
        if(!finite(throughput))return glm::vec3(std::numeric_limits<float>::quiet_NaN());
        previousPdf=sample.pdf;previousPoint=hit.position;origin=offset(hit,sample.direction);direction=sample.direction;++bounce;
        if(bounce>=3){const float survive=glm::clamp(std::max({throughput.r,throughput.g,throughput.b}),.05f,.95f);if(rng.uniform()>=survive)break;throughput/=survive;}
    }
    return radiance;
}
Image render(const CpuScene &scene,const Options &options,const std::function<void(const Image &)> &progress) {
    if(!options.width || !options.height || options.width>16384 || options.height>16384 || !options.samples || options.samples>1048576 || !options.maxDepth || options.maxDepth>128 || options.threads>256 || !std::isfinite(options.exposure) || options.exposure<=0)throw std::invalid_argument("PT: invalid render options");
    const uint64_t pixels=uint64_t(options.width)*options.height;if(pixels>67108864)throw std::invalid_argument("PT: image exceeds CPU allocation limit");
    Image image;image.width=options.width;image.height=options.height;image.radiance.resize(pixels,glm::vec3(0));image.albedo.resize(pixels,glm::vec3(0));image.normal.resize(pixels,glm::vec3(0));
    std::vector<glm::vec3> accumulation(pixels,glm::vec3(0));
    const uint32_t tilesX=(options.width+15)/16,tilesY=(options.height+15)/16;
    const uint32_t workers=std::min(tilesX*tilesY,options.threads?options.threads:std::max(1u,std::thread::hardware_concurrency()>1?std::thread::hardware_concurrency()-1:1));
    const auto started=std::chrono::steady_clock::now();
    for(uint32_t first=0;first<options.samples;) {
        const uint32_t end=std::min(options.samples,first<4?4u:first<16?16u:first+32);
        std::atomic<uint32_t> next{0};std::atomic<uint64_t> rays{0},invalid{0};std::vector<std::thread> threads;std::exception_ptr failure;std::mutex failureMutex;
        auto job=[&]{try {uint64_t localRays=0,localInvalid=0;
            while(true){const uint32_t tile=next.fetch_add(1);if(tile>=tilesX*tilesY)break;const uint32_t x0=tile%tilesX*16,y0=tile/tilesX*16;
                for(uint32_t y=y0;y<std::min(y0+16,options.height);++y)for(uint32_t x=x0;x<std::min(x0+16,options.width);++x){const size_t pixel=size_t(y)*options.width+x;
                    if(first==0){glm::vec3 o,d;scene.cameraRay((x+.5f)/options.width,(y+.5f)/options.height,o,d);Surface h;++localRays;if(scene.intersect(o,d,1e-4f,std::numeric_limits<float>::infinity(),h)){image.albedo[pixel]=h.albedo;image.normal[pixel]=h.normal;}}
                    for(uint32_t sample=first;sample<end;++sample){Random random(options.seed^(uint64_t(pixel)*0xd1b54a32d192ed03ull)^(uint64_t(sample)*0x94d049bb133111ebull));glm::vec3 o,d;const float u=(x+random.uniform())/options.width,v=(y+random.uniform())/options.height;scene.cameraRay(u,v,o,d);auto value=scene.trace(o,d,random,options.maxDepth,localRays);if(!finite(value)){++localInvalid;continue;}accumulation[pixel]+=value;}
                    image.radiance[pixel]=accumulation[pixel]/float(end);
                }
            }rays+=localRays;invalid+=localInvalid;
        }catch(...){std::lock_guard<std::mutex> lock(failureMutex);if(!failure)failure=std::current_exception();next=tilesX*tilesY;}};
        try{for(uint32_t i=0;i<workers;++i)threads.emplace_back(job);}catch(...){next=tilesX*tilesY;for(auto &thread:threads)thread.join();throw;}
        for(auto &thread:threads)thread.join();if(failure)std::rethrow_exception(failure);
        image.rays+=rays.load();image.nonFiniteSamples+=invalid.load();image.samples=end;image.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        if(image.nonFiniteSamples)throw std::runtime_error("PT: non-finite path contribution");
        std::cout<<"CPU PT "<<end<<" spp, "<<image.seconds<<" s, "<<image.rays<<" rays, "<<workers<<" workers\n"<<std::flush;
        if(progress)progress(image);first=end;
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
    png(image.albedo,image.width,image.height,exposure,prefix+"-albedo.png",false);
    png(image.normal,image.width,image.height,exposure,prefix+"-normal.png",false,true);
    std::ofstream pfm(prefix+".pfm",std::ios::binary);const uint16_t endian=1;const bool little=*reinterpret_cast<const uint8_t*>(&endian)==1;
    pfm<<"PF\n"<<image.width<<" "<<image.height<<"\n"<<(little?"-1.0":"1.0")<<"\n";
    for(uint32_t y=image.height;y>0;--y)for(uint32_t x=0;x<image.width;++x){const auto value=image.radiance[size_t(y-1)*image.width+x];const float rgb[3]={value.r,value.g,value.b};pfm.write(reinterpret_cast<const char*>(rgb),sizeof(rgb));}
    if(!pfm)throw std::runtime_error("PT: cannot write linear HDR PFM");
}
void writeReport(const Image &image,const CpuScene &scene,const Options &options,const std::string &prefix,const std::string &name) {
    nlohmann::json data={{"scene",name},{"width",image.width},{"height",image.height},{"samples",image.samples},{"max_depth",options.maxDepth},{"seed",options.seed},{"requested_threads",options.threads},{"exposure",options.exposure},{"triangles",scene.triangles()},{"meshes",scene.meshCount()},{"bvh_nodes",scene.nodeCount()},{"geometry_bvh_bytes",scene.memoryBytes()},{"render_seconds",image.seconds},{"rays",image.rays},{"non_finite_samples",image.nonFiniteSamples},{"integrator","Lambert + GGX, environment/sun/emitter NEE, power MIS, Russian roulette"},{"terrain_ocean","not captured"}};
    if(scene.environment)data["environment"]={{"width",scene.environment->width()},{"height",scene.environment->height()},{"type","linear HDR equirectangular, importance sampled"}};
    std::ofstream file(prefix+".json");file<<data.dump(2)<<'\n';if(!file)throw std::runtime_error("PT: cannot write render report");
}
} // namespace pt
