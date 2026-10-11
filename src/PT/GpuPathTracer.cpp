#include "PT/GpuPathTracer.h"
#include "PT/PhotonMap.h"
#include "PT/ValidationScenes.h"
#include "renderer/rhi/Resources.h"
#include "rhi/ShaderAssets.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numeric>
#include <map>
#include <stdexcept>
namespace pt {
struct GpuSceneCache::Lease {
    render::Resources resources;
    rhi::BufferHandle buffer;
    std::vector<uint8_t> bytes;
    // A live immutable packed owner proves the same byte array. Weak ownership
    // avoids pinning an entire old CPU export after its scenes are destroyed.
    std::weak_ptr<const void> identity;
    const void *source=nullptr;
    uint64_t used=0;
    Lease(std::shared_ptr<rhi::GraphicsDevice> device,const char *name,size_t size):resources(std::move(device)),bytes(size) {
        buffer=resources.buffer({size,rhi::BufferUsage::Storage|rhi::BufferUsage::CopySource|rhi::BufferUsage::CopyDestination,name,rhi::BufferInitialization::Uninitialized});
    }
};
struct GpuSceneCache::Impl {
    std::shared_ptr<rhi::GraphicsDevice> device;
    std::map<std::string,std::shared_ptr<Lease>> entries;
    size_t budget=0,bytes=0;uint64_t clock=0;
    std::shared_ptr<rhi::RayTracingScene> rayScene;
    std::weak_ptr<const PackedGeometry> rayGeometry;
    std::vector<rhi::RayTracingInstance> rayInstances;
};
GpuSceneCache::GpuSceneCache(std::shared_ptr<rhi::GraphicsDevice> device,size_t budget):impl_(std::make_unique<Impl>()) {
    if(!device||!device->isOpen())throw std::invalid_argument("GPU scene cache needs an open device");
    impl_->device=std::move(device);impl_->budget=budget;
}
GpuSceneCache::~GpuSceneCache()=default;
size_t GpuSceneCache::retainedBytes()const{return impl_->bytes+(impl_->rayScene?impl_->rayScene->bytes:0);}
struct GpuSceneCacheAccess {
    static std::shared_ptr<GpuSceneCache::Lease> candidate(GpuSceneCache &cache,const std::shared_ptr<rhi::GraphicsDevice> &device,const char *name,size_t bytes) {
        auto &c=*cache.impl_;if(c.device!=device)throw std::invalid_argument("GPU scene cache belongs to another device");
        auto prior=c.entries.find(name);
        return prior!=c.entries.end()&&prior->second->bytes.size()==bytes?prior->second:nullptr;
    }
    static void publish(GpuSceneCache &cache,const char *name,const std::shared_ptr<GpuSceneCache::Lease> &lease) {
        auto &c=*cache.impl_;const auto bytes=lease->bytes.size();lease->used=++c.clock;
        auto prior=c.entries.find(name);
        if(prior!=c.entries.end()){if(prior->second==lease)return;c.bytes-=prior->second->bytes.size();c.entries.erase(prior);}
        while(!c.entries.empty()&&bytes<=c.budget&&c.bytes>c.budget-bytes){auto oldest=std::min_element(c.entries.begin(),c.entries.end(),[](const auto &a,const auto &b){return a.second->used<b.second->used;});c.bytes-=oldest->second->bytes.size();c.entries.erase(oldest);}
        if(bytes<=c.budget){c.entries[name]=lease;c.bytes+=bytes;}
        if(c.rayScene&&c.rayScene->bytes>c.budget-c.bytes){c.rayScene.reset();c.rayGeometry.reset();c.rayInstances.clear();}
    }
    static std::shared_ptr<rhi::RayTracingScene> rayCandidate(GpuSceneCache &cache,const std::shared_ptr<const PackedGeometry>& geometry,const std::vector<rhi::RayTracingInstance>& instances,bool &exact){
        auto &c=*cache.impl_;exact=false;if(!c.rayScene||c.rayGeometry.lock()!=geometry)return {};
        exact=c.rayInstances.size()==instances.size();if(exact)for(size_t i=0;i<instances.size();++i)if(c.rayInstances[i].mesh!=instances[i].mesh||c.rayInstances[i].transform!=instances[i].transform){exact=false;break;}
        return c.rayScene;
    }
    static void rayPublish(GpuSceneCache &cache,const std::shared_ptr<const PackedGeometry>& geometry,const std::vector<rhi::RayTracingInstance>& instances,const std::shared_ptr<rhi::RayTracingScene>& scene){
        auto &c=*cache.impl_;if(scene->bytes>c.budget-c.bytes)return;
        c.rayScene=scene;c.rayGeometry=geometry;c.rayInstances=instances;
    }
};
GpuSceneUpload prepareGpuScene(const CpuScene &scene,const Options *options) {
    if(options&&options->sppm)throw std::invalid_argument("SPPM is CPU-only; use CPU rendering");
    const auto started=std::chrono::steady_clock::now();GpuSceneUpload upload;upload.data=scene.exportData();
    if(scene.environment){const auto &cdf=scene.environment->cumulative();upload.environment.reserve(cdf.size());for(size_t i=0;i<cdf.size();++i)upload.environment.emplace_back(scene.environment->pixels()[i],float(cdf[i]/scene.environment->totalWeight()));upload.environment.back().w=1;}
    if(scene.cloudShadow){scene.cloudShadow->validate();}
    if(scene.cloudShadow)for(float value:scene.cloudShadow->transmission)upload.environment.emplace_back(value,0,0,0);
    if(options&&options->photonMapping)upload.photons=std::make_shared<PhotonMap>(scene,*options);
    upload.exportSeconds=std::max(0.0,std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()-(upload.photons?upload.photons->seconds:0));return upload;
}
namespace {
struct alignas(16) Parameters {
    glm::mat4 inverseProjection;
    glm::vec4 camera,sunDirectionRadius,sunIrradiance;
    glm::uvec4 dimensions,counts,control;
    glm::vec4 settings;
    glm::uvec4 adaptive,learning;
    glm::vec4 guideSettings;
    std::array<glm::uvec4,2> cameraMedia{},cameraWinding{};
    glm::uvec4 acceleration{0};
    std::array<glm::vec4,8> thinSolarNormals{};
    glm::vec4 photonSettings{0};
    glm::uvec4 photonControl{0};
    glm::vec4 cloudPatch{0},cloudProjection{0};
    glm::uvec4 cloudInfo{0};
};
struct alignas(16) Pixel {glm::vec4 mean{0},m2{0};glm::uvec4 stats{0};glm::vec4 albedo{0},normal{0};};
static_assert(sizeof(Parameters)==512 && sizeof(Pixel)==80,"GPU path tracing ABI");
struct alignas(16) GuideCell {glm::uvec4 meta{0};glm::vec4 tail{0};std::array<float,64> cdf{},environmentCdf{};};
static_assert(sizeof(GuideCell)==544,"Guide cell ABI");
rhi::ComputePipelineDesc pathPipelineDesc(bool native=false,bool probes=false) {
    using namespace rhi;
    const auto path=defaultShaderDirectory()+(probes?"/path-trace-probe.comp":"/path-trace.comp");
    ComputePipelineDesc desc;desc.shader={path+".glsl",path+(native?".native.metallib":".metallib"),path+".spv",path+".json","main0"};desc.threads={8,8,1};desc.label=native?"GPU PT native Metal ray query":"Portable GPU path tracing";desc.nativeRayTracing=native;
    desc.bindings={{0,{{0,BindingType::UniformBuffer,ShaderStage::Compute,"Parameters",sizeof(Parameters)},
                      {1,BindingType::StorageRead,ShaderStage::Compute,"TlasNodes",0},{2,BindingType::StorageRead,ShaderStage::Compute,"Vertices",0},
                      {3,BindingType::StorageRead,ShaderStage::Compute,"Triangles",0},{4,BindingType::StorageRead,ShaderStage::Compute,"Materials",0},
                      {5,BindingType::StorageRead,ShaderStage::Compute,"Texels",0},
                      {6,BindingType::StorageRead,ShaderStage::Compute,"Instances",0},{7,BindingType::StorageRead,ShaderStage::Compute,"InstanceOrder",0}}},
                   {1,{{0,BindingType::StorageRead,ShaderStage::Compute,"Images",0},{1,BindingType::StorageRead,ShaderStage::Compute,"Environment",0},
                      {2,BindingType::StorageRead,ShaderStage::Compute,"Lights",0},{3,BindingType::StorageRead,ShaderStage::Compute,"Emitters",0},
                      {4,BindingType::StorageReadWrite,ShaderStage::Compute,"Pixels",0},{5,BindingType::StorageRead,ShaderStage::Compute,"Guide",0},{6,BindingType::StorageReadWrite,ShaderStage::Compute,"Training",0}}},
                   {2,{{0,BindingType::StorageRead,ShaderStage::Compute,"Photons",0},{1,BindingType::StorageRead,ShaderStage::Compute,"PhotonCells",0},{2,BindingType::StorageRead,ShaderStage::Compute,"BlasNodes",0}}}};
    return desc;
}
rhi::ComputePipelineDesc displayPipelineDesc() {
    using namespace rhi;
    const auto path=defaultShaderDirectory()+"/pt-display.comp";
    ComputePipelineDesc desc;desc.shader={path+".glsl",path+".metallib",path+".spv",path+".json","main0"};desc.threads={8,8,1};desc.label="GPU PT film display";
    desc.bindings={{0,{{0,BindingType::UniformBuffer,ShaderStage::Compute,"DisplayParameters",48},{1,BindingType::StorageRead,ShaderStage::Compute,"Pixels",0},{2,BindingType::StorageTextureWrite,ShaderStage::Compute,"outputImage",0,Format::RGBA8UNorm},{3,BindingType::StorageRead,ShaderStage::Compute,"Denoised",0}}}};
    return desc;
}
class Kernel {
  public:
    Kernel(const CpuScene &scene,const Options &options,std::shared_ptr<rhi::GraphicsDevice> device,std::shared_ptr<GpuSceneCache> cache={},const GpuSceneUpload *prepared=nullptr,std::shared_ptr<const GpuSceneUpload> payload={},bool probes=false):cache_(std::move(cache)),resources_(std::move(device)) {
        validateOptions(options);
        if(options.bdpt)throw std::invalid_argument("GPU PT does not implement BDPT; use the CPU BDPT reference");
        if(options.sppm)throw std::invalid_argument("GPU PT does not implement SPPM; use CPU rendering");
        if(!resources_.device->computeLimits().supported || resources_.device->backend()==rhi::Backend::OpenGL)throw std::invalid_argument("GPU PT requires Metal or Vulkan compute");
        if(!options.sobol)throw std::invalid_argument("GPU PT uses Sobol; use CPU for the PCG reference");
        if(scene.scatteringCount()&&(options.guiding||options.radianceCache))throw std::invalid_argument("GPU volume PT requires guiding/cache disabled");
        deferred_=bool(payload);
        if(payload)payload_=std::move(payload);
        // The synchronous pointer API borrows only until this constructor has
        // finished all uploads; avoid copying an entire exported scene again.
        else if(prepared)payload_=std::shared_ptr<const GpuSceneUpload>(prepared,[](const GpuSceneUpload *){});
        else payload_=std::make_shared<GpuSceneUpload>(prepareGpuScene(scene,&options));
        if(options.photonMapping)photons_=payload_->photons?payload_->photons:std::make_shared<PhotonMap>(scene,options);
        parameters_.photonControl={options.photonMapping?1u:0u,photons_?uint32_t(photons_->cells().size()):0u,options.photonPaths,0u};
        if(photons_)parameters_.photonSettings={options.photonRadius,2.f/(3.14159265358979323846f*options.photonRadius*options.photonRadius*options.photonPaths),.9f,.1f};
        const auto &data=payload_->data;if(!data.geometry||!data.textures)throw std::invalid_argument("GPU PT payload needs packed geometry and textures");exportSeconds_=payload_->exportSeconds;geometryExportReused_=data.geometryReused;textureExportReused_=data.texturesReused;parameters_.acceleration.x=uint32_t(data.instances.size());parameters_.acceleration.y=uint32_t(std::count_if(data.materials.begin(),data.materials.end(),[](const auto &m){return m.optics.x>0&&(m.extra.w&1u)==0;}));
        parameters_.cameraMedia=data.cameraMedia;parameters_.cameraWinding=data.cameraWinding;parameters_.inverseProjection=data.inverseProjection;parameters_.camera=glm::vec4(data.camera,0);
        parameters_.acceleration.w=options.shadowAnyHit?(scene.opaqueShadows()?3u:1u):0u;
        parameters_.acceleration.z=uint32_t(scene.thinSolarNormals().size());for(size_t i=0;i<scene.thinSolarNormals().size();++i)parameters_.thinSolarNormals[i]=glm::vec4(scene.thinSolarNormals()[i],0);
        const uint32_t highSeed=uint32_t(options.seed>>32);std::memcpy(&parameters_.camera.w,&highSeed,4);
        if(scene.cloudShadow){parameters_.cloudPatch=scene.cloudShadow->patch;parameters_.cloudProjection=scene.cloudShadow->projection;parameters_.cloudInfo={scene.environment?scene.environment->width()*scene.environment->height():0,scene.cloudShadow->width,0,0};}
        parameters_.sunDirectionRadius=glm::vec4(scene.sunDirection,scene.sunRadius);parameters_.sunIrradiance=glm::vec4(scene.sunIrradiance,0);
        parameters_.dimensions={options.width,options.height,0,0};parameters_.counts={uint32_t(data.tlasNodes.size()),uint32_t(data.lights.size()),scene.environment?scene.environment->width():0,scene.environment?scene.environment->height():0};
        parameters_.control={options.maxDepth,uint32_t(options.seed),0,uint32_t(data.emitters.size())};parameters_.settings={float(data.emitterWeight),options.relativeError,options.absoluteError,data.inverseSquare?1.f:0.f};parameters_.adaptive={options.adaptive?1:0,options.minimumSamples,options.samples,1};
        parameters_.learning={options.guiding||options.radianceCache?16384u:0u,(options.guiding?1u:0u)|(options.radianceCache?2u:0u)|(options.waterSunProposal?0u:4u)|(options.thinSunProposal?0u:8u),options.cacheMinimum,options.cacheDepth};
        float extent=1;if(!data.tlasNodes.empty()){auto size=glm::vec3(data.tlasNodes[0].high-data.tlasNodes[0].low);extent=std::max({size.x,size.y,size.z,1.f});}
        parameters_.guideSettings={options.guideCellSize>0?options.guideCellSize:extent/48,.35f,.5f,scene.scatteringCount()?1.f:0.f};
        using namespace rhi;
        if(options.gpuTraversal==GpuTraversal::Native&&!resources_.device->supportsNativeRayTracing())throw std::runtime_error("GPU PT: forced native RT is unavailable on this device");
        native_=options.gpuTraversal!=GpuTraversal::Software&&resources_.device->supportsNativeRayTracing()&&!data.instances.empty();
        auto desc=pathPipelineDesc(native_,probes);
        pipelineDesc_=desc;
        auto &geometry=entries_[0],&lighting=entries_[1],&photonEntries=entries_[2];
        memoryBytes_=sizeof(Parameters);
        queue(&parameters_,sizeof(Parameters),"PT parameters",BufferUsage::Uniform|BufferUsage::CopyDestination,0,0,1);
        auto add=[&](const auto &values,std::vector<BindingEntry> &entries,uint32_t binding,const char *name,std::shared_ptr<const void> identity={}){
            using Value=typename std::decay_t<decltype(values)>::value_type;
            const size_t bytes=std::max(values.size(),size_t(1))*sizeof(Value);
            if(bytes>resources_.device->computeLimits().maxStorageRange)throw std::length_error(std::string("GPU PT buffer exceeds device storage range: ")+name);
            memoryBytes_+=bytes;
            queue(values.empty()?nullptr:values.data(),bytes,name,BufferUsage::Storage|BufferUsage::CopyDestination,&entries==&geometry?0:&entries==&lighting?1:2,binding,0);
            if(!values.empty())tasks_.back().identity=std::move(identity);
        };
        add(data.tlasNodes,geometry,1,"PT TLAS");add(data.geometry->blasNodes,photonEntries,2,"PT BLAS",data.geometry);add(data.geometry->vertices,geometry,2,"PT vertices",data.geometry);add(data.geometry->triangles,geometry,3,"PT triangles",data.geometry);add(data.materials,geometry,4,"PT materials");add(data.textures->texels,geometry,5,"PT texels",data.textures);
        add(data.instances,geometry,6,"PT instances");add(data.instanceOrder,geometry,7,"PT TLAS instance order");
        add(data.textures->images,lighting,0,"PT image descriptors",data.textures);
        add(payload_->environment,lighting,1,"PT HDR environment");add(data.lights,lighting,2,"PT lights");add(data.emitters,lighting,3,"PT emitters");
        const size_t pixelBytes=size_t(options.width)*options.height*sizeof(Pixel);if(pixelBytes>resources_.device->computeLimits().maxStorageRange)throw std::length_error("GPU PT output exceeds device storage range");
        pixels_.resize(size_t(options.width)*options.height);
        memoryBytes_+=pixelBytes;
        film_=std::make_unique<render::Resources>(resources_.device);
        queue(pixels_.data(),pixelBytes,"PT progressive pixels",BufferUsage::Storage|BufferUsage::CopySource|BufferUsage::CopyDestination,1,4,2);
        const size_t capacity=std::max(1u,parameters_.learning.x);guide_.resize(capacity);training_.resize(capacity*133,0);
        const size_t guideBytes=guide_.size()*sizeof(GuideCell),trainBytes=training_.size()*sizeof(uint32_t);memoryBytes_+=guideBytes+trainBytes;
        queue(guide_.data(),guideBytes,"PT frozen guiding/cache",BufferUsage::Storage|BufferUsage::CopyDestination,1,5,3);
        queue(training_.data(),trainBytes,"PT integer training",BufferUsage::Storage|BufferUsage::CopySource|BufferUsage::CopyDestination,1,6,4);
        const std::vector<Photon> noPhotons;const std::vector<PhotonCell> noCells;
        add(photons_?photons_->photons():noPhotons,photonEntries,0,"PT frozen photon vertices");add(photons_?photons_->cells():noCells,photonEntries,1,"PT sorted photon cells");
        // Empty photon arrays are queued with an owned zero dummy.
        lightingLayout_=desc.bindings[1];
        if(!deferred_)while(!prepareStep(16*1024*1024,1)){}
    }
    bool ready() const {return ready_;}
    const GpuPreparationStats &preparationStatistics() const {return preparation_;}
    void cancelPreparation() {if(!ready_){tasks_.clear();entries_={};leases_.clear();payload_.reset();photons_.reset();rayScene_.reset();rayInstances_.clear();film_.reset();resources_.clear();}}
    bool prepareStep(size_t byteBudget,double secondsBudget) {
        if(byteBudget<4||!std::isfinite(secondsBudget)||secondsBudget<=0)throw std::invalid_argument("GPU PT preparation needs at least 4 bytes and a positive finite time budget");
        if(ready_)return true;
        auto &p=preparation_;++p.steps;p.lastStepBytes=p.lastStepWorkBytes=0;
        const auto started=std::chrono::steady_clock::now();
        auto elapsed=[&]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();};
        // Pipeline creation has no incremental RHI API. Keep it separate from
        // buffer work, and report the stall rather than calling it budgeted.
        if(!pipeline_.value){pipeline_=resources_.computePipeline(pipelineDesc_);p.pipelineSeconds+=elapsed();return false;}
        const size_t budget=byteBudget&~size_t(3);
        while(taskIndex_<tasks_.size()&&p.lastStepWorkBytes<budget&&elapsed()<secondsBudget) {
            auto &t=tasks_[taskIndex_];
            if(!t.checked){
                t.prior=cache_&&t.kind==0?GpuSceneCacheAccess::candidate(*cache_,resources_.device,t.name,t.bytes):nullptr;t.checked=true;
                if(t.prior&&t.identity&&t.prior->identity.lock()==t.identity&&t.prior->source==t.data){
                    t.lease=t.prior;t.buffer=t.lease->buffer;++cacheHits_;++p.identityCacheHits;p.identityCacheBytes+=t.bytes;finishTask(t);continue;
                }
            }
            if(t.prior&&t.compared<t.bytes){
                const auto bytes=std::min({t.bytes-t.compared,budget-size_t(p.lastStepWorkBytes),size_t(256*1024)});
                const auto stage=std::chrono::steady_clock::now();const bool equal=std::memcmp(t.prior->bytes.data()+t.compared,t.source()+t.compared,bytes)==0;
                if(!equal){
                    // Four-byte runs are portable to Vulkan buffer copies. Stop
                    // collecting when updates become dense or fragmented; a
                    // full upload is cheaper than cloning plus many patches.
                    for(size_t at=t.compared;at<t.compared+bytes;at+=4){
                        if(std::memcmp(t.prior->bytes.data()+at,t.source()+at,4)==0)continue;
                        if(!t.ranges.empty()&&t.ranges.back().first+t.ranges.back().second==at)t.ranges.back().second+=4;
                        else t.ranges.emplace_back(at,4);
                        t.changedBytes+=4;
                        if(t.changedBytes>t.bytes/4||t.ranges.size()>128){t.prior.reset();t.ranges.clear();break;}
                    }
                }
                p.cacheCompareSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-stage).count();p.lastStepWorkBytes+=bytes;t.compared+=bytes;
                if(t.prior){
                    if(t.compared<t.bytes)continue;
                    if(t.ranges.empty()){t.lease=t.prior;t.buffer=t.lease->buffer;++cacheHits_;finishTask(t);continue;}
                }
            }
            if(!t.buffer.value){
                const auto stage=std::chrono::steady_clock::now();
                if(cache_&&t.kind==0){t.lease=std::make_shared<GpuSceneCache::Lease>(resources_.device,t.name,t.bytes);t.lease->identity=t.identity;t.lease->source=t.data;t.buffer=t.lease->buffer;}
                else t.buffer=(t.kind==2?film_.get():&resources_)->buffer({t.bytes,t.usage,t.name,rhi::BufferInitialization::Uninitialized});
                p.allocationSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-stage).count();
                // Allocation can consume the cooperative time budget by itself.
                if(elapsed()>=secondsBudget)break;
            }
            const auto bytes=std::min({t.bytes-t.offset,budget-size_t(p.lastStepWorkBytes),size_t(256*1024)});
            if(t.prior&&t.offset<t.bytes){
                // Copies do not allocate host staging. Use up to 4 MiB (still
                // within the caller's work budget) to avoid one offline Vulkan
                // fence wait per 256 KiB; comparisons/writes retain that cap.
                const auto cloneBytes=std::min({t.bytes-t.offset,budget-size_t(p.lastStepWorkBytes),size_t(4*1024*1024)});
                if(!cloneBytes)break;
                const auto stage=std::chrono::steady_clock::now();
                resources_.device->copyBuffer(t.prior->buffer,t.offset,t.buffer,t.offset,cloneBytes);
                std::memcpy(t.lease->bytes.data()+t.offset,t.prior->bytes.data()+t.offset,cloneBytes);
                p.cloneSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-stage).count();p.clonedBytes+=cloneBytes;p.lastStepWorkBytes+=cloneBytes;t.offset+=cloneBytes;
                continue;
            }
            if(t.prior){
                const auto &range=t.ranges[t.rangeIndex];
                const auto count=std::min({range.second-t.rangeOffset,budget-size_t(p.lastStepWorkBytes),size_t(256*1024)});
                if(!count)break;
                const auto at=range.first+t.rangeOffset;
                const auto stage=std::chrono::steady_clock::now();
                resources_.device->writeBuffer(t.buffer,at,count,t.source()+at);
                std::memcpy(t.lease->bytes.data()+at,t.source()+at,count);
                p.uploadSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-stage).count();
                p.transferredBytes+=count;p.patchBytes+=count;p.lastStepBytes+=count;p.lastStepWorkBytes+=count;t.rangeOffset+=count;
                if(t.rangeOffset==range.second){++p.patchRanges;++t.rangeIndex;t.rangeOffset=0;}
                if(t.rangeIndex==t.ranges.size()){++p.patchBuffers;++uploads_;uploadBytes_+=t.changedBytes;finishTask(t);}
                continue;
            }
            if(!bytes)break;
            const auto stage=std::chrono::steady_clock::now();resources_.device->writeBuffer(t.buffer,t.offset,bytes,t.source()+t.offset);
            if(t.lease)std::memcpy(t.lease->bytes.data()+t.offset,t.source()+t.offset,bytes);
            p.uploadSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-stage).count();p.transferredBytes+=bytes;p.lastStepBytes+=bytes;p.lastStepWorkBytes+=bytes;t.offset+=bytes;
            if(t.offset==t.bytes){if(t.kind==0){++uploads_;uploadBytes_+=t.bytes;}finishTask(t);}
        }
        // Binding publication and optional training get their own preparation
        // call. No partial buffer can enter sampling or the retained cache.
        if(taskIndex_<tasks_.size()||p.lastStepWorkBytes)return false;
        if(native_&&!rayScene_){
            const auto &data=payload_->data;const auto &geometry=*data.geometry;
            rhi::RayTracingSceneDesc d;d.vertexStride=sizeof(PackedVertex);d.vertexCount=uint32_t(geometry.vertices.size());
            for(const auto &entry:entries_[0])if(entry.binding==2)d.vertices=entry.buffer;
            for(size_t i=0;i<geometry.triangleBases.size();++i){const uint32_t begin=geometry.triangleBases[i],end=i+1<geometry.triangleBases.size()?geometry.triangleBases[i+1]:uint32_t(geometry.triangles.size());d.meshes.push_back({&geometry.triangles[begin].x,end-begin,sizeof(glm::uvec4)});}
            for(const auto &instance:data.instances){rhi::RayTracingInstance v;std::memcpy(v.transform.data(),&instance.model,sizeof(instance.model));v.mesh=instance.range.z;d.instances.push_back(v);}
            bool exact=false;if(cache_)d.previousGeometry=GpuSceneCacheAccess::rayCandidate(*cache_,data.geometry,d.instances,exact);
            rayInstances_=d.instances;
            if(exact){rayScene_=d.previousGeometry;rayCacheHit_=true;}
            else rayScene_=resources_.device->buildRayTracingScene(d);
            return false; // Native build is indivisible; report its time separately.
        }
        const auto stage=std::chrono::steady_clock::now();
        lightingEntries_=entries_[1];
        sets_={resources_.bindings({pipelineDesc_.bindings[0],entries_[0]}),film_->bindings({lightingLayout_,lightingEntries_}),resources_.bindings({pipelineDesc_.bindings[2],entries_[2]})};
        if(cache_)for(auto &t:tasks_)if(t.kind==0){t.lease->identity=t.identity;t.lease->source=t.data;GpuSceneCacheAccess::publish(*cache_,t.name,t.lease);}
        if(cache_&&rayScene_)GpuSceneCacheAccess::rayPublish(*cache_,payload_->data.geometry,rayInstances_,rayScene_);
        tasks_.clear();payload_.reset();ready_=true;p.finalizeSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-stage).count();return true;
    }
    void updateCamera(const SceneCamera &camera) {
        parameters_.inverseProjection=camera.inverseProjection;
        parameters_.camera.x=camera.position.x;parameters_.camera.y=camera.position.y;parameters_.camera.z=camera.position.z;
        parameters_.cameraMedia=camera.media;parameters_.cameraWinding=camera.winding;
    }
    void uploadStats(Image &image) const {image.sceneBufferUploads=uploads_;image.sceneBufferCacheHits=cacheHits_;image.sceneUploadBytes=uploadBytes_;image.sceneCacheBytes=cache_?cache_->retainedBytes():0;image.sceneGeometryExportReused=geometryExportReused_;image.sceneTextureExportReused=textureExportReused_;image.nativeRayTracing=bool(rayScene_);if(rayScene_){image.nativeAccelerationBytes=rayScene_->bytes;image.nativeAccelerationCacheHit=rayCacheHit_;image.nativeBlasBuilds=rayCacheHit_?0:rayScene_->blasBuilds;image.nativeAccelerationBuildSeconds=rayCacheHit_?0:rayScene_->buildSeconds;image.nativeBlasCount=rayScene_->blasCount;image.nativeInstanceCount=rayScene_->instanceCount;}}
    rhi::BufferHandle filmBuffer() const {return pixelBuffer_;}
    size_t filmBytes() const {return pixels_.size()*sizeof(Pixel);}
    void resetFilm(uint32_t width,uint32_t height) {
        const size_t bytes=size_t(width)*height*sizeof(Pixel);
        if(bytes>resources_.device->computeLimits().maxStorageRange)throw std::length_error("GPU PT output exceeds device storage range");
        std::vector<Pixel> pixels(size_t(width)*height);
        auto film=std::make_unique<render::Resources>(resources_.device);
        auto buffer=film->buffer({bytes,rhi::BufferUsage::Storage|rhi::BufferUsage::CopySource|rhi::BufferUsage::CopyDestination,"PT progressive pixels"},pixels.data());
        auto entries=lightingEntries_;entries[4]={4,buffer,0,bytes,{},{}};
        auto bindings=film->bindings({lightingLayout_,entries});
        memoryBytes_=memoryBytes_-filmBytes()+bytes;
        film_=std::move(film);pixels_=std::move(pixels);pixelBuffer_=buffer;lightingEntries_=std::move(entries);sets_[1]=bindings;
        parameters_.dimensions={width,height,0,0};
    }
    uint64_t memoryBytes() const {return memoryBytes_;}
    double exportSeconds() const {return exportSeconds_;}
    void photonStats(Image &image) const {if(!photons_)return;image.photonSeconds=photons_->seconds;image.photonRays=photons_->rays;image.storedPhotons=photons_->photons().size();image.causticPhotons=photons_->causticCount;image.photonBytes=photons_->memoryBytes();}
    void train(const Options &options,Image &image){
        if(!parameters_.learning.x)return;
        const auto started=std::chrono::steady_clock::now();
        for(uint32_t begin=0;begin<options.trainingSamples;begin+=4)dispatch(begin,std::min(begin+4,options.trainingSamples),false,3);
        const auto &pixels=read();for(const auto &pixel:pixels){image.trainingRays+=pixel.stats.z;if(pixel.stats.w)throw std::runtime_error("GPU training non-finite path contribution");}
        resources_.device->readBuffer(trainingBuffer_,0,training_.size()*sizeof(uint32_t),training_.data());
        for(size_t i=0;i<guide_.size();++i){auto base=i*133;auto count=std::min(training_[base+1],8192u);if(!count)continue;
            auto &cell=guide_[i];cell.meta={training_[base],count,0,0};
            for(int c=0;c<3;++c)cell.tail[c]=float(double(training_[base+2+c])/4096/count);
            double total=0;for(int bin=0;bin<64;++bin)total+=training_[base+5+bin];double sum=0;
            for(int bin=0;bin<64;++bin){sum+=training_[base+5+bin];cell.cdf[bin]=total>0?float(sum/total):float(bin+1)/64;}cell.cdf[63]=1;cell.meta.z=total>0?1:0;
            double environmentTotal=0;for(int bin=0;bin<64;++bin)environmentTotal+=training_[base+69+bin];sum=0;
            for(int bin=0;bin<64;++bin){sum+=training_[base+69+bin];cell.environmentCdf[bin]=environmentTotal>0?float(sum/environmentTotal):float(bin+1)/64;}cell.environmentCdf[63]=1;cell.meta.w=environmentTotal>0?1:0;
            if(count>=16)++image.trainedCells;
        }
        resources_.device->writeBuffer(guideBuffer_,0,guide_.size()*sizeof(GuideCell),guide_.data());
        std::fill(pixels_.begin(),pixels_.end(),Pixel{});uploadPixels(pixels_);
        image.trainingSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();image.guideCellSize=parameters_.guideSettings.x;
        std::cout<<"GPU PT training: "<<image.trainedCells<<" cells, "<<image.trainingSeconds<<" s, "<<image.trainingRays<<" rays\n"<<std::flush;
    }
    void dispatch(uint32_t begin,uint32_t end,bool checkAdaptive,uint32_t mode=0) {
        parameters_.dimensions.z=begin;parameters_.dimensions.w=end;parameters_.control.z=mode;
        const uint32_t adaptive=parameters_.adaptive.x;parameters_.adaptive.x=checkAdaptive?adaptive:0;
        resources_.device->writeBuffer(parameterBuffer_,0,sizeof(parameters_),&parameters_);parameters_.adaptive.x=adaptive;
        auto commands=resources_.device->createCommandList();commands.setLabel("GPU PT samples "+std::to_string(begin)+".."+std::to_string(end));
        commands.dispatch(pipeline_,sets_,{(parameters_.dimensions.x+7)/8,(parameters_.dimensions.y+7)/8,1},rayScene_);resources_.device->submit(commands);
    }
    const std::vector<Pixel> &read() {resources_.device->readBuffer(pixelBuffer_,0,pixels_.size()*sizeof(Pixel),pixels_.data());return pixels_;}
    rhi::BufferHandle recordProbe(render::Resources &temp,rhi::CommandList &commands,const SceneCamera &origin,uint32_t sequence,float maxDistance,uint32_t samples) {
        auto p=parameters_;p.camera=glm::vec4(origin.position,0);p.cameraMedia=origin.media;p.cameraWinding=origin.winding;
        p.dimensions.z=sequence*samples;p.dimensions.w=p.dimensions.z+samples;p.control.z=4;
        p.adaptive.x=0;p.learning.x=0;p.learning.y=0;p.photonControl.x=0;p.photonSettings.w=maxDistance;
        auto uniform=temp.buffer({sizeof(p),rhi::BufferUsage::Uniform,"PT probe parameters"},&p);
        auto entries=entries_[0];for(auto &e:entries)if(e.binding==0)e.buffer=uniform;
        auto sets=sets_;sets[0]=temp.bindings({pipelineDesc_.bindings[0],entries});
        commands.setLabel("ddgi/pt-path-capture");commands.dispatch(pipeline_,sets,{(p.dimensions.x+7)/8,(p.dimensions.y+7)/8,1},rayScene_);
        return pixelBuffer_;
    }
    bool nativeRayTracing() const {return bool(rayScene_);}
    void uploadPixels(const std::vector<Pixel> &pixels) {if(pixels.size()!=pixels_.size())throw std::invalid_argument("GPU PT probe extent mismatch");resources_.device->writeBuffer(pixelBuffer_,0,pixels.size()*sizeof(Pixel),pixels.data());}
  private:
    struct UploadTask {
        const char *name=nullptr;const uint8_t *data=nullptr;
        std::vector<uint8_t> dummy;
        size_t bytes=0,offset=0,compared=0;
        size_t changedBytes=0,rangeIndex=0,rangeOffset=0;
        std::vector<std::pair<size_t,size_t>> ranges;
        rhi::BufferUsage usage{};uint32_t group=0,binding=0,kind=0;
        bool checked=false;
        rhi::BufferHandle buffer;
        std::shared_ptr<GpuSceneCache::Lease> prior,lease;
        std::shared_ptr<const void> identity;
        const uint8_t *source()const{return data?data:dummy.data();}
    };
    void queue(const void *source,size_t bytes,const char *name,rhi::BufferUsage usage,uint32_t group,uint32_t binding,uint32_t kind) {
        UploadTask t;t.name=name;t.data=static_cast<const uint8_t*>(source);t.bytes=bytes;t.usage=usage;t.group=group;t.binding=binding;t.kind=kind;
        if(!source)t.dummy.resize(bytes,0);
        tasks_.push_back(std::move(t));entries_[group].push_back({binding,{},0,bytes,{},{}});preparation_.totalBytes+=bytes;
    }
    void finishTask(UploadTask &t) {
        auto &entries=entries_[t.group];for(auto &entry:entries)if(entry.binding==t.binding){entry.buffer=t.buffer;break;}
        if(t.kind==1)parameterBuffer_=t.buffer;else if(t.kind==2)pixelBuffer_=t.buffer;else if(t.kind==3)guideBuffer_=t.buffer;else if(t.kind==4)trainingBuffer_=t.buffer;
        if(t.lease)leases_.push_back(t.lease);
        preparation_.completedBytes+=t.bytes;++taskIndex_;
    }
    std::shared_ptr<const GpuSceneUpload> payload_;
    std::vector<UploadTask> tasks_;
    std::array<std::vector<rhi::BindingEntry>,3> entries_;
    rhi::ComputePipelineDesc pipelineDesc_;
    std::shared_ptr<rhi::RayTracingScene> rayScene_;
    std::vector<rhi::RayTracingInstance> rayInstances_;
    bool rayCacheHit_=false;
    bool native_=false;
    GpuPreparationStats preparation_;
    size_t taskIndex_=0;
    bool deferred_=false,ready_=false;
    std::shared_ptr<GpuSceneCache> cache_;
    // Leases precede Resources: binding sets are retired before their buffers.
    std::vector<std::shared_ptr<GpuSceneCache::Lease>> leases_;
    uint64_t uploads_=0,cacheHits_=0,uploadBytes_=0;
    std::shared_ptr<const PhotonMap> photons_;
    uint64_t memoryBytes_=0;
    bool geometryExportReused_=false,textureExportReused_=false;
    double exportSeconds_=0;
    render::Resources resources_;
    std::unique_ptr<render::Resources> film_;
    rhi::BindingLayout lightingLayout_;
    std::vector<rhi::BindingEntry> lightingEntries_;
    Parameters parameters_{};
    rhi::BufferHandle parameterBuffer_,pixelBuffer_,guideBuffer_,trainingBuffer_;
    std::vector<GuideCell> guide_;
    std::vector<uint32_t> training_;
    rhi::ComputePipelineHandle pipeline_;
    std::vector<rhi::BindingSetHandle> sets_;
    std::vector<Pixel> pixels_;
};
void check(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
render::SnapshotDraw triangle(glm::vec3 a,glm::vec3 b,glm::vec3 c) {
    render::SnapshotDraw draw;auto mesh=std::make_shared<render::MeshPayload>();auto n=glm::normalize(glm::cross(b-a,c-a));mesh->vertices={{a,n,{0,0}},{b,n,{1,0}},{c,n,{.5f,1}}};mesh->indices={0,1,2};draw.mesh=mesh;draw.parameters.albedoAlpha={.7f,.3f,.1f,1};draw.parameters.factors={0,.7f,1,0};return draw;
}
render::RenderWorldSnapshot fixture() {
    render::RenderWorldSnapshot world;world.frame.cameraPosition={0,0,2};glm::mat4 depth(1);depth[2][2]=.5f;depth[3][2]=.5f;
    world.frame.viewProjection=depth*glm::perspective(glm::radians(50.f),1.f,.1f,100.f)*glm::lookAt(world.frame.cameraPosition,glm::vec3(0),glm::vec3(0,1,0));return world;
}
}
struct GpuPipelineWarmup::Impl {
    render::Resources resources;
    GpuPipelineWarmupStats stats;
    explicit Impl(std::shared_ptr<rhi::GraphicsDevice> device):resources(std::move(device)) {
        auto start=std::chrono::steady_clock::now();resources.computePipeline(pathPipelineDesc(resources.device->supportsNativeRayTracing()));
        stats.traceSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        start=std::chrono::steady_clock::now();resources.computePipeline(displayPipelineDesc());
        stats.displaySeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    }
};
GpuPipelineWarmup::GpuPipelineWarmup(std::shared_ptr<rhi::GraphicsDevice> device):impl_(std::make_unique<Impl>(std::move(device))){}
GpuPipelineWarmup::~GpuPipelineWarmup()=default;
const GpuPipelineWarmupStats &GpuPipelineWarmup::statistics()const{return impl_->stats;}
struct GpuProbeTracer::Impl {
    const CpuScene &scene;uint32_t extent,samples;std::unique_ptr<Kernel> kernel;
    Impl(const CpuScene &s,std::shared_ptr<rhi::GraphicsDevice> d,uint32_t n,uint32_t spp,uint32_t depth,std::shared_ptr<GpuSceneCache> cache,const GpuSceneUpload *upload,GpuTraversal traversal):scene(s),extent(n),samples(spp) {
        if(n<4||n>32||spp<1||spp>4096||depth<1||depth>32)throw std::invalid_argument("PT probe budget outside domain");
        Options o;o.width=6*n;o.height=n;o.samples=spp;o.maxDepth=depth;o.adaptive=false;o.gpuTraversal=traversal;
        kernel=std::make_unique<Kernel>(s,o,std::move(d),std::move(cache),upload,std::shared_ptr<const GpuSceneUpload>{},true);
    }
};
GpuProbeTracer::GpuProbeTracer(const CpuScene &s,std::shared_ptr<rhi::GraphicsDevice> d,uint32_t n,uint32_t spp,uint32_t depth,std::shared_ptr<GpuSceneCache> c,const GpuSceneUpload *u,GpuTraversal t):impl_(std::make_unique<Impl>(s,std::move(d),n,spp,depth,std::move(c),u,t)){}
GpuProbeTracer::~GpuProbeTracer()=default;
rhi::BufferHandle GpuProbeTracer::record(render::Resources &temp,rhi::CommandList &commands,glm::vec3 origin,uint32_t sequence,float maxDistance){
    if(!std::isfinite(origin.x)||!std::isfinite(origin.y)||!std::isfinite(origin.z)||!std::isfinite(maxDistance)||maxDistance<=0)throw std::invalid_argument("PT probe origin / distance outside domain");
    SceneCamera c;c.position=origin;std::array<uint32_t,8> winding{};const auto media=impl_->scene.initialMedia(origin,&winding);
    for(uint32_t i=0;i<8;++i){c.media[i/4][i%4]=media[i];c.winding[i/4][i%4]=winding[i];}
    return impl_->kernel->recordProbe(temp,commands,c,sequence,maxDistance,impl_->samples);
}
std::vector<glm::vec4> GpuProbeTracer::readCapture(){std::vector<glm::vec4> out;for(const auto &p:impl_->kernel->read()){if(p.stats.w)throw std::runtime_error("PT probe contains nonfinite paths");out.push_back(p.mean);}return out;}
uint32_t GpuProbeTracer::resolution()const{return impl_->extent;}
bool GpuProbeTracer::nativeRayTracing()const{return impl_->kernel->nativeRayTracing();}
struct GpuRenderSession::Impl {
    struct FilteredFilm {
        render::Resources resources;
        rhi::BufferHandle buffer;
        std::vector<glm::vec4> pixels;
        size_t offset=0;
        uint32_t samples=0;
        FilteredFilm(std::shared_ptr<rhi::GraphicsDevice> device,const Image &image):resources(device),samples(image.samples) {
            pixels.reserve(image.denoised.size());for(auto c:image.denoised)pixels.emplace_back(c,0);
            buffer=resources.buffer({pixels.size()*sizeof(glm::vec4),rhi::BufferUsage::Storage|rhi::BufferUsage::CopyDestination,"PT denoised display",rhi::BufferInitialization::Uninitialized});
        }
    };
    struct Display {
        render::Resources resources;
        rhi::TextureHandle texture;
        rhi::TextureViewHandle view;
        rhi::ComputePipelineHandle pipeline;
        rhi::BufferHandle parameters;
        rhi::BindingSetHandle bindings;
        rhi::BindingLayout layout;
        std::shared_ptr<FilteredFilm> filtered;
        std::unique_ptr<render::Resources> bindingOwner;
        uint32_t width,height;
        Display(std::shared_ptr<rhi::GraphicsDevice> device,Kernel &kernel,uint32_t w,uint32_t h):resources(device),width(w),height(h) {
            using namespace rhi;
            if(!device->computeLimits().maxStorageImages)throw std::invalid_argument("PT viewport requires a compute storage image");
            auto desc=displayPipelineDesc();
            pipeline=resources.computePipeline(desc);
            parameters=resources.buffer({48,BufferUsage::Uniform|BufferUsage::CopyDestination,"PT display exposure"});
            texture=resources.texture({w,h,Format::RGBA8UNorm,TextureUsage::Storage|TextureUsage::ColorAttachment|TextureUsage::Sampled|TextureUsage::CopySource,"PT viewport"});view=resources.view(texture);
            layout=desc.bindings[0];
            bind(kernel,{});
        }
        void bind(Kernel &kernel,std::shared_ptr<FilteredFilm> next) {
            if(bindingOwner&&next==filtered)return;
            auto owner=std::make_unique<render::Resources>(resources.device);
            auto set=owner->bindings({layout,{{0,parameters,0,48,{},{}},{1,kernel.filmBuffer(),0,kernel.filmBytes(),{},{}},{2,{},0,0,view,{}},{3,next?next->buffer:kernel.filmBuffer(),0,next?kernel.filmBytes()/sizeof(Pixel)*sizeof(glm::vec4):kernel.filmBytes(),{},{}}}});
            bindingOwner=std::move(owner);filtered=std::move(next);bindings=set;
        }
    };
    std::shared_ptr<rhi::GraphicsDevice> device;
    Options options;
    Kernel kernel;
    Image image,baseline;
    std::shared_ptr<FilteredFilm> filtered,pendingFiltered;
    std::unique_ptr<Display> output;
    uint32_t first=0,checkpoint=0;
    bool paused=false,cancelled=false,converged=false,initialized=false;
    float cameraAspect;
    Impl(const CpuScene &scene,const Options &opts,std::shared_ptr<rhi::GraphicsDevice> d,std::shared_ptr<GpuSceneCache> cache,const GpuSceneUpload *prepared,std::shared_ptr<const GpuSceneUpload> payload={}):device(std::move(d)),options(opts),kernel(scene,opts,device,std::move(cache),prepared,std::move(payload)),cameraAspect(float(opts.width)/opts.height) {
        image.width=options.width;image.height=options.height;
        if(kernel.ready())finishPreparation();
    }
    void finishPreparation() {
        image.sceneExportSeconds=kernel.exportSeconds();image.gpuBufferBytes=kernel.memoryBytes();image.execution=device->backend()==rhi::Backend::Metal?"GPU Metal compute BLAS/TLAS":"GPU Vulkan compute BLAS/TLAS";
        kernel.uploadStats(image);if(image.nativeRayTracing)image.execution="GPU Metal native triangle ray query";kernel.photonStats(image);if(options.photonMapping)image.execution+=" + photon density indirect (CPU emission)";
        kernel.train(options,image);
        const auto &p=kernel.preparationStatistics();image.gpuPipelineSeconds=p.pipelineSeconds;image.gpuAllocationSeconds=p.allocationSeconds;image.gpuUploadSeconds=p.uploadSeconds;image.gpuCacheCompareSeconds=p.cacheCompareSeconds;image.gpuFinalizeSeconds=p.finalizeSeconds;image.gpuPreparationBytes=p.transferredBytes;image.gpuPreparationSteps=p.steps;image.gpuIdentityCacheHits=p.identityCacheHits;image.gpuIdentityCacheBytes=p.identityCacheBytes;image.gpuCloneBytes=p.clonedBytes;image.gpuPatchBytes=p.patchBytes;image.gpuPatchBuffers=p.patchBuffers;image.gpuPatchRanges=p.patchRanges;image.gpuCloneSeconds=p.cloneSeconds;
        initialized=true;baseline=image;const bool wasPaused=paused;restart();paused=wasPaused;
    }
    void nextCheckpoint(){checkpoint=std::min(options.samples,first<4?4u:first<16?16u:uint32_t(std::min(uint64_t(options.samples),uint64_t(first)+(options.adaptive?32:options.checkpointSamples))));}
    void restart(){image=baseline;image.width=options.width;image.height=options.height;image.gpuBufferBytes=kernel.memoryBytes();first=0;paused=false;cancelled=false;converged=false;nextCheckpoint();}
};
GpuRenderSession::GpuRenderSession(const CpuScene &scene,const Options &options,std::shared_ptr<rhi::GraphicsDevice> device,std::shared_ptr<GpuSceneCache> cache,const GpuSceneUpload *prepared) {
    const auto started=std::chrono::steady_clock::now();impl_=std::make_unique<Impl>(scene,options,std::move(device),std::move(cache),prepared);
    impl_->image.setupSeconds=std::max(0.0,std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()-impl_->image.trainingSeconds);impl_->baseline.setupSeconds=impl_->image.setupSeconds;
}
GpuRenderSession::GpuRenderSession(const CpuScene &scene,const Options &options,std::shared_ptr<rhi::GraphicsDevice> device,std::shared_ptr<GpuSceneCache> cache,std::shared_ptr<const GpuSceneUpload> payload) {
    if(!payload)throw std::invalid_argument("Deferred GPU PT needs an owned prepared payload");
    if(options.photonMapping&&!payload->photons)throw std::invalid_argument("Deferred GPU PT needs worker-prepared photons");
    const auto started=std::chrono::steady_clock::now();impl_=std::make_unique<Impl>(scene,options,std::move(device),std::move(cache),nullptr,std::move(payload));
    impl_->image.setupSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
}
bool GpuRenderSession::prepareStep(size_t bytes,double seconds) {
    auto &s=*impl_;if(s.cancelled)return false;if(s.initialized)return true;
    const auto started=std::chrono::steady_clock::now();
    if(s.kernel.prepareStep(bytes,seconds))s.finishPreparation();
    s.image.setupSeconds+=std::max(0.0,std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()-s.image.trainingSeconds);
    if(s.initialized)s.baseline.setupSeconds=s.image.setupSeconds;
    s.kernel.uploadStats(s.image);return s.initialized;
}
bool GpuRenderSession::ready()const{return impl_->initialized;}
const GpuPreparationStats &GpuRenderSession::preparationStatistics()const{return impl_->kernel.preparationStatistics();}
GpuRenderSession::~GpuRenderSession()=default;
bool GpuRenderSession::step(){
    auto &s=*impl_;if(!s.initialized||s.paused||s.cancelled||complete())return false;
    const auto started=std::chrono::steady_clock::now();const auto end=std::min(s.checkpoint,s.first+s.options.gpuBatchSamples);
    s.kernel.dispatch(s.first,end,s.options.adaptive&&end==s.checkpoint);++s.image.dispatches;
    const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();s.image.dispatchSeconds+=seconds;s.image.seconds+=seconds;
    s.first=end;s.image.samples=end;if(end==s.checkpoint)s.nextCheckpoint();return true;
}
void GpuRenderSession::pause(bool value){impl_->paused=value;}
void GpuRenderSession::cancel(){impl_->cancelled=true;impl_->kernel.cancelPreparation();}
bool GpuRenderSession::paused()const{return impl_->paused;}
bool GpuRenderSession::cancelled()const{return impl_->cancelled;}
bool GpuRenderSession::complete()const{return ready()&&(impl_->converged||impl_->first>=impl_->options.samples);}
uint32_t GpuRenderSession::samples()const{return impl_->first;}
const Image &GpuRenderSession::statistics()const{return impl_->image;}
void GpuRenderSession::reset(){resize(impl_->options.width,impl_->options.height);}
void GpuRenderSession::resize(uint32_t width,uint32_t height){
    if(!ready())throw std::logic_error("GPU PT film is not prepared");
    auto options=impl_->options;options.width=width;options.height=height;validateOptions(options);
    impl_->output.reset();impl_->filtered.reset();impl_->pendingFiltered.reset();impl_->kernel.resetFilm(width,height);impl_->options=options;impl_->restart();
}
void GpuRenderSession::updateCamera(const CpuScene &scene,float aspect){
    if(!std::isfinite(aspect)||aspect<=0)throw std::invalid_argument("Invalid PT camera aspect");
    const auto camera=scene.cameraData();reset();impl_->kernel.updateCamera(camera);impl_->cameraAspect=aspect;
    impl_->baseline.sceneBufferUploads=impl_->baseline.sceneBufferCacheHits=impl_->baseline.sceneUploadBytes=0;
    impl_->baseline.sceneGeometryExportReused=impl_->baseline.sceneTextureExportReused=false;
    impl_->baseline.sceneExportSeconds=impl_->baseline.setupSeconds=0;
    impl_->baseline.gpuPipelineSeconds=impl_->baseline.gpuAllocationSeconds=impl_->baseline.gpuUploadSeconds=impl_->baseline.gpuCacheCompareSeconds=impl_->baseline.gpuFinalizeSeconds=0;
    impl_->baseline.gpuCloneBytes=impl_->baseline.gpuPatchBytes=impl_->baseline.gpuPatchBuffers=impl_->baseline.gpuPatchRanges=0;impl_->baseline.gpuCloneSeconds=0;
    impl_->baseline.gpuPreparationBytes=impl_->baseline.gpuPreparationSteps=impl_->baseline.gpuIdentityCacheHits=impl_->baseline.gpuIdentityCacheBytes=0;impl_->restart();
}
Image GpuRenderSession::readback(){
    if(!ready())throw std::logic_error("GPU PT film is not prepared");
    auto &s=*impl_;auto stage=std::chrono::steady_clock::now();const auto &pixels=s.kernel.read();const double readSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-stage).count();s.image.readbackSeconds+=readSeconds;++s.image.readbacks;s.image.readbackBytes+=pixels.size()*sizeof(Pixel);
    stage=std::chrono::steady_clock::now();auto &image=s.image;const auto count=pixels.size();image.radiance.resize(count);image.albedo.resize(count);image.normal.resize(count);image.sampleCounts.resize(count);if(s.options.photonMapping)image.caustics.resize(count);
    image.rays=0;image.nonFiniteSamples=0;image.totalSamples=0;image.convergedPixels=0;image.guideHits=0;image.cacheHits=0;image.volumeEvents=0;
    for(size_t i=0;i<count;++i){if(s.options.photonMapping)image.caustics[i]={pixels[i].mean.w,pixels[i].m2.w,pixels[i].albedo.w};image.radiance[i]=glm::vec3(pixels[i].mean);image.albedo[i]=glm::vec3(pixels[i].albedo);image.normal[i]=glm::vec3(pixels[i].normal);image.sampleCounts[i]=pixels[i].stats.x;image.totalSamples+=pixels[i].stats.x;image.rays+=pixels[i].stats.z;image.nonFiniteSamples+=pixels[i].stats.w;image.convergedPixels+=pixels[i].stats.y>=2;if(!s.options.photonMapping){image.guideHits+=uint64_t(pixels[i].mean.w);image.cacheHits+=uint64_t(pixels[i].m2.w);}image.volumeEvents+=uint64_t(pixels[i].normal.w);}
    const double filmSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-stage).count();image.filmUpdateSeconds+=filmSeconds;image.seconds+=readSeconds+filmSeconds;
    if(image.nonFiniteSamples)throw std::runtime_error("GPU PT non-finite path contribution");s.converged=image.convergedPixels==count;return image;
}
rhi::TextureHandle GpuRenderSession::display(uint32_t width,uint32_t height,float exposure,bool filtered){
    if(!ready())throw std::logic_error("GPU PT film is not prepared");
    if(!width||!height||width>16384||height>16384||!std::isfinite(exposure)||exposure<=0)throw std::invalid_argument("Invalid PT display extent/exposure");
    auto &s=*impl_;if(!s.output||s.output->width!=width||s.output->height!=height)s.output=std::make_unique<Impl::Display>(s.device,s.kernel,width,height);
    s.output->bind(s.kernel,filtered?s.filtered:nullptr);
    const uint32_t w=std::max(1u,std::min(width,uint32_t(std::max(1.f,std::round(height*s.cameraAspect))))),h=std::max(1u,std::min(height,uint32_t(std::max(1.f,std::round(w/s.cameraAspect)))));
    struct alignas(16) DisplayParameters {glm::vec4 film;glm::uvec4 target,region;};
    const DisplayParameters parameters{{float(s.options.width),float(s.options.height),exposure,0},{width,height,(width-w)/2,(height-h)/2},{w,h,filtered&&s.filtered?1u:0u,0}};
    s.device->writeBuffer(s.output->parameters,0,sizeof(parameters),&parameters);
    auto commands=s.device->createCommandList();commands.setLabel("PT film display (no readback)");commands.dispatch(s.output->pipeline,{s.output->bindings},{(width+7)/8,(height+7)/8,1});s.device->submit(commands);return s.output->texture;
}
void GpuRenderSession::queueDenoisedDisplay(const Image &image) {
    auto &s=*impl_;if(!ready()||s.cancelled)throw std::logic_error("GPU PT film is unavailable");
    if(image.width!=s.options.width||image.height!=s.options.height||image.denoised.size()!=size_t(image.width)*image.height||!image.samples||image.samples>s.first)throw std::invalid_argument("PT denoised display does not match film extent / samples");
    for(auto c:image.denoised)if(!std::isfinite(c.x)||!std::isfinite(c.y)||!std::isfinite(c.z)||glm::any(glm::lessThan(c,glm::vec3(0))))throw std::invalid_argument("PT invalid denoised display pixel");
    if(s.filtered&&image.samples<s.filtered->samples)throw std::invalid_argument("PT denoised display is older than published output");
    s.pendingFiltered=std::make_shared<Impl::FilteredFilm>(s.device,image);
}
bool GpuRenderSession::uploadDenoisedDisplay(size_t byteBudget,double secondsBudget) {
    if(byteBudget<16||!std::isfinite(secondsBudget)||secondsBudget<=0)throw std::invalid_argument("PT denoised upload needs 16 bytes and a finite positive time budget");
    auto &s=*impl_;if(s.cancelled)return false;if(!s.pendingFiltered)return bool(s.filtered);
    auto &f=*s.pendingFiltered;size_t remaining=byteBudget&~size_t(15);const auto started=std::chrono::steady_clock::now();const auto bytes=f.pixels.size()*sizeof(glm::vec4);
    while(f.offset<bytes&&remaining&&std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<secondsBudget){const auto count=std::min({remaining,bytes-f.offset,size_t(256*1024)});s.device->writeBuffer(f.buffer,f.offset,count,reinterpret_cast<const uint8_t*>(f.pixels.data())+f.offset);f.offset+=count;remaining-=count;s.image.denoisedUploadBytes+=count;}
    s.image.denoisedUploadSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    if(f.offset<bytes)return false;
    f.pixels.clear();f.pixels.shrink_to_fit();s.filtered=std::move(s.pendingFiltered);return true;
}
uint32_t GpuRenderSession::denoisedSamples()const{return impl_->filtered?impl_->filtered->samples:0;}
Image renderGpu(const CpuScene &scene,const Options &options,std::shared_ptr<rhi::GraphicsDevice> device,const std::function<void(const Image &)> &progress) {
    GpuRenderSession session(scene,options,std::move(device));const auto started=std::chrono::steady_clock::now();Image image;double checkpointSeconds=0;
    while(!session.complete()) {
        const auto first=session.samples();const auto end=std::min(options.samples,first<4?4u:first<16?16u:uint32_t(std::min(uint64_t(options.samples),uint64_t(first)+(options.adaptive?32:options.checkpointSamples))));
        while(session.samples()<end)session.step();image=session.readback();image.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        std::cout<<image.execution<<": "<<image.samples<<" spp budget, "<<double(image.totalSamples)/(size_t(options.width)*options.height)<<" average spp, "<<image.seconds<<" s, "<<image.rays<<" rays\n"<<std::flush;
        image.checkpointSeconds=checkpointSeconds;if(progress){const auto stage=std::chrono::steady_clock::now();progress(image);checkpointSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-stage).count();}
    }
    image.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();image.checkpointSeconds=checkpointSeconds;return image;
}
void validateGpuPathTracing(std::shared_ptr<rhi::GraphicsDevice> device) {
    if(device->backend()==rhi::Backend::OpenGL || !device->computeLimits().supported)return;
    {
        GpuPipelineWarmup warm(device);const auto builds=device->pipelineCacheStats().computeBuilds;
        device->setPipelineCacheIdleLimit(0);
        GpuPipelineWarmup same(device);auto source=fixture();if(device->supportsNativeRayTracing())source.draws.push_back(triangle({-1,-1,0},{1,-1,0},{0,1,0}));CpuScene warmScene(source);Options options;options.width=options.height=8;options.samples=1;
        GpuRenderSession session(warmScene,options,device);session.display(8,8,1);
        check(device->pipelineCacheStats().computeBuilds==builds,"Warm PT pipeline leases did not survive idle-cache eviction / first display");
        device->setPipelineCacheIdleLimit(64);
        std::cout<<"GPU PT warmup: trace / display descriptor reuse and active lease protection passed\n";
    }
    auto world=fixture();Random rng(813);for(int i=0;i<200;++i){glm::vec3 a{rng.uniform()*8-4,rng.uniform()*8-4,rng.uniform()*8-4};world.draws.push_back(triangle(a,a+glm::vec3(.6f,0,.1f),a+glm::vec3(0,.5f,0)));}
    auto material=std::make_shared<render::MaterialPayload>();material->images[0]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{2,1,{255,0,0,255,0,255,0,0}});world.draws[0].material=material;world.draws[0].parameters.factors.w=.5f;
    material->images[1]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{1,1,{151,105,249,255}});world.draws[0].parameters.emissiveNormal.w=.6f;
    auto tangentMesh=std::make_shared<render::MeshPayload>(*world.draws[0].mesh);for(const auto &v:tangentMesh->vertices)tangentMesh->pathTracingTangents.emplace_back(glm::normalize(glm::vec3(1,0,0)-v.normal*v.normal.x),1);world.draws[0].mesh=tangentMesh;
    // Shared source triangles with mirrors/nonuniform scale must keep world t,
    // UV alpha and inverse-transpose shading normals on both backends.
    for(int i=0;i<32;++i){auto draw=world.draws[i%4];draw.model=glm::translate(glm::mat4(1),glm::vec3((i%8-4)*1.6f,(i/8-2)*1.7f,2))*glm::rotate(glm::mat4(1),.25f*(i%3),glm::vec3(0,1,0))*glm::scale(glm::mat4(1),glm::vec3(i%2?-.6f:1.4f,.4f+.2f*(i%5),1.7f));world.draws.push_back(draw);}
    CpuScene geometry(world);Options probeOptions;probeOptions.width=32;probeOptions.height=32;probeOptions.samples=1;Kernel probe(geometry,probeOptions,device);std::vector<Pixel> rays(1024);std::vector<Surface> references(1024);std::vector<bool> hits(1024);
    // Probe both opaque and cutout UVs away from the exact alpha threshold;
    // rounding at that discontinuity cannot define cross-backend visibility.
    for(size_t i=0;i<rays.size();++i){glm::vec3 o{rng.uniform()*12-6,rng.uniform()*12-6,rng.uniform()*12-6},d=glm::normalize(glm::vec3(rng.uniform()-.5f,rng.uniform()-.5f,rng.uniform()-.5f));if(i<512){const auto &draw=world.draws[world.draws.size()-32+i%32];const auto &v=draw.mesh->vertices;const float weight=(i/32)%2?.25f:.55f;auto center=v[0].position*weight+v[1].position*(.8f-weight)+v[2].position*.2f;auto target=glm::vec3(draw.model*glm::vec4(center,1));d=glm::normalize(glm::vec3(draw.model*glm::vec4(0,0,-1,0)))*(i%2?2.f:1.f);o=target-d*2.f;}rays[i].albedo=glm::vec4(o,0);rays[i].normal=glm::vec4(d,0);hits[i]=geometry.intersect(o,d,1e-4f,1e30f,references[i],true);}
    probe.uploadPixels(rays);probe.dispatch(0,1,false,1);const auto result=probe.read();
    for(size_t i=0;i<result.size();++i){check((result[i].m2.w>0)==hits[i],"GPU BLAS/TLAS visibility differs from world-space CPU brute force");if(hits[i]){check(std::abs(result[i].mean.w-references[i].distance)<.001f,"GPU instance ray parameter differs from CPU");check(glm::length(glm::vec3(result[i].m2)-references[i].albedo)<.002f,"GPU instance material texel differs from CPU");check(glm::length(glm::vec3(result[i].normal)-references[i].normal)<.002f,"GPU mirrored/nonuniform instance normal differs from CPU");}}
    if(device->supportsNativeRayTracing()){
        auto softwareOptions=probeOptions;softwareOptions.gpuTraversal=GpuTraversal::Software;Kernel software(geometry,softwareOptions,device);software.uploadPixels(rays);software.dispatch(0,1,false,1);const auto &reference=software.read();
        for(size_t i=0;i<result.size();++i){check(result[i].m2.w==reference[i].m2.w,"Native/software ray hit mask differs");if(result[i].m2.w>0){check(std::abs(result[i].mean.w-reference[i].mean.w)<1e-4f,"Native/software world t differs");check(glm::length(glm::vec3(result[i].m2-reference[i].m2))<1e-5f&&glm::length(glm::vec3(result[i].normal-reference[i].normal))<1e-5f,"Native/software alpha/material/normal differs");}}
        std::cout<<"Native Metal RT: 1024 rays / cutout / mirrors / nonunit directions passed\n";
    }
    probe.dispatch(0,1,false,2);const auto samples=probe.read();
    const uint32_t probeDimensions[]={0,258,514,24578};
    for(uint32_t i=0;i<samples.size();++i){uint32_t pixelSeed=sampleHash(1^sampleHash(0)^sampleHash(i));for(uint32_t d=0;d<4;++d)check(samples[i].mean[d]==sobolSample(pixelSeed,i,probeDimensions[d]),"GPU Sobol sequence differs from CPU");}
    world=fixture();world.draws.push_back(triangle({-20,-20,0},{20,-20,0},{0,20,0}));world.frame.lights.push_back({{0,0,0,0},{2,2,2,0},{0,0,-1,0}});CpuScene scene(world);scene.environment=std::make_shared<Environment>(16,8,std::vector<glm::vec3>(128,glm::vec3(.3f)));
    Options options;options.width=32;options.height=32;options.samples=128;options.maxDepth=4;options.threads=1;options.adaptive=false;
    const auto cpu=render(scene,options),gpu=renderGpu(scene,options,device);double error=0,energy=0;
    {
        GpuProbeTracer tracer(scene,device,8,128,4,std::make_shared<GpuSceneCache>(device));
        render::Resources temp(device);auto commands=device->createCommandList();const glm::vec3 origin(0,0,2);
        tracer.record(temp,commands,origin,0,45);device->submit(commands);const auto captured=tracer.readCapture();double delta=0,total=0;
        for(uint32_t y=0;y<8;++y)for(uint32_t x=0;x<48;++x){const auto q=(glm::vec2(x%8,y)+.5f)/8.f*2.f-1.f;glm::vec3 d;
            switch(x/8){case 0:d={1,q.y,-q.x};break;case 1:d={-1,q.y,q.x};break;case 2:d={q.x,1,-q.y};break;case 3:d={q.x,-1,q.y};break;case 4:d={q.x,q.y,1};break;default:d={-q.x,q.y,-1};}d=glm::normalize(d);Surface hit;const bool found=scene.intersect(origin,d,1e-4f,1e30f,hit);const auto v=captured[y*48+x];
            check(found?(std::abs(v.w-std::min(hit.distance,45.f))<.002f):v.w==0,"PT probe first-hit distance changed");glm::vec3 reference(0);
            if(found){for(uint32_t i=0;i<128;++i){auto random=Random::forPixel(17,x+y*48,i,false);uint64_t rays=0;reference+=scene.trace(origin,d,random,4,rays)/128.f;}}
            else reference=scene.environment->evaluate(d);
            delta+=glm::length(glm::vec3(v)-reference);total+=glm::length(reference);
        }
        check(delta/std::max(total,1e-9)<.04,"Shared PT probe transport differs from independent CPU integration");
        auto back=device->createCommandList();tracer.record(temp,back,{0,0,-2},1,45);device->submit(back);const auto reverse=tracer.readCapture();bool rejected=false;for(auto v:reverse)if(v.w<0){rejected=true;check(glm::vec3(v)==glm::vec3(0),"Back-facing probe emitted radiance");}check(rejected,"PT probe did not record back-facing geometry");
        std::cout<<"PT probe capture: CPU transport / first-hit distances / backface rejection passed\n";
    }
    for(size_t i=0;i<cpu.radiance.size();++i){check(glm::length(cpu.albedo[i]-gpu.albedo[i])<1e-5f,"GPU albedo AOV changed");error+=glm::length(cpu.radiance[i]-gpu.radiance[i]);energy+=glm::length(cpu.radiance[i]);}
    check(error/std::max(energy,1e-9)<.02,"GPU VNDF/MIS transport differs from CPU reference");
    options.checkpointSamples=32;auto frequent=renderGpu(scene,options,device);check(frequent.radiance==gpu.radiance&&frequent.sampleCounts==gpu.sampleCounts&&frequent.rays==gpu.rays,"GPU fixed-spp checkpoint frequency changed transport");check(frequent.readbacks>gpu.readbacks,"GPU checkpoint interval did not reduce readback count");options.checkpointSamples=256;
    {
        auto small=options;small.width=17;small.height=13;small.samples=4;small.gpuBatchSamples=1;
        auto cache=std::make_shared<GpuSceneCache>(device);
        auto payload=std::make_shared<GpuSceneUpload>(prepareGpuScene(scene,&small));std::weak_ptr<const GpuSceneUpload> lifetime=payload;
        GpuRenderSession interrupted(scene,small,device,cache,std::shared_ptr<const GpuSceneUpload>(payload));payload.reset();
        check(!lifetime.expired()&&!interrupted.ready()&&!interrupted.step()&&!interrupted.complete(),"Partial GPU preparation sampled or released its source");
        bool blocked=false;try{interrupted.readback();}catch(const std::logic_error &){blocked=true;}check(blocked,"Partial film was readable");
        blocked=false;try{interrupted.display(17,13,1);}catch(const std::logic_error &){blocked=true;}check(blocked,"Partial film was displayed");
        blocked=false;try{interrupted.reset();}catch(const std::logic_error &){blocked=true;}check(blocked,"Partial film was reset");
        blocked=false;try{interrupted.prepareStep(3,1);}catch(const std::invalid_argument &){blocked=true;}check(blocked,"Invalid upload budget accepted");
        check(!interrupted.prepareStep(1024,1)&&interrupted.preparationStatistics().lastStepBytes==0,"Pipeline creation was combined with upload");
        for(int i=0;i<12;++i){interrupted.prepareStep(256,1);check(interrupted.preparationStatistics().lastStepWorkBytes<=256,"Cold upload exceeded byte budget");}
        check(!interrupted.ready()&&interrupted.statistics().sceneBufferUploads>0&&cache->retainedBytes()==0,"Incomplete job published immutable buffers");
        interrupted.cancel();check(lifetime.expired()&&!interrupted.prepareStep(1024,1)&&!interrupted.step(),"Cancelled preparation continued work or retained its payload");
        GpuRenderSession resident(scene,small,device,cache);check(resident.statistics().sceneBufferUploads==14,"Cancelled cold job populated cache");
        const auto retainedBytes=cache->retainedBytes();auto edited=world;edited.draws[0].parameters.albedoAlpha.x*=.7f;CpuScene changed(edited);changed.environment=scene.environment;
        auto changedPayload=std::make_shared<GpuSceneUpload>(prepareGpuScene(changed,&small));
        GpuRenderSession abandoned(changed,small,device,cache,std::shared_ptr<const GpuSceneUpload>(changedPayload));
        for(int i=0;i<1000&&abandoned.statistics().sceneBufferUploads==0;++i)abandoned.prepareStep(256,1);
        check(!abandoned.ready()&&abandoned.statistics().sceneBufferUploads==1,"Changed material did not reach a partial preparation");
        abandoned.cancel();check(cache->retainedBytes()==retainedBytes,"Cancelled update changed retained cache budget");
        GpuRenderSession original(scene,small,device,cache);check(original.statistics().sceneBufferCacheHits==14&&original.statistics().sceneBufferUploads==0,"Cancelled update overwrote resident cache");
        std::weak_ptr<const GpuSceneUpload> stagedLifetime=changedPayload;GpuRenderSession staged(changed,small,device,cache,std::shared_ptr<const GpuSceneUpload>(changedPayload));changedPayload.reset();staged.pause(true);
        uint64_t totalWork=0;for(int i=0;!staged.ready()&&i<1000;++i){staged.prepareStep(1027,1);const auto &p=staged.preparationStatistics();check(p.lastStepWorkBytes<=1024&&p.lastStepBytes<=p.lastStepWorkBytes,"Cache comparison or upload exceeded aligned budget");totalWork+=p.lastStepWorkBytes;check(staged.ready()||!staged.step(),"Partially uploaded job sampled");}
        check(staged.ready()&&staged.preparationStatistics().completedBytes==staged.preparationStatistics().totalBytes,"Staged upload failed to finish all bytes");
        check(staged.statistics().sceneBufferUploads==1&&staged.statistics().sceneBufferCacheHits==13,"Staged exact cache reuse differs from synchronous construction");
        check(totalWork+staged.preparationStatistics().identityCacheBytes>=staged.preparationStatistics().totalBytes,"Preparation did not count film / cache comparison work");
        check(stagedLifetime.expired()&&staged.paused()&&!staged.step(),"Completed preparation retained worker payload or lost pending pause");staged.pause(false);
        while(staged.step()){}auto result=staged.readback(),reference=renderGpu(changed,small,device);
        check(result.radiance==reference.radiance&&result.sampleCounts==reference.sampleCounts&&result.rays==reference.rays,"Chunked preparation changed raw GPU film");
        const auto &p=staged.preparationStatistics();std::cout<<"Staged GPU preparation: "<<p.steps<<" calls, "<<p.totalBytes<<" payload bytes, "<<p.transferredBytes<<" transferred bytes; cancellation / exact cache / raw film passed\n";
    }
    {
        auto cache=std::make_shared<GpuSceneCache>(device);auto geometry=std::make_shared<GeometryCache>();
        auto small=options;small.samples=8;small.seed|=uint64_t(1)<<32;CpuScene retained(world,{},geometry);retained.environment=scene.environment;
        auto upload=prepareGpuScene(retained);GpuRenderSession cold(retained,small,device,cache,&upload);
        check(cold.statistics().sceneBufferUploads==14&&cold.statistics().sceneBufferCacheHits==0,"Cold GPU scene upload count changed");
        GpuRenderSession hot(retained,small,device,cache);
        if(device->supportsNativeRayTracing()){check(cold.statistics().nativeRayTracing&&cold.statistics().nativeBlasBuilds>0&&!cold.statistics().nativeAccelerationCacheHit,"Cold native AS was not built");check(hot.statistics().nativeAccelerationCacheHit&&hot.statistics().nativeBlasBuilds==0&&hot.statistics().nativeAccelerationBuildSeconds==0,"Hot native AS was rebuilt");}
        check(hot.statistics().sceneBufferUploads==0&&hot.statistics().sceneBufferCacheHits==14,"Identical GPU scene uploaded immutable buffers again");
        check(hot.preparationStatistics().identityCacheHits==3&&hot.statistics().sceneGeometryExportReused&&hot.statistics().sceneTextureExportReused,"Repeated GPU preparation repacked immutable CPU arrays");
        cold.step();hot.step(); // Both lease the same buffers with independent films.
        auto edited=world;edited.draws[0].parameters.albedoAlpha={.2f,.8f,.4f,1};CpuScene materialScene(edited,{},geometry,&retained);materialScene.environment=scene.environment;
        check(materialScene.accelerationStats().tlasReuses==1&&materialScene.accelerationStats().emitterReuses==1,"GPU material update rebuilt CPU derived acceleration");
        GpuRenderSession materialSession(materialScene,small,device,cache);
        if(device->supportsNativeRayTracing())check(materialSession.statistics().nativeAccelerationCacheHit,"Material edit rebuilt native AS");
        check(materialSession.statistics().sceneBufferUploads==1&&materialSession.statistics().sceneBufferCacheHits==13,"Material scalar edit uploaded geometry/textures");
        check(materialSession.statistics().sceneGeometryExportReused&&materialSession.statistics().sceneTextureExportReused,"Material scalar edit repacked CPU geometry/textures");
        while(materialSession.step()){}check(materialSession.readback().radiance==renderGpu(materialScene,small,device).radiance,"Cached edited material changed GPU film");
        edited.draws[0].model=glm::translate(glm::mat4(1),glm::vec3(0,0,-.3f));CpuScene transformScene(edited,{},geometry,&materialScene);transformScene.environment=scene.environment;
        check(transformScene.accelerationStats().tlasRefits==1&&transformScene.accelerationStats().tlasBuilds==0,"GPU transform update did not use CPU TLAS refit");
        GpuRenderSession transformed(transformScene,small,device,cache);
        if(device->supportsNativeRayTracing())check(!transformed.statistics().nativeAccelerationCacheHit&&transformed.statistics().nativeBlasBuilds==0,"Transform edit rebuilt native BLAS or reused stale TLAS");
        check(transformed.statistics().sceneBufferUploads==2&&transformed.statistics().sceneBufferCacheHits==12,"Transform edit uploaded unchanged vertices/materials");
        auto transformedData=transformScene.exportData();
        check(transformed.statistics().sceneUploadBytes<transformedData.tlasNodes.size()*sizeof(PackedNode)+transformedData.instances.size()*sizeof(PackedInstance)&&transformed.preparationStatistics().patchBuffers>0,"Transform edit did not reduce host uploads");
        edited.frame.lights[0].colorInner.x=4;CpuScene lightScene(edited,{},geometry,&transformScene);lightScene.environment=scene.environment;
        GpuRenderSession relit(lightScene,small,device,cache);
        check(relit.statistics().sceneBufferUploads==1&&relit.statistics().sceneBufferCacheHits==13,"Light edit uploaded geometry/materials");
        while(relit.step()){}check(relit.readback().radiance==renderGpu(lightScene,small,device).radiance,"Cached light update changed transport");
        auto newMaterial=edited.draws[0].material?std::make_shared<render::MaterialPayload>(*edited.draws[0].material):std::make_shared<render::MaterialPayload>();
        auto newImage=newMaterial->images[0]?std::make_shared<render::ImageRGBA8>(*newMaterial->images[0]):std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{1,1,{255,255,255,255}});
        newImage->pixels[0]=128;newMaterial->images[0]=newImage;edited.draws[0].material=newMaterial;
        CpuScene textureScene(edited,{},geometry,&lightScene);textureScene.environment=scene.environment;
        GpuRenderSession textureSession(textureScene,small,device,cache);
        check(textureSession.statistics().sceneGeometryExportReused&&!textureSession.statistics().sceneTextureExportReused,"Texture replacement reused stale CPU texels or repacked geometry");
        while(textureSession.step()){}check(textureSession.readback().radiance==renderGpu(textureScene,small,device).radiance,"Texture-replaced cached scene differs from fresh GPU rendering");
        auto alteredMesh=std::make_shared<render::MeshPayload>(*edited.draws[0].mesh);alteredMesh->vertices[0].position.z+=.03f;edited.draws[0].mesh=alteredMesh;
        CpuScene changedGeometry(edited,{},geometry,&textureScene);changedGeometry.environment=scene.environment;
        GpuRenderSession geometrySession(changedGeometry,small,device,cache);
        if(device->supportsNativeRayTracing())check(geometrySession.statistics().nativeBlasBuilds>0&&!geometrySession.statistics().nativeAccelerationCacheHit,"Geometry edit reused stale native BLAS");
        check(!geometrySession.statistics().sceneGeometryExportReused&&geometrySession.statistics().sceneTextureExportReused,"Geometry replacement reused stale packed roots or repacked unchanged textures");
        CpuScene rebuiltGeometry(edited);rebuiltGeometry.environment=scene.environment;
        while(geometrySession.step()){}check(geometrySession.readback().radiance==renderGpu(rebuiltGeometry,small,device).radiance,"Geometry-replaced packed leases changed raw GPU film");
        while(cold.step()){}check(cold.readback().radiance==renderGpu(retained,small,device).radiance,"Replacing GPU cache slots invalidated a live session");
        auto moved=world.frame;moved.cameraPosition={.3f,0,2};glm::mat4 depth(1);depth[2][2]=.5f;depth[3][2]=.5f;moved.viewProjection=depth*glm::perspective(glm::radians(55.f),1.f,.1f,100.f)*glm::lookAt(moved.cameraPosition,glm::vec3(0),glm::vec3(0,1,0));
        retained.updateCamera(moved);cold.updateCamera(retained,1);
        check(cold.samples()==0&&cold.statistics().sceneBufferUploads==0&&cold.statistics().sceneExportSeconds==0,"Camera update rebuilt or uploaded scene");
        while(cold.step()){}check(cold.readback().radiance==renderGpu(retained,small,device).radiance,"Camera-only GPU update differs from fresh rendering");
        auto tiny=std::make_shared<GpuSceneCache>(device,256);GpuRenderSession bounded(retained,small,device,tiny);bounded.step();
        GpuRenderSession evict(lightScene,small,device,tiny);evict.step();check(tiny->retainedBytes()<=256,"GPU cache exceeded retention budget");
        while(bounded.step()){}check(bounded.readback().radiance==renderGpu(retained,small,device).radiance,"GPU cache eviction invalidated leased buffers");
        std::cout<<"GPU scene cache: cold 14 uploads, hot 0, material/light 1, transform 2, camera 0; edited-film parity and bounded lease retention passed\n";
    }
    {
        auto cache=std::make_shared<GpuSceneCache>(device);auto small=options;small.samples=4;
        auto source=world;auto material=std::make_shared<render::MaterialPayload>();material->images[0]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{1,1,{200,120,80,255}});source.draws[0].material=material;
        std::weak_ptr<const PackedGeometry> geometryOwner;std::weak_ptr<const PackedTextures> textureOwner;Image original;
        {
            CpuScene temporary(source);temporary.environment=scene.environment;auto payload=prepareGpuScene(temporary);
            geometryOwner=payload.data.geometry;textureOwner=payload.data.textures;
            GpuRenderSession session(temporary,small,device,cache,&payload);while(session.step()){}original=session.readback();
        }
        check(geometryOwner.expired()&&textureOwner.expired(),"GPU identity cache pinned destroyed CPU exports");
        CpuScene rebuilt(source);rebuilt.environment=scene.environment;GpuRenderSession fallback(rebuilt,small,device,cache);
        check(fallback.preparationStatistics().identityCacheHits==0&&fallback.statistics().sceneBufferCacheHits==14&&fallback.statistics().sceneBufferUploads==0,"Expired identity did not fall back to exact byte comparison");
        while(fallback.step()){}check(fallback.readback().radiance==original.radiance,"Expired packed-owner cache changed raw film");
        GpuRenderSession sameOwner(rebuilt,small,device,cache);
        check(sameOwner.preparationStatistics().identityCacheHits==5&&sameOwner.statistics().sceneBufferCacheHits==14,"Exact comparison did not promote the new immutable identity");
        std::cout<<"GPU packed identity: weak-owner expiry, exact fallback, identity promotion and raw film passed\n";
    }
    {
        auto input=fixture();auto draw=triangle({-.35f,-.35f,0},{.35f,-.35f,0},{0,.35f,0});auto cache=std::make_shared<GeometryCache>();
        for(int i=0;i<64;++i){auto instance=draw;instance.objectId=i+1;instance.model=glm::translate(glm::mat4(1),glm::vec3((i%8-3.5f)*.7f,(i/8-3.5f)*.7f,0));instance.parameters.albedoAlpha={.2f+.01f*i,.4f,.8f,1};input.draws.push_back(instance);}
        auto originalInput=input;CpuScene old(input,{},cache);input.draws[10].model[3].x+=.05f;
        CpuScene refit(input,{},cache,&old),fresh(input);refit.environment=fresh.environment=scene.environment;
        auto opts=options;opts.samples=16;check(refit.accelerationStats().tlasRefits==1,"GPU multi-instance fixture did not refit");
        check(renderGpu(refit,opts,device).radiance==renderGpu(fresh,opts,device).radiance,"Refitted GPU multi-instance scene differs from rebuilt raw film");
        {
            old.environment=scene.environment;auto small=opts;small.width=17;small.height=13;small.samples=4;small.gpuBatchSamples=1;
            auto gpuCache=std::make_shared<GpuSceneCache>(device);GpuRenderSession resident(old,small,device,gpuCache);resident.step();
            auto edited=originalInput;for(auto &d:edited.draws)d.parameters.albedoAlpha.x*=.8f;
            // Keep transforms identical to isolate the fragmented scalar edits.
            CpuScene changed(edited,{},cache,&old);changed.environment=old.environment;
            auto payload=std::make_shared<GpuSceneUpload>(prepareGpuScene(changed,&small));
            for(bool inFrame:{false,true})for(bool afterPatch:{false,true}){
                if(inFrame)device->beginFrame();
                GpuRenderSession cancelled(changed,small,device,gpuCache,std::shared_ptr<const GpuSceneUpload>(payload));
                for(int i=0;i<10000;++i){cancelled.prepareStep(64,1);const auto &p=cancelled.preparationStatistics();
                    check(p.lastStepWorkBytes<=64,"Clone or patch exceeded preparation byte budget");
                    if(afterPatch?p.patchBytes>0:p.clonedBytes>0)break;
                }
                const auto &p=cancelled.preparationStatistics();
                check(!cancelled.ready()&&(afterPatch?p.patchBytes>0&&p.patchBytes<256:p.clonedBytes>0&&p.clonedBytes<payload->data.materials.size()*sizeof(PackedMaterial)),"Cancellation did not reach partial clone / patch");
                cancelled.cancel();if(inFrame)device->endFrame();GpuRenderSession original(old,small,device,gpuCache);
                check(original.statistics().sceneBufferUploads==0&&original.statistics().sceneBufferCacheHits==14,"Cancelled clone / patch replaced resident cache");
            }
            GpuRenderSession patched(changed,small,device,gpuCache,std::shared_ptr<const GpuSceneUpload>(payload));
            for(int i=0;!patched.ready()&&i<10000;++i){patched.prepareStep(64,1);check(patched.preparationStatistics().lastStepWorkBytes<=64,"Sparse preparation exceeded byte budget");}
            const auto &p=patched.preparationStatistics();
            check(patched.ready()&&p.patchBuffers==1&&p.patchBytes==256&&p.patchRanges==64&&p.clonedBytes==payload->data.materials.size()*sizeof(PackedMaterial),"Fragmented scalar material changes were not patched");
            while(patched.step()){}check(patched.readback().radiance==renderGpu(changed,small,device).radiance,"Sparse material upload differs from fresh raw film");
            while(resident.step()){}check(resident.readback().radiance==renderGpu(old,small,device).radiance,"Sparse clone / patch modified live old buffers");
            CpuScene dense(edited,{},cache,&changed);dense.environment=std::make_shared<Environment>(16,8,std::vector<glm::vec3>(128,glm::vec3(.7f,.2f,.5f)));
            GpuRenderSession full(dense,small,device,gpuCache);
            check(full.statistics().sceneBufferUploads==1&&full.preparationStatistics().patchBuffers==0&&full.preparationStatistics().clonedBytes==0,"Dense environment changes did not fall back to full upload");
            while(full.step()){}check(full.readback().radiance==renderGpu(dense,small,device).radiance,"Dense fallback changed raw film");
            edited.draws.push_back(edited.draws.back());edited.draws.back().objectId=65;CpuScene enlarged(edited,{},cache,&dense);enlarged.environment=dense.environment;
            GpuRenderSession resized(enlarged,small,device,gpuCache);
            while(resized.step()){}check(resized.readback().radiance==renderGpu(enlarged,small,device).radiance,"Resized scene buffers retained stale cloned bytes");
            std::cout<<"GPU sparse uploads: fragmented patches, partial clone / patch cancellation, old-session isolation, dense / size fallback and raw film passed\n";
        }
        input.draws[0].model=input.draws[1].model;CpuScene overlap(input,{},cache,&old),overlapFresh(input);overlap.environment=overlapFresh.environment=scene.environment;
        check(renderGpu(overlap,opts,device).radiance==renderGpu(overlapFresh,opts,device).radiance,"GPU coplanar nearest-hit ties depend on TLAS topology");
        Kernel hitProbe(overlap,opts,device);std::vector<Pixel> probes(size_t(opts.width)*opts.height);
        for(uint32_t i=0;i<probes.size();++i){probes[i].albedo=glm::vec4(glm::vec3(input.draws[i%64].model[3])+glm::vec3(0,0,2),0);probes[i].normal={0,0,-1,0};}
        hitProbe.uploadPixels(probes);hitProbe.dispatch(0,1,false,1);const auto &hits=hitProbe.read();
        for(uint32_t i=0;i<hits.size();++i){Surface hit;const bool found=overlap.intersect(glm::vec3(probes[i].albedo),{0,0,-1},1e-4f,100,hit,true);
            check((hits[i].m2.w>0)==found,"GPU refit returned a stale instance hit");
            if(found)check(glm::length(glm::vec3(hits[i].m2)-hit.albedo)<1e-5f&&std::abs(hits[i].mean.w-hit.distance)<1e-4f,"GPU refit/tied material and distance differ from CPU brute-force ray");
        }
        std::cout<<"GPU TLAS refit: multi-instance raw-film / tied-hit CPU parity passed\n";
    }
    auto compare=[&](const CpuScene &fixtureScene,const char *message){auto a=render(fixtureScene,options),b=renderGpu(fixtureScene,options,device);double error=0,energy=0;for(size_t i=0;i<a.radiance.size();++i){error+=glm::length(a.radiance[i]-b.radiance[i]);energy+=glm::length(a.radiance[i]);}check(error/std::max(energy,1e-9)<.03,message);check(energy>.01,"GPU lighting fixture has no energy");};
    for(uint32_t batch:{1u,4u,16u}){auto batched=options;batched.gpuBatchSamples=batch;auto image=renderGpu(scene,batched,device);check(image.radiance==gpu.radiance&&image.sampleCounts==gpu.sampleCounts&&image.rays==gpu.rays,"GPU batch size changed deterministic PT samples");}
    {
        GpuRenderSession session(scene,options,device);
        session.pause(true);check(!session.step()&&session.samples()==0,"Paused PT session advanced");
        session.pause(false);check(session.step()&&session.samples()==4,"PT session first checkpoint changed");
        session.pause(true);check(!session.step()&&session.samples()==4,"Paused PT film changed");session.pause(false);
        while(session.step()){}
        check(session.complete()&&session.statistics().readbacks==0,"PT steps unexpectedly read film to CPU");
        auto stepped=session.readback();check(stepped.radiance==gpu.radiance&&stepped.sampleCounts==gpu.sampleCounts&&stepped.rays==gpu.rays,"PT session differs from offline adapter");
        session.display(48,32,.8f);check(session.statistics().readbacks==1,"PT display read back the film");
        session.cancel();check(!session.step()&&session.cancelled(),"Cancelled PT session advanced");
        session.reset();check(!session.cancelled()&&!session.paused()&&session.samples()==0,"PT reset did not restart sequence");
        while(session.step()){}check(session.readback().radiance==gpu.radiance,"Reset PT session did not replay its original samples");session.reset();
        session.step();session.resize(16,24); // Retire film while preceding GPU work may still be in flight.
        auto blank=session.readback();check(blank.totalSamples==0&&blank.width==16&&blank.height==24,"Resized PT film retained old samples");
        auto resizedOptions=options;resizedOptions.width=16;resizedOptions.height=24;
        while(session.step()){}auto resized=session.readback();auto resizedReference=renderGpu(scene,resizedOptions,device);
        check(resized.radiance==resizedReference.radiance&&resized.sampleCounts==resizedReference.sampleCounts,"PT resize changed frozen-camera transport");
        bool rejected=false;try{session.resize(0,24);}catch(const std::invalid_argument&){rejected=true;}
        check(rejected&&session.samples()==options.samples,"Invalid PT resize changed live session");
    }
    {
        auto displayWorld=fixture();auto draw=triangle({-.4f,-.6f,0},{.85f,-.6f,0},{.6f,.65f,0});draw.parameters.emissiveNormal={2,.4f,.1f,0};displayWorld.draws.push_back(draw);
        CpuScene displayScene(displayWorld);displayScene.environment=scene.environment;auto displayOptions=options;displayOptions.samples=8;
        GpuRenderSession session(displayScene,displayOptions,device);while(session.step()){}
        auto output=session.display(32,32,.8f);auto rgba=device->readTexture(output);auto raw=session.readback();
        check(rgba.size()==32*32*4,"PT viewport readback extent changed");
        for(size_t i=0;i<raw.radiance.size();++i)for(int c=0;c<3;++c){const float expected=std::pow(1-std::exp(-std::max(raw.radiance[i][c],0.f)*.8f),1/2.2f)*255;check(std::abs(float(rgba[i*4+c])-expected)<=2,"GPU PT display differs from linear-film tone map / row orientation");}
        const auto reads=session.statistics().readbacks;session.display(32,32,2.f);check(session.samples()==8&&session.statistics().readbacks==reads,"Exposure reset or read back PT film");
        auto wide=device->readTexture(session.display(48,32,1.f));
        for(uint32_t y=0;y<32;++y)for(uint32_t x:{0u,47u})for(int c=0;c<3;++c)check(wide[(y*48+x)*4+c]==0,"Frozen PT camera was stretched instead of letterboxed");
        auto derived=raw;derived.denoised.assign(derived.radiance.size(),glm::vec3(.25f,.5f,1));
        session.queueDenoisedDisplay(derived);check(!session.uploadDenoisedDisplay(16,1)&&session.denoisedSamples()==0,"Partially uploaded denoised film was published");
        for(int i=0;!session.denoisedSamples()&&i<1000;++i)session.uploadDenoisedDisplay(1024,1);
        check(session.denoisedSamples()==8,"Denoised display upload stalled");auto filtered=device->readTexture(session.display(32,32,.8f));
        for(size_t i=0;i<derived.denoised.size();++i)for(int c=0;c<3;++c){const float expected=std::pow(1-std::exp(-derived.denoised[i][c]*.8f),1/2.2f)*255;check(std::abs(float(filtered[i*4+c])-expected)<=2,"Denoised display tone map / orientation differs");}
        check(device->readTexture(session.display(32,32,.8f,false))==rgba,"Raw display toggle retained denoised pixels");
        check(session.statistics().readbacks==reads,"Denoised upload or display read back raw film");auto unchanged=session.readback();check(unchanged.radiance==raw.radiance&&unchanged.sampleCounts==raw.sampleCounts&&unchanged.rays==raw.rays,"Denoised display modified raw film");
        session.reset();check(!session.denoisedSamples(),"Film reset retained stale denoised output");
        std::cout<<"GPU PT session: pause/resume/cancel/reset/resize, offline parity and direct display passed\n";
    }
    auto anyHitOptions=options;anyHitOptions.shadowAnyHit=false;auto closestShadows=renderGpu(scene,anyHitOptions,device);check(closestShadows.radiance==gpu.radiance,"GPU any-hit changed opaque shadow transport");
    CpuScene pool(makePoolCausticsScene(32,32).snapshot);pool.sunDirection=glm::normalize(glm::vec3(-.25f,1,.3f));pool.sunIrradiance=glm::vec3(4);pool.sunRadius=.01f;
    options.photonMapping=true;options.photonPaths=30000;options.photonRadius=.16f;compare(pool,"GPU pool photon gathering differs from shared CPU map");auto photonImage=renderGpu(pool,options,device);double causticEnergy=0;for(size_t i=0;i<photonImage.caustics.size();++i){causticEnergy+=photonImage.caustics[i].x;check(glm::all(glm::lessThanEqual(photonImage.caustics[i],photonImage.radiance[i]+glm::vec3(1e-4f))),"GPU photon caustics exceed complete image");}check(causticEnergy>1&&photonImage.causticPhotons>100,"GPU pool photon caustics AOV is empty");{
        auto photonOptions=options;photonOptions.samples=8;GpuRenderSession photonSession(pool,photonOptions,device);photonSession.step();
        const auto stored=photonSession.statistics().storedPhotons;const auto emitted=photonSession.statistics().photonRays;
        auto camera=makePoolCausticsScene(32,32).snapshot.frame;camera.cameraPosition.x+=.1f;camera.viewProjection=camera.viewProjection*glm::translate(glm::mat4(1),glm::vec3(-.1f,0,0));pool.updateCamera(camera);photonSession.updateCamera(pool,1);
        check(photonSession.statistics().sceneBufferUploads==0&&photonSession.statistics().storedPhotons==stored&&photonSession.statistics().photonRays==emitted,"Camera move discarded static photon map");
        while(photonSession.step()){}auto reference=renderGpu(pool,photonOptions,device);check(photonSession.readback().radiance==reference.radiance,"Camera-reused photon map differs from a fresh map");
        auto photonPayload=std::make_shared<GpuSceneUpload>(prepareGpuScene(pool,&photonOptions));
        GpuRenderSession staged(pool,photonOptions,device,{},std::shared_ptr<const GpuSceneUpload>(photonPayload));photonPayload.reset();
        for(int i=0;!staged.ready()&&i<1000;++i){staged.prepareStep(65536,1);check(staged.preparationStatistics().lastStepWorkBytes<=65536,"Photon upload exceeded budget");}
        check(staged.ready(),"Worker-prepared photon upload stalled");while(staged.step()){}auto result=staged.readback();
        check(result.radiance==reference.radiance&&result.caustics==reference.caustics&&result.storedPhotons==reference.storedPhotons,"Worker-prepared / chunked photon map changed caustics");
    }
    options.photonMapping=false;
    auto leafWorld=world;leafWorld.frame.lights.push_back({{0,0,-2,1},{2,3,4,0},{0,0,1,0}});leafWorld.frame.inverseSquareLocalLights=true;leafWorld.draws[0].pathTracingBsdfModel=3;leafWorld.draws[0].pathTracingDiffuseTransmission={.6f,.2f,.1f};leafWorld.draws[0].pathTracingTransmissionTexture=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{1,1,{200,150,100,255}});CpuScene leaf(leafWorld);leaf.environment=scene.environment;compare(leaf,"GPU thin diffuse transmission, backside NEE or texture differs from CPU");
    for(uint32_t model:{4u,5u}){
        auto fiberWorld=world;fiberWorld.draws[0].pathTracingBsdfModel=model;fiberWorld.draws[0].parameters.factors.x=0;
        fiberWorld.draws[0].pathTracingSheen={.6f,.4f,.3f,0};fiberWorld.draws[0].pathTracingSheenColor={.8f,.5f,.2f};fiberWorld.draws[0].pathTracingFiber={.35f,.3f,1.55f,.02f};
        auto fiberMesh=std::make_shared<render::MeshPayload>(*fiberWorld.draws[0].mesh);for(const auto& vertex:fiberMesh->vertices)fiberMesh->pathTracingTangents.emplace_back(1,0,0,1);fiberWorld.draws[0].mesh=fiberMesh;
        CpuScene fibers(fiberWorld);fibers.environment=scene.environment;compare(fibers,model==4?"GPU cloth sheen/rough diffuse/transmission differs from CPU":"GPU R/TT/TRT hair scattering, tangent or sphere PDF differs from CPU");
        if(model==4){
            fiberWorld.draws[0].pathTracingKnit={1,13,19,.12f};
            auto knitMesh=std::make_shared<render::MeshPayload>(*fiberMesh);for(size_t i=0;i<knitMesh->vertices.size();++i)knitMesh->vertices[i].uv={float(i)*.4f,float(i)*.3f};fiberWorld.draws[0].mesh=knitMesh;
            CpuScene knit(fiberWorld);knit.environment=scene.environment;compare(knit,"GPU knit UV pattern or directional yarn sheen differs from CPU");
        }
        if(model==5){
            for(float beta:{.1f,.8f}){
                fiberWorld.draws[0].pathTracingFiber={beta,beta,1.55f,.2f};
                auto edgeMesh=std::make_shared<render::MeshPayload>(*fiberMesh);for(auto& vertex:edgeMesh->vertices)vertex.uv.y=.9995f;fiberWorld.draws[0].mesh=edgeMesh;
                CpuScene edge(fiberWorld);edge.environment=scene.environment;compare(edge,"GPU narrow/broad tilted hair edge differs from CPU");
            }
        }
    }
    std::cout<<"GPU hair / cloth shared equations, tangent frame, reflection/transmission and PDF parity passed\n";
    auto grazingWorld=leafWorld;auto grazingMap=std::make_shared<render::MaterialPayload>();grazingMap->images[1]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{1,1,{240,128,170,255}});
    auto grazingMesh=std::make_shared<render::MeshPayload>(*grazingWorld.draws[0].mesh);for(size_t i=0;i<grazingMesh->vertices.size();++i)grazingMesh->pathTracingTangents.emplace_back(1,0,0,1);
    grazingWorld.draws[0].mesh=grazingMesh;grazingWorld.draws[0].material=grazingMap;grazingWorld.draws[0].parameters.emissiveNormal.w=1;
    for(auto model:{1u,2u,3u}){grazingWorld.draws[0].pathTracingBsdfModel=model;CpuScene grazing(grazingWorld);grazing.environment=scene.environment;compare(grazing,"GPU grazing diffuse/glossy/translucent normal, bump shadowing or mixture PDF differs from CPU");}
    CpuScene solarReflection(makeWaterSolarValidationScene(32,32).snapshot);solarReflection.sunDirection=glm::normalize(glm::vec3(0,.2f,1));solarReflection.sunIrradiance=glm::vec3(1);solarReflection.sunRadius=.01f;
    compare(solarReflection,"GPU water solar reflection continuation mixture differs from CPU");
    for(bool overlap:{false,true}){CpuScene thinReflection(makeThinSolarValidationScene(32,32,overlap).snapshot);thinReflection.sunDirection=solarReflection.sunDirection;thinReflection.sunIrradiance=solarReflection.sunIrradiance;thinReflection.sunRadius=solarReflection.sunRadius;compare(thinReflection,"GPU thin-sheet reflection/transmission or overlapping cone mixture differs from CPU");}
    auto importedWater=makeWaterSolarValidationScene(32,32).snapshot;importedWater.draws[0].pathTracingKind=0;CpuScene importedReflection(importedWater);importedReflection.sunDirection=solarReflection.sunDirection;importedReflection.sunIrradiance=solarReflection.sunIrradiance;importedReflection.sunRadius=solarReflection.sunRadius;compare(importedReflection,"GPU imported dielectric pool solar reflection mixture differs from CPU");
    auto mapped=std::make_shared<render::MaterialPayload>();mapped->images[1]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{1,1,{180,128,245,255}});mapped->images[2]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{1,1,{0,0,255,255}});mapped->images[3]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{1,1,{0,150,0,255}});
    world.draws[0].material=mapped;world.draws[0].parameters.factors={1,1,1,0};world.draws[0].parameters.emissiveNormal.w=1;CpuScene metal(world);metal.environment=scene.environment;compare(metal,"GPU normal mapping / metallic VNDF differs from CPU");
    for(uint32_t model:{0u,2u})for(float anisotropy:{-.8f,.8f}){
        auto shaped=world;shaped.draws[0].pathTracingBsdfModel=model;shaped.draws[0].pathTracingSpecularAnisotropy=anisotropy;shaped.draws[0].pathTracingSpecularRotation=.43f;shaped.draws[0].parameters.factors.y=.5f;
        auto tangent=std::make_shared<render::MeshPayload>(*shaped.draws[0].mesh);for(const auto& v:tangent->vertices)tangent->pathTracingTangents.emplace_back(glm::normalize(glm::vec3(1,0,0)-v.normal*v.normal.x),-1);shaped.draws[0].mesh=tangent;
        shaped.draws[0].model=glm::scale(glm::mat4(1),glm::vec3(-1.3f,.8f,1));CpuScene anisotropic(shaped);anisotropic.environment=scene.environment;compare(anisotropic,"GPU anisotropic GGX evaluation / VNDF / rotated mirrored frame differs from CPU");
    }

    world=fixture();world.draws.push_back(triangle({-20,-20,0},{20,-20,0},{0,20,0}));world.frame.lights.push_back({{0,0,1,1},{3,2,1,0},{0,0,-1,0}});world.frame.lights.push_back({{1,0,1,2},{1,2,3,.95f},{-.707f,0,-.707f,.6f}});world.frame.inverseSquareLocalLights=true;CpuScene local(world);compare(local,"GPU point / spot lighting differs from CPU");
    world.frame.lights.clear();auto emitter=triangle({-.5f,-.5f,1},{0,.5f,1},{.5f,-.5f,1});emitter.parameters.emissiveNormal={3,4,2,0};world.draws.push_back(emitter);CpuScene area(world);compare(area,"GPU emitter NEE / MIS differs from CPU");
    auto instancedLights=world;for(int i=0;i<4;++i){auto duplicate=emitter;duplicate.model=glm::translate(glm::mat4(1),glm::vec3((i-1.5f)*.6f,0,.3f))*glm::scale(glm::mat4(1),glm::vec3(i%2?-.4f:.5f,.7f,1.2f));duplicate.parameters.emissiveNormal=glm::vec4(1.f+i,2,1,0);instancedLights.draws.push_back(duplicate);}CpuScene sharedLights(instancedLights);check(sharedLights.accelerationStats().uniqueMeshes<sharedLights.meshCount(),"GPU emitter fixture did not share BLAS");compare(sharedLights,"GPU mirrored/shared emitter world area or material differs from CPU");
    auto mediumWorld=makeMediumValidationScene(32,32,true).snapshot;mediumWorld.frame=fixture().frame;auto medium=mediumWorld.draws[0];medium.pathTracingScattering=glm::vec3(0);medium.model=glm::translate(glm::mat4(1),glm::vec3(-1.2f,0,0))*glm::scale(glm::mat4(1),glm::vec3(.6f,1,1));auto mirrorMedium=medium;mirrorMedium.model=glm::translate(glm::mat4(1),glm::vec3(1.2f,0,0))*glm::scale(glm::mat4(1),glm::vec3(-.6f,1,1));mirrorMedium.pathTracingAbsorption={.8f,.05f,.1f};mediumWorld.draws={medium,mirrorMedium};CpuScene sharedMedia(mediumWorld);sharedMedia.environment=scene.environment;check(sharedMedia.accelerationStats().uniqueMeshes==1&&sharedMedia.media().size()==2,"GPU dielectric fixture lost independent instance media");compare(sharedMedia,"GPU shared mirrored dielectric medium transport differs from CPU");
    world.draws.pop_back();world.frame.lights.push_back({{0,0,0,0},{1,1,1,0},{0,0,-1,0}});CpuScene sun(world);sun.environment=std::make_shared<Environment>(16,8,std::vector<glm::vec3>(128,glm::vec3(.2f)));sun.sunRadius=.005f;sun.sunDirection={0,0,1};sun.sunIrradiance={1,2,3};compare(sun,"GPU finite sun / environment MIS differs from CPU");
    auto mask=std::make_shared<render::MaterialPayload>();mask->images[0]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{2,1,{255,255,255,255,255,255,255,0}});world.draws[0].material=mask;world.draws[0].parameters.factors.w=.5f;CpuScene cutout(world);cutout.environment=scene.environment;compare(cutout,"GPU primary / shadow alpha mask differs from CPU");
    CpuScene empty(fixture());empty.environment=std::make_shared<Environment>(8,4,std::vector<glm::vec3>(32,glm::vec3(.5f)));options.samples=256;options.adaptive=true;auto adaptive=renderGpu(empty,options,device);check(adaptive.convergedPixels==1024 && adaptive.totalSamples<uint64_t(1024)*256,"GPU adaptive pixels did not stop");for(auto value:adaptive.radiance)check(glm::length(value-glm::vec3(.5f))<1e-6f,"GPU adaptive mean changed constant environment");
    options.samples=512;options.adaptive=false;options.guiding=true;options.trainingSamples=64;options.guideCellSize=100;
    auto guided=renderGpu(scene,options,device);check(guided.trainedCells>0&&guided.guideHits>0,"GPU guiding did not learn or use a proposal");double baseEnergy=0,guidedEnergy=0;for(auto value:gpu.radiance)baseEnergy+=glm::length(value);for(auto value:guided.radiance)guidedEnergy+=glm::length(value);check(std::abs(guidedEnergy/baseEnergy-1)<.04,"GPU mixture PDF changed transport energy");
    {
        auto payload=std::make_shared<GpuSceneUpload>(prepareGpuScene(scene,&options));
        GpuRenderSession staged(scene,options,device,{},std::shared_ptr<const GpuSceneUpload>(payload));payload.reset();
        for(int i=0;!staged.ready()&&i<1000;++i){staged.prepareStep(1024*1024,1);check(staged.preparationStatistics().lastStepWorkBytes<=1024*1024,"Guide / training initialization exceeded budget");}
        check(staged.ready(),"Chunked guiding preparation stalled");while(staged.step()){}auto image=staged.readback();
        check(image.radiance==guided.radiance&&image.trainingRays==guided.trainingRays&&image.trainedCells==guided.trainedCells,"Chunked guiding initialization changed trained film");
    }
    world=fixture();world.draws.push_back(triangle({-20,-20,0},{20,-20,0},{0,20,0}));world.draws.push_back(triangle({-20,-20,3},{0,20,3},{20,-20,3}));world.frame.lights.push_back({{0,0,2,1},{2,2,2,0},{0,0,-1,0}});CpuScene cacheScene(world);cacheScene.environment=scene.environment;
    options.radianceCache=true;options.cacheDepth=1;options.cacheMinimum=16;auto cached=renderGpu(cacheScene,options,device);check(cached.nonFiniteSamples==0&&cached.cacheHits>0,"GPU cache was not exercised");
    options.guiding=false;options.radianceCache=false;options.samples=128;options.maxDepth=6;auto validation=makeCausticsScene(32,32);CpuScene glassScene(validation.snapshot,validation.dielectrics);compare(glassScene,"GPU smooth dielectric transport differs from CPU");
    options.guiding=true;options.radianceCache=true;options.adaptive=true;options.samples=256;auto fallback=renderGpu(empty,options,device);check(fallback.trainedCells==0&&fallback.guideHits==0&&fallback.cacheHits==0,"Empty training table must fall back");for(auto value:fallback.radiance)check(glm::length(value-glm::vec3(.5f))<1e-6f,"Empty guiding proposal changed constant energy");
    std::cout<<"GPU PT: brute-force/BVH, texture alpha, CPU-identical Sobol, VNDF/MIS and adaptive tests passed\n";
}
} // namespace pt
