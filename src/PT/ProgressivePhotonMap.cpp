#include "PT/PhotonMap.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace pt {
namespace {
constexpr double pi=3.14159265358979323846;
struct Pixel {
    glm::dvec3 direct{0},tau{0},causticTau{0};
    double count=0,radius=0;
};
bool finite(glm::vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
// Independent pixel ownership; deterministic photon order comes from PhotonMap.
// Exceptions always join every worker before unwinding to the caller.
template<class F> void pixelsParallel(size_t count,uint32_t workers,F process){
    std::atomic<size_t> next{0};std::atomic<bool> failed{false};std::exception_ptr failure;
    std::mutex mutex;std::vector<std::thread> threads;
    auto run=[&]{try{for(;;){if(failed.load(std::memory_order_relaxed))break;auto first=next.fetch_add(64);if(first>=count)break;for(size_t i=first;i<std::min(first+64,count);++i)process(i);}}
        catch(...){std::lock_guard<std::mutex> lock(mutex);if(!failure)failure=std::current_exception();failed=true;}};
    try{for(uint32_t i=0;i<workers;++i)threads.emplace_back(run);}catch(...){failed=true;for(auto &thread:threads)thread.join();throw;}
    for(auto &thread:threads)thread.join();if(failure)std::rethrow_exception(failure);
}
}
Image renderSppm(const CpuScene &scene,const Options &options,const std::function<void(const Image &)> &progress){
    validateOptions(options);scene.validatePhotonMapping();
    const size_t count=size_t(options.width)*options.height;
    // Bound persistent pixels + transient camera endpoints + film/AOVs before allocation.
    const uint64_t stateBytes=count*(sizeof(Pixel)+sizeof(VisiblePoint)+4*sizeof(glm::vec3)+sizeof(uint32_t));
    if(stateBytes>512ull*1024*1024)throw std::length_error("SPPM pixel state exceeds 512 MiB; reduce resolution");
    auto started=std::chrono::steady_clock::now();
    Image image;image.width=options.width;image.height=options.height;
    image.execution="CPU SPPM (stochastic camera endpoints, progressive uniform disk density)";
    image.progressiveStateBytes=stateBytes;
    image.radiance.resize(count);image.albedo.resize(count);image.normal.resize(count);image.caustics.resize(count);image.sampleCounts.resize(count);
    std::vector<Pixel> pixels(count);std::vector<VisiblePoint> points(count);
    for(auto &p:pixels)p.radius=options.photonRadius;
    const uint32_t workers=uint32_t(std::min<size_t>(count,options.threads?options.threads:std::max(1u,std::thread::hardware_concurrency()-std::min(1u,std::thread::hardware_concurrency()))));
    for(uint32_t iteration=0;iteration<options.samples;++iteration){
        std::atomic<uint64_t> cameraRays{0};
        pixelsParallel(count,workers,[&](size_t i){
            auto &p=pixels[i];uint64_t rays=0;glm::vec3 o,d;
            const uint32_t x=uint32_t(i%options.width),y=uint32_t(i/options.width);
            if(!iteration){scene.cameraRay((x+.5f)/options.width,(y+.5f)/options.height,o,d);Surface h;++rays;if(scene.intersect(o,d,1e-4f,std::numeric_limits<float>::infinity(),h)){image.albedo[i]=h.albedo;image.normal[i]=h.normal;}}
            auto rng=Random::forPixel(options.seed,uint32_t(i),iteration,options.sobol);
            const float u=(x+rng.uniform())/options.width,v=(y+rng.uniform())/options.height;
            scene.cameraRay(u,v,o,d);
            auto direct=scene.trace(o,d,rng,options.maxDepth,rays,nullptr,options.waterSunProposal,options.thinSunProposal,nullptr,options.shadowAnyHit,nullptr,&points[i]);
            if(!finite(direct)||!finite(points[i].beta))throw std::runtime_error("SPPM: non-finite camera contribution");
            p.direct+=glm::dvec3(direct);cameraRays.fetch_add(rays,std::memory_order_relaxed);
        });
        image.rays+=cameraRays.load();
        // Rebuild only the current batch; memory does not grow with iterations.
        // A cell covers the largest active disk, so every query visits 27 cells.
        Options batch=options;float largest=0;
        for(size_t i=0;i<count;++i)if(points[i].valid)largest=std::max(largest,float(pixels[i].radius));
        batch.photonRadius=std::max(largest,1e-5f);
        PhotonMap photons(scene,batch,iteration,false);
        image.emittedPhotonPaths+=photons.paths;image.photonSeconds+=photons.seconds;image.photonRays+=photons.rays;
        image.storedPhotons+=photons.photons().size();image.causticPhotons+=photons.causticCount;
        image.photonBytes=std::max(image.photonBytes,photons.memoryBytes());
        std::atomic<uint64_t> merges{0};
        pixelsParallel(count,workers,[&](size_t i){
            auto &p=pixels[i];const auto &v=points[i];
            if(v.valid){const auto g=photons.gather(v.surface,v.view,v.remainingDepth,float(p.radius));
                if(g.count){
                    // Hachisuka/Jensen SPPM: effective N is fractional, M counts
                    // photons including zero-valued BSDF contributions. No path
                    // normalization until film reconstruction, including misses.
                    const double next=p.count+options.sppmAlpha*g.count;
                    const double ratio=next/(p.count+g.count);
                    p.radius*=std::sqrt(ratio);p.count=next;
                    p.tau=(p.tau+glm::dvec3(v.beta)*g.flux)*ratio;
                    p.causticTau=(p.causticTau+glm::dvec3(v.beta)*g.causticFlux)*ratio;
                    merges.fetch_add(g.count,std::memory_order_relaxed);
                }
            }
            const double normalization=1/(pi*p.radius*p.radius*double(image.emittedPhotonPaths));
            image.radiance[i]=glm::vec3(p.direct/double(iteration+1)+p.tau*normalization);
            image.caustics[i]=glm::vec3(p.causticTau*normalization);
            image.sampleCounts[i]=iteration+1;
            if(!finite(image.radiance[i])||!finite(image.caustics[i]))throw std::runtime_error("SPPM: non-finite progressive film");
        });
        image.photonMerges+=merges.load();image.samples=iteration+1;image.progressiveIterations=iteration+1;image.totalSamples=count*(iteration+1ull);
        if(iteration==0||(iteration+1)%options.checkpointSamples==0||iteration+1==options.samples){
            double radiusSum=0;image.progressiveRadiusMin=options.photonRadius;image.progressiveRadiusMax=0;
            for(auto &p:pixels){radiusSum+=p.radius;image.progressiveRadiusMin=std::min(image.progressiveRadiusMin,float(p.radius));image.progressiveRadiusMax=std::max(image.progressiveRadiusMax,float(p.radius));}
            image.progressiveRadiusMean=float(radiusSum/count);
            // Keep the existing report convention: trace seconds exclude photon build.
            image.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()-image.photonSeconds;
            std::cout<<"CPU SPPM "<<iteration+1<<" iterations, "<<image.emittedPhotonPaths<<" emitted paths, mean radius "<<image.progressiveRadiusMean<<", "<<image.seconds+image.photonSeconds<<" s\n"<<std::flush;
            if(progress){auto checkpoint=std::chrono::steady_clock::now();progress(image);image.checkpointSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-checkpoint).count();}
        }
    }
    image.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()-image.photonSeconds;
    return image;
}
} // namespace pt
