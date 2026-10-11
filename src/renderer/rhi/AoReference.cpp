#include "renderer/rhi/AoReference.h"
#include "PT/CpuPathTracer.h"
#include "engine/JobSystem.h"
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
namespace render {
namespace {
float falloff(float distance2,float radius) {
    const float t=glm::clamp((distance2/(radius*radius)-.36f)/.64f,0.f,1.f);
    return 1-t*t*(3-2*t);
}
float epsilon(glm::vec3 p,float radius) {
    return std::max(1e-5f*std::max(radius,1.f),4*std::numeric_limits<float>::epsilon()*std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z),1.f}));
}
struct Counts {uint64_t rays=0,saturated=0;size_t covered=0,matched=0;};
bool matchSurface(const pt::CpuScene& scene,glm::vec3 camera,glm::vec3 p,float eps,pt::Surface& surface,Counts& counts) {
    const auto delta=p-camera;const float distance=glm::length(delta);
    if(distance<=eps)return false;
    const auto direction=delta/distance;float minimum=eps;
    const float tolerance=std::max(8*eps,distance*1e-5f);
    for(int skip=0;skip<128;++skip) {
        ++counts.rays;
        if(!scene.intersect(camera,direction,minimum,distance+tolerance,surface))return false;
        if(surface.opacity>0)return glm::length(surface.position-p)<=tolerance;
        minimum=surface.distance+eps;
    }
    ++counts.saturated;return false;
}
float occlusion(const pt::CpuScene& scene,glm::vec3 p,glm::vec3 normal,glm::vec3 direction,
                const AoReferenceOptions& options,float eps,Counts& counts) {
    float minimum=eps,transmission=1,blocked=0;
    for(int skip=0;skip<128;++skip) {
        pt::Surface hit;++counts.rays;
        if(!scene.intersect(p,direction,minimum,options.radius,hit))return blocked;
        // Match the shader's geometric normal bias and world-space falloff.
        if(glm::dot(hit.position-p,normal)>options.bias) {
            blocked+=transmission*hit.opacity*falloff(hit.distance*hit.distance,options.radius);
            transmission*=1-hit.opacity;if(transmission<1e-6f)return blocked;
        }
        minimum=hit.distance+eps;if(minimum>=options.radius)return blocked;
    }
    ++counts.saturated;return 1; // Conservative result; reports expose exhaustion.
}
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
AoReferenceImage traceAoReference(const pt::CpuScene& scene,const FrameData& frame,
                                  const std::vector<float>& positions,const AoReferenceOptions& options) {
    if(!options.width||!options.height||options.width>8192||options.height>8192||!options.samples||options.samples>65536||
       !options.threads||options.threads>32||positions.size()!=size_t(options.width)*options.height*4||
       !std::isfinite(options.radius)||options.radius<0||options.radius>20||!std::isfinite(options.bias)||options.bias<0||options.bias>5||
       !std::isfinite(options.power)||options.power<=0||options.power>4)
        throw std::invalid_argument("AO reference: invalid dimensions, samples or parameters");
    for(float value:positions)if(!std::isfinite(value))throw std::invalid_argument("AO reference: nonfinite position");
    for(int axis=0;axis<3;++axis)if(!std::isfinite(frame.cameraPosition[axis]))throw std::invalid_argument("AO reference: nonfinite camera");
    const auto start=std::chrono::steady_clock::now();AoReferenceImage result;
    const size_t pixels=size_t(options.width)*options.height;result.linear.assign(pixels,1);result.visibility.assign(pixels,1);result.matched.assign(pixels,0);
    engine::JobSystem jobs(options.threads);std::vector<std::future<Counts>> futures;
    for(uint32_t row=0;row<options.height;row+=8)futures.push_back(jobs.submit([&,row]{
        Counts counts;
        for(uint32_t y=row;y<std::min(row+8,options.height);++y)for(uint32_t x=0;x<options.width;++x) {
            const size_t pixel=size_t(y)*options.width+x,index=pixel*4;if(positions[index+3]<=0)continue;
            ++counts.covered;glm::vec3 p(positions[index],positions[index+1],positions[index+2]);
            const float eps=epsilon(p,options.radius);pt::Surface surface;
            if(!matchSurface(scene,frame.cameraPosition,p,eps,surface,counts))continue;
            result.matched[pixel]=1;++counts.matched;if(options.radius==0)continue;
            const glm::vec3 N=surface.geometricNormal,T=glm::normalize(std::abs(N.z)<.99f?glm::cross(N,glm::vec3(0,0,1)):glm::cross(N,glm::vec3(0,1,0))),B=glm::cross(N,T);
            double blocked=0;
            for(uint32_t sample=0;sample<options.samples;++sample) {
                auto random=pt::Random::forPixel(options.seed,uint32_t(pixel),sample,true);auto u=random.uniform2();
                const float r=std::sqrt(u[0]),phi=6.28318530718f*u[1];
                const auto direction=T*(r*std::cos(phi))+B*(r*std::sin(phi))+N*std::sqrt(std::max(0.f,1-u[0]));
                blocked+=occlusion(scene,p,N,direction,options,eps,counts);
            }
            result.linear[pixel]=glm::clamp(1-float(blocked/options.samples),0.f,1.f);
            result.visibility[pixel]=std::pow(result.linear[pixel],options.power);
        }
        return counts;
    }));
    for(auto& future:futures){auto count=future.get();result.rays+=count.rays;result.saturatedRays+=count.saturated;result.covered+=count.covered;result.matchedPixels+=count.matched;}
    jobs.shutdown();result.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();return result;
}
void validateAoReference() {
    auto quad=[](float z,bool hole){
        SnapshotDraw draw;auto mesh=std::make_shared<MeshPayload>();mesh->vertices={{{-4,-4,z},{0,0,1},{0,0}},{{4,-4,z},{0,0,1},{1,0}},{{4,4,z},{0,0,1},{1,1}},{{-4,4,z},{0,0,1},{0,1}}};mesh->indices={0,1,2,0,2,3};draw.mesh=mesh;
        if(hole){auto material=std::make_shared<MaterialPayload>();material->images[0]=std::make_shared<ImageRGBA8>(ImageRGBA8{1,1,{255,255,255,0}});draw.material=material;draw.parameters.factors.w=.5f;}
        return draw;
    };
    RenderWorldSnapshot snapshot;snapshot.frame.cameraPosition={0,0,.15f};snapshot.frame.view=glm::lookAt(snapshot.frame.cameraPosition,glm::vec3(0),glm::vec3(0,1,0));
    snapshot.frame.viewProjection=glm::perspective(glm::radians(60.f),1.f,.01f,10.f)*snapshot.frame.view;
    snapshot.draws={quad(0,false)};AoReferenceOptions options;options.width=options.height=1;options.samples=256;options.power=1;
    auto trace=[&]{pt::CpuScene scene(snapshot);return traceAoReference(scene,snapshot.frame,{0,0,0,1},options);};
    auto open=trace();require(open.matchedPixels==1&&open.visibility[0]==1,"AO ray reference self-occluded a plane");
    snapshot.draws.push_back(quad(.25f,false));auto blocked=trace();require(blocked.visibility[0]<.3f,"AO ray reference did not see a parallel blocker");
    options.samples=4096;auto converged=trace();require(std::abs(blocked.visibility[0]-converged.visibility[0])<.015f,"AO cosine sampler did not converge");
    options.samples=256;options.threads=1;auto serial=trace();require(serial.visibility==blocked.visibility,"AO reference depends on worker count");
    options.bias=.3f;require(trace().visibility[0]==1,"AO reference ignored the geometric-normal bias");options.bias=.025f;
    {
        pt::CpuScene scene(snapshot);options.width=options.height=16;options.samples=64;
        std::vector<float> positions(16*16*4,0);for(size_t p=0;p<16*16;++p)positions[p*4+3]=1;
        auto one=traceAoReference(scene,snapshot.frame,positions,options);options.threads=4;
        auto parallel=traceAoReference(scene,snapshot.frame,positions,options);
        require(one.visibility==parallel.visibility&&one.rays==parallel.rays,"Parallel AO reference was not deterministic");
        options.width=options.height=1;options.samples=256;
    }
    snapshot.draws[1]=quad(1.5f,false);require(trace().visibility[0]==1,"AO reference occluded beyond its radius");
    snapshot.draws[1]=quad(.25f,true);require(trace().visibility[0]==1,"AO reference ignored alpha cutoff holes");
    options.radius=0;require(trace().visibility[0]==1,"Zero-radius AO reference was not open");
    pt::CpuScene scene(snapshot);options.radius=1;auto mismatch=traceAoReference(scene,snapshot.frame,{0,0,.08f,1},options);require(mismatch.matchedPixels==0,"AO reference accepted a different primary surface");
    options.radius=std::numeric_limits<float>::quiet_NaN();bool rejected=false;try{trace();}catch(const std::invalid_argument&){rejected=true;}require(rejected,"AO reference accepted NaN radius");
    require(blocked.saturatedRays==0&&converged.saturatedRays==0,"AO reference exhausted alpha traversal");
    std::cout<<"AO cosine ray reference: plane, blocker, finite radius, alpha cutoff, sample convergence, deterministic workers and surface matching passed\n";
}
}
