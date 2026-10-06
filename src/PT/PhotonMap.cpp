#include "PT/PhotonMap.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <tuple>
#include <atomic>
#include <mutex>
#include <thread>

namespace pt {
namespace {
constexpr float pi=3.14159265358979323846f,epsilon=1e-4f;
float energy(glm::vec3 v){return glm::dot(v,glm::vec3(.2126f,.7152f,.0722f));}
bool finite(glm::vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
glm::vec3 local(glm::vec3 v,glm::vec3 n){auto t=glm::normalize(glm::cross(std::abs(n.y)<.99f?glm::vec3(0,1,0):glm::vec3(1,0,0),n));return t*v.x+glm::cross(n,t)*v.y+n*v.z;}
glm::vec3 offset(const Surface &s,glm::vec3 d){float bias=std::max(epsilon,8*std::numeric_limits<float>::epsilon()*std::max({std::abs(s.position.x),std::abs(s.position.y),std::abs(s.position.z)}));return s.position+s.geometricNormal*(glm::dot(s.geometricNormal,d)>=0?bias:-bias);}
glm::ivec3 key(glm::vec3 p,float radius){auto k=glm::floor(p/radius);if(!finite(k)||glm::any(glm::greaterThan(glm::abs(k),glm::vec3(2147480000.f))))throw std::length_error("Photon grid coordinates exceed int32; increase radius");return glm::ivec3(k);}
bool less(glm::ivec3 a,glm::ivec3 b){return std::tie(a.x,a.y,a.z)<std::tie(b.x,b.y,b.z);}
float adjoint(const Surface &s,glm::vec3 v,glm::vec3 l){float denominator=std::abs(glm::dot(v,s.geometricNormal)*glm::dot(l,s.normal));return denominator>1e-8f?std::abs(glm::dot(v,s.normal)*glm::dot(l,s.geometricNormal))/denominator:0;}
}
PhotonMap::PhotonMap(const CpuScene &scene,const Options &options):paths(options.photonPaths),radius(options.photonRadius){
    validateOptions(options);scene.validatePhotonMapping();auto started=std::chrono::steady_clock::now();
    auto box=scene.bounds();key(box.first,radius);key(box.second,radius);auto center=(box.first+box.second)*.5f;float aperture=std::max(glm::length(box.second-box.first)*.5001f,.01f);
    Random probe;bool area=scene.sampleEmitter(probe).pdfArea>0,sun=scene.sunRadius>0&&energy(scene.sunIrradiance)>0,sky=bool(scene.environment);
    uint32_t sources=uint32_t(area)+uint32_t(sun)+uint32_t(sky);if(!sources)throw std::invalid_argument("Photon mapping requires an area emitter, finite sun, or HDR environment");
    auto mediaInfo=scene.media();auto medium=[&](uint32_t id){for(const auto &m:mediaInfo)if(m.id==id)return m;return MediumInfo{};};
    const uint32_t workers=std::min(paths,std::min(16u,options.threads?options.threads:std::max(1u,std::thread::hardware_concurrency()/2)));
    struct Worker {std::vector<Photon> photons;uint64_t rays=0,caustics=0;};std::vector<Worker> results(workers);
    std::atomic<uint32_t> stored{0};std::atomic<bool> failed{false};std::exception_ptr failure;std::mutex mutex;std::vector<std::thread> threads;
    auto run=[&](uint32_t worker){try{auto &workerResult=results[worker];workerResult.photons.reserve(std::min<uint64_t>(uint64_t(paths)*2/workers,4000000/workers));
        for(uint32_t path=worker;path<paths&&!failed.load(std::memory_order_relaxed);path+=workers){auto rng=Random::forPixel(options.seed^0x70686f746f6eull,path,0,false);glm::vec3 origin,d,beta;uint32_t source=std::min(uint32_t(rng.uniform()*sources),sources-1);bool areaOrigin=area&&source==0;
        if(area&&source==0){auto emitter=scene.sampleEmitter(rng);auto n=emitter.surface.normal;if(emitter.surface.twoSided&&rng.uniform()<.5f)n=-n;auto uv=rng.uniform2();float r=std::sqrt(uv[0]),phi=2*pi*uv[1];d=local({r*std::cos(phi),r*std::sin(phi),std::sqrt(1-uv[0])},n);origin=offset(emitter.surface,d);beta=emitter.surface.emission*emitter.surface.opacity*(pi*(emitter.surface.twoSided?2.f:1.f)*sources/emitter.pdfArea);}
        else {source-=uint32_t(area);glm::vec3 Le;float pdf;
            if(sun&&source==0){auto uv=rng.uniform2();float edge=std::cos(scene.sunRadius),c=glm::mix(1.f,edge,uv[0]),r=std::sqrt(std::max(0.f,1-c*c)),phi=2*pi*uv[1];d=-local({r*std::cos(phi),r*std::sin(phi),c},scene.sunDirection);Le=scene.sunIrradiance/(pi*std::sin(scene.sunRadius)*std::sin(scene.sunRadius));pdf=1/(2*pi*(1-edge));}
            else {auto e=scene.environment->sample(rng);d=-e.direction;Le=e.radiance;pdf=e.pdf;}
            auto uv=rng.uniform2();float r=aperture*std::sqrt(uv[0]),phi=2*pi*uv[1];origin=center-d*(aperture+2*epsilon)+local({r*std::cos(phi),r*std::sin(phi),0},d);beta=pdf>0?Le*(pi*aperture*aperture*sources/pdf):glm::vec3(0);
        }
        std::array<uint32_t,8> winding{};std::array<uint32_t,8> media{};if(areaOrigin)media=scene.initialMedia(origin,&winding);uint32_t count=0;while(count<8&&media[count])++count;
        bool diffuseSeen=false;uint32_t specular=0;
        auto transition=[&](const Surface &hit){if(!hit.mediumId)return;uint32_t found=count;for(uint32_t i=0;i<count;++i)if(media[i]==hit.mediumId)found=i;
            if(hit.frontFace){if(found<count){if(++winding[found]>64)throw std::runtime_error("Photon medium self-overlap exceeded limit");return;}if(count==8)throw std::runtime_error("Photon medium stack overflow");
                if(hit.water){for(uint32_t i=count;i>0;--i){media[i]=media[i-1];winding[i]=winding[i-1];}media[0]=hit.mediumId;winding[0]=1;++count;}else{media[count]=hit.mediumId;winding[count++]=1;}
            }else if(found<count){if(winding[found]>1){--winding[found];return;}if(!hit.water&&found+1!=count)throw std::invalid_argument("Photon reference does not support overlapping non-water media");for(uint32_t i=found+1;i<count;++i){media[i-1]=media[i];winding[i-1]=winding[i];}--count;}
        };
        for(uint32_t bounce=0,transparent=0;bounce<options.maxDepth;){Surface hit;++workerResult.rays;if(!scene.intersect(origin,d,epsilon,std::numeric_limits<float>::infinity(),hit))break;
            auto inside=medium(count?media[count-1]:0);beta*=transmittance(inside.volume.absorption,hit.distance);if(energy(beta)<=0)break;
            // Match the camera walk's winding and water-host boundary semantics.
            if((hit.water&&inside.id&&inside.kind!=3)||(hit.mediumId==inside.id&&inside.id&&(hit.frontFace||winding[count-1]>1))){transition(hit);if(++transparent>128)throw std::runtime_error("Photon transparent boundary limit exceeded");origin=offset(hit,d);continue;}
            hit.exteriorIor=hit.frontFace?inside.ior:(count>1&&media[count-1]==hit.mediumId?medium(media[count-2]).ior:inside.id&&inside.id!=hit.mediumId?inside.ior:1.f);
            bool delta=hit.ior>0&&hit.transmissionRoughness<.02f&&hit.foam==0;
            if(!delta&&bounce>0){if(stored.fetch_add(1,std::memory_order_relaxed)>=4000000)throw std::length_error("Photon map exceeds four million stored vertices; reduce photon paths/depth");bool caustic=!diffuseSeen&&specular>0;workerResult.photons.push_back({glm::vec4(hit.position,float(bounce)),glm::vec4(hit.geometricNormal,float(path)),glm::vec4(-d,0),glm::vec4(beta,caustic?1.f:0.f)});workerResult.caustics+=caustic;}
            if(!delta)diffuseSeen=true;else ++specular;
            if(bounce+1==options.maxDepth)break;
            if(hit.ior>0){float eta=hit.frontFace?hit.exteriorIor/hit.ior:hit.ior/hit.exteriorIor;auto refracted=glm::refract(d,hit.normal,eta);if(glm::dot(glm::reflect(d,hit.normal),hit.geometricNormal)<=0||(glm::dot(refracted,refracted)>0&&glm::dot(refracted,hit.geometricNormal)>=0))hit.normal=hit.geometricNormal;}
            auto sample=sampleBsdf(hit,-d,rng,TransportMode::Importance);if(sample.pdf<=1e-20f)break;
            beta*=sample.value*((hit.ior>0?std::abs(glm::dot(hit.normal,sample.direction)):surfaceCosine(hit,sample.direction))/sample.pdf);beta*=adjoint(hit,-d,sample.direction);
            if(!finite(beta))throw std::runtime_error("Non-finite photon throughput");
            if(sample.transmission)transition(hit);
            origin=offset(hit,sample.direction);d=sample.direction;
            ++bounce;
            if(bounce>=3){float survive=glm::clamp(std::max({beta.x,beta.y,beta.z}),.05f,.95f);if(rng.uniform()>=survive)break;beta/=survive;}
        }
    }
    }catch(...){std::lock_guard<std::mutex> lock(mutex);if(!failure)failure=std::current_exception();failed=true;}};
    try{for(uint32_t worker=0;worker<workers;++worker)threads.emplace_back(run,worker);}catch(...){failed=true;for(auto &thread:threads)thread.join();throw;}
    for(auto &thread:threads)thread.join();if(failure)std::rethrow_exception(failure);
    photons_.reserve(stored);for(auto &workerResult:results){rays+=workerResult.rays;causticCount+=workerResult.caustics;photons_.insert(photons_.end(),workerResult.photons.begin(),workerResult.photons.end());std::vector<Photon>().swap(workerResult.photons);}
    std::stable_sort(photons_.begin(),photons_.end(),[&](const auto &a,const auto &b){auto ka=key(glm::vec3(a.positionDepth),radius),kb=key(glm::vec3(b.positionDepth),radius);if(ka!=kb)return less(ka,kb);return a.normal.w!=b.normal.w?a.normal.w<b.normal.w:a.positionDepth.w<b.positionDepth.w;});
    for(uint32_t i=0;i<photons_.size();++i){auto k=key(glm::vec3(photons_[i].positionDepth),radius);if(cells_.empty()||glm::ivec3(cells_.back().keyFirst)!=k)cells_.push_back({glm::ivec4(k,int(i)),glm::uvec4(0)});++cells_.back().count.x;}
    seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();std::cout<<"Photon map: "<<paths<<" paths, "<<photons_.size()<<" vertices, "<<causticCount<<" caustic photons, "<<seconds<<" s\n"<<std::flush;
}
PhotonEstimate PhotonMap::estimate(const Surface &s,glm::vec3 view,uint32_t remainingDepth) const {
    PhotonEstimate result;auto k=key(s.position,radius);float r2=radius*radius;
    for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z){auto neighbor=k+glm::ivec3(x,y,z);auto it=std::lower_bound(cells_.begin(),cells_.end(),neighbor,[](const auto &cell,glm::ivec3 value){return less(glm::ivec3(cell.keyFirst),value);});if(it==cells_.end()||glm::ivec3(it->keyFirst)!=neighbor)continue;
        for(uint32_t i=uint32_t(it->keyFirst.w),end=i+it->count.x;i<end;++i){const auto &p=photons_[i];auto delta=glm::vec3(p.positionDepth)-s.position;float distance2=glm::dot(delta,delta);
            if(distance2>=r2||p.positionDepth.w>=remainingDepth||glm::dot(glm::vec3(p.normal),s.geometricNormal)<.9f||std::abs(glm::dot(delta,s.geometricNormal))>radius*.1f)continue;
            // Epanechnikov disk kernel: integral over a flat disk is one.
            auto value=evaluateBsdf(s,view,glm::vec3(p.incoming))*glm::vec3(p.fluxCaustic)*(2*(1-distance2/r2)/(pi*r2*paths));result.radiance+=value;if(p.fluxCaustic.w>0)result.caustics+=value;
        }
    }return result;
}
uint64_t PhotonMap::memoryBytes() const{return photons_.size()*sizeof(Photon)+cells_.size()*sizeof(PhotonCell);}
static_assert(sizeof(Photon)==64&&sizeof(PhotonCell)==32,"Photon GPU ABI");
} // namespace pt
