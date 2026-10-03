#include "PT/CpuPathTracer.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <mutex>
#include <thread>
namespace pt {
namespace {
constexpr float pi=3.14159265358979323846f,epsilon=1e-4f;
enum class Kind {Surface,Emitter,Camera};
struct Vertex {Surface hit;glm::vec3 beta{1};Kind kind=Kind::Surface;};
float energy(glm::vec3 v){return glm::dot(v,glm::vec3(.2126f,.7152f,.0722f));}
bool finite(glm::vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
glm::vec3 direction(const Vertex &a,const Vertex &b){return glm::normalize(b.hit.position-a.hit.position);}
Surface orient(Surface s,glm::vec3 view){if(glm::dot(view,s.geometricNormal)<0){s.geometricNormal=-s.geometricNormal;s.normal=-s.normal;s.frontFace=!s.frontFace;}return s;}
glm::vec3 offset(const Surface &s,glm::vec3 d){return s.position+s.geometricNormal*(glm::dot(s.geometricNormal,d)>=0?epsilon:-epsilon);}
float adjoint(const Surface &s,glm::vec3 v,glm::vec3 l){
    const float denominator=std::abs(glm::dot(v,s.geometricNormal)*glm::dot(l,s.normal));
    return denominator>1e-8f?std::abs(glm::dot(v,s.normal)*glm::dot(l,s.geometricNormal))/denominator:0;
}
glm::vec3 scattering(Surface s,glm::vec3 view,glm::vec3 out,bool importance){
    s=orient(s,view);auto f=evaluateBsdf(s,view,out);return importance?f*adjoint(s,view,out):f;
}
glm::vec3 cosineDirection(glm::vec3 n,Random &rng){
    auto uv=rng.uniform2();float r=std::sqrt(uv[0]),phi=2*pi*uv[1];auto t=glm::normalize(glm::cross(std::abs(n.y)<.99f?glm::vec3(0,1,0):glm::vec3(1,0,0),n));return t*(r*std::cos(phi))+glm::cross(n,t)*(r*std::sin(phi))+n*std::sqrt(1-uv[0]);
}
bool visible(const CpuScene &scene,const Vertex &a,const Vertex &b,uint64_t &rays){
    auto to=b.hit.position-a.hit.position;float distance=glm::length(to);if(distance<=2*epsilon)return false;auto d=to/distance;Surface hit;++rays;return !scene.intersect(offset(a.hit,d),d,epsilon,distance-2*epsilon,hit);
}
void walk(const CpuScene &scene,std::vector<Vertex> &vertices,glm::vec3 origin,glm::vec3 d,glm::vec3 beta,Random &rng,uint32_t depth,bool importance,uint64_t &rays){
    // Fixed depth, no roulette: strategy densities do not need survival factors.
    for(uint32_t bounce=0;bounce<=depth;++bounce){Surface hit;++rays;if(!scene.intersect(origin,d,epsilon,std::numeric_limits<float>::infinity(),hit))break;
        vertices.push_back({hit,beta,Kind::Surface});if(bounce==depth)break;rng.dimension((importance?65536u:2u)+bounce*256u+12u);
        auto sample=sampleBsdf(hit,-d,rng,importance?TransportMode::Importance:TransportMode::Radiance);if(sample.pdf<=1e-20f)break;
        beta*=sample.value*(std::abs(glm::dot(hit.normal,sample.direction))/sample.pdf);if(importance)beta*=adjoint(hit,-d,sample.direction);
        if(!finite(beta))throw std::runtime_error("BDPT non-finite subpath throughput");if(energy(beta)<=0)break;origin=offset(hit,sample.direction);d=sample.direction;
    }
}
// Directional densities are converted to area at each non-delta edge. Delta
// edges use a common discrete placeholder, with impossible connection splits
// excluded. Weights form a partition of unity over the supported strategies.
// This also avoids multiplying tiny densities along deep paths.
double edgeDensity(const CpuScene &scene,const std::vector<Vertex> &path,size_t from,size_t to,size_t previous){
    const auto &a=path[from],&b=path[to];auto d=direction(a,b);double pdf=0;
    if(a.kind==Kind::Camera)pdf=scene.cameraPdf(d);
    else if(a.kind==Kind::Emitter){float cosine=glm::dot(a.hit.normal,d);if(!a.hit.twoSided&&cosine<=0)return 0;pdf=std::abs(cosine)/pi*(a.hit.twoSided?.5:1);}
    else {if(a.hit.ior>0)return 1;auto view=direction(a,path[previous]);auto surface=orient(a.hit,view);pdf=bsdfPdf(surface,view,d);}
    const auto delta=b.hit.position-a.hit.position;double distance2=glm::dot(delta,delta);return distance2>0?pdf*std::abs(glm::dot(b.hit.geometricNormal,-d))/distance2:0;
}
double strategyLog(const CpuScene &scene,const std::vector<Vertex> &path,size_t split){
    const size_t n=path.size();if(n==2&&split==1)return -INFINITY;
    if(split>0 && ((split>1&&path[split-1].hit.ior>0)||(split<n-1&&path[split].hit.ior>0)))return -INFINITY;
    double log=0;if(split){double area=scene.emitterPdfArea(path.front().hit.primitive);if(area<=0)return -INFINITY;log=std::log(area);}
    auto add=[&](double pdf){if(pdf<=0)return false;log+=std::log(pdf);return true;};
    for(size_t i=0;i+1<split;++i)if(!add(edgeDensity(scene,path,i,i+1,i?i-1:0)))return -INFINITY;
    for(size_t i=n-1;i>split;--i)if(!add(edgeDensity(scene,path,i,i-1,i+1<n?i+1:i)))return -INFINITY;
    return log;
}
float weight(const CpuScene &scene,const std::vector<Vertex> &path,size_t split){
    const double selected=strategyLog(scene,path,split);if(!std::isfinite(selected))return 0;double sum=0;
    for(size_t s=0;s<path.size();++s){double log=strategyLog(scene,path,s);if(std::isfinite(log)){double ratio=2*(log-selected);if(ratio>700)return 0;sum+=std::exp(ratio);}}
    return float(1/sum);
}
std::vector<Vertex> joined(const std::vector<Vertex> &light,size_t s,const std::vector<Vertex> &camera,size_t t){
    std::vector<Vertex> path;path.reserve(s+t);for(size_t i=0;i<s;++i)path.push_back(light[i]);for(size_t i=t;i>0;--i)path.push_back(camera[i-1]);path.front().kind=Kind::Emitter;path.front().hit.normal=path.front().hit.geometricNormal;return path;
}
bool causticPath(const std::vector<Vertex> &path){bool delta=false;for(size_t i=1;i+1<path.size();++i){if(path[i].hit.ior>0)delta=true;else if(delta)return true;}return false;}
struct Splat {size_t pixel;glm::vec3 value;bool caustic;};
struct Result {glm::vec3 camera{0},caustic{0};std::vector<Splat> splats;uint64_t rays=0;};
Result sample(const CpuScene &scene,const Options &options,uint32_t pixel,uint32_t iteration){
    Result result;auto rng=Random::forPixel(options.seed,pixel,iteration,options.sobol);glm::vec3 origin,d;float u=(pixel%options.width+rng.uniform())/options.width,v=(pixel/options.width+rng.uniform())/options.height;scene.cameraRay(u,v,origin,d);
    Vertex eye;eye.kind=Kind::Camera;eye.hit.position=origin;std::vector<Vertex> camera{eye},light;
    walk(scene,camera,origin,d,glm::vec3(1),rng,options.maxDepth,false,result.rays);
    rng.dimension(65536);auto source=scene.sampleEmitter(rng);light.push_back({source.surface,source.surface.emission/source.pdfArea,Kind::Emitter});
    auto n=source.surface.normal;if(source.surface.twoSided&&rng.uniform()<.5f)n=-n;auto emissionDirection=cosineDirection(n,rng);float pdf=std::abs(glm::dot(source.surface.normal,emissionDirection))/pi*(source.surface.twoSided?.5f:1.f);
    walk(scene,light,offset(source.surface,emissionDirection),emissionDirection,light[0].beta*(std::abs(glm::dot(source.surface.normal,emissionDirection))/pdf),rng,options.maxDepth-1,true,result.rays);
    // s=0: camera paths that reach an emitter, including pure delta chains.
    for(size_t t=2;t<=camera.size();++t){const auto &end=camera[t-1];if(energy(end.hit.emission)<=0||t-2>options.maxDepth)continue;auto path=joined(light,0,camera,t);auto value=end.beta*end.hit.emission*weight(scene,path,0);result.camera+=value;if(causticPath(path))result.caustic+=value;}
    for(size_t s=1;s<=light.size();++s)for(size_t t=1;t<=camera.size();++t){
        if(s+t<3||s+t-2>options.maxDepth)continue;const auto &a=light[s-1],&b=camera[t-1];if(a.hit.ior>0||(t>1&&b.hit.ior>0))continue;
        auto to=b.hit.position-a.hit.position;float distance2=glm::dot(to,to);if(distance2<=4*epsilon*epsilon)continue;auto l=to/std::sqrt(distance2);
        if(s==1&&!a.hit.twoSided&&glm::dot(a.hit.normal,l)<=0)continue;
        glm::vec3 fa=s==1?glm::vec3(1):scattering(a.hit,direction(a,light[s-2]),l,true);if(energy(fa)<=0)continue;
        glm::vec3 value=a.beta*fa;glm::vec2 uv;float cameraPdf=0;
        if(t==1){if(!scene.project(a.hit.position,uv,cameraPdf))continue;value*=std::abs(glm::dot(a.hit.normal,l))*cameraPdf/distance2;}
        else {auto fb=scattering(b.hit,direction(b,camera[t-2]),-l,false);if(energy(fb)<=0)continue;value*=b.beta*fb*(std::abs(glm::dot(a.hit.normal,l)*glm::dot(b.hit.normal,l))/distance2);}
        if(!visible(scene,a,b,result.rays))continue;auto path=joined(light,s,camera,t);value*=weight(scene,path,s);
        if(!finite(value))throw std::runtime_error("BDPT non-finite connection");
        if(t==1){size_t x=std::min(uint32_t(uv.x*options.width),options.width-1),y=std::min(uint32_t(uv.y*options.height),options.height-1);result.splats.push_back({y*options.width+x,value,causticPath(path)});}else{result.camera+=value;if(causticPath(path))result.caustic+=value;}
    }
    return result;
}
}
Image renderBdpt(const CpuScene &scene,const Options &options,const std::function<void(const Image &)> &progress){
    if(options.adaptive||options.guiding||options.radianceCache||options.maxDepth>32)throw std::invalid_argument("BDPT requires fixed spp, depth <=32, without guiding/cache");
    validateOptions(options);scene.validateBidirectional();const size_t count=size_t(options.width)*options.height;
    Image image;image.width=options.width;image.height=options.height;image.execution="CPU BDPT area lights + pinhole";image.radiance.resize(count);image.caustics.resize(count);image.albedo.resize(count);image.normal.resize(count);image.sampleCounts.resize(count);
    const uint32_t workers=std::min(uint32_t(count),options.threads?options.threads:std::max(1u,std::thread::hardware_concurrency()/2));
    if(count*(uint64_t(2+2*workers)*sizeof(glm::dvec3)+4*sizeof(glm::vec3)+sizeof(uint32_t))>1073741824ull)throw std::length_error("BDPT reference film/splat memory exceeds 1 GiB; reduce resolution or workers");
    std::vector<glm::dvec3> cameraSum(count),causticSum(count);std::vector<std::vector<glm::dvec3>> splats(workers,std::vector<glm::dvec3>(count)),causticSplats(workers,std::vector<glm::dvec3>(count));
    for(uint32_t i=0;i<count;++i){glm::vec3 o,d;scene.cameraRay((i%options.width+.5f)/options.width,(i/options.width+.5f)/options.height,o,d);Surface hit;if(scene.intersect(o,d,epsilon,std::numeric_limits<float>::infinity(),hit)){image.albedo[i]=hit.albedo;image.normal[i]=hit.normal;}++image.rays;}
    auto started=std::chrono::steady_clock::now();
    for(uint32_t first=0;first<options.samples;){uint32_t end=std::min(options.samples,first<4?4u:first<16?16u:first+32);std::vector<std::thread> threads;std::vector<uint64_t> rays(workers);std::exception_ptr failure;std::mutex mutex;
        auto run=[&](uint32_t worker){try{for(uint32_t pixel=worker;pixel<count;pixel+=workers)for(uint32_t i=first;i<end;++i){auto value=sample(scene,options,pixel,i);cameraSum[pixel]+=glm::dvec3(value.camera);causticSum[pixel]+=glm::dvec3(value.caustic);for(const auto &splat:value.splats){splats[worker][splat.pixel]+=glm::dvec3(splat.value);if(splat.caustic)causticSplats[worker][splat.pixel]+=glm::dvec3(splat.value);}rays[worker]+=value.rays;}}catch(...){std::lock_guard<std::mutex> lock(mutex);if(!failure)failure=std::current_exception();}};
        try{for(uint32_t w=0;w<workers;++w)threads.emplace_back(run,w);}catch(...){for(auto &thread:threads)thread.join();throw;}
        for(auto &thread:threads)thread.join();if(failure)std::rethrow_exception(failure);for(auto value:rays)image.rays+=value;
        for(size_t i=0;i<count;++i){auto sum=cameraSum[i],caustic=causticSum[i];for(uint32_t w=0;w<workers;++w){sum+=splats[w][i];caustic+=causticSplats[w][i];}image.radiance[i]=glm::vec3(sum/double(end));image.caustics[i]=glm::vec3(caustic/double(end));image.sampleCounts[i]=end;}
        image.samples=end;image.totalSamples=uint64_t(count)*end;image.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();std::cout<<"CPU BDPT: "<<end<<" spp, "<<image.seconds<<" s, "<<image.rays<<" rays\n"<<std::flush;
        if(progress)progress(image);first=end;
    }
    return image;
}
} // namespace pt
