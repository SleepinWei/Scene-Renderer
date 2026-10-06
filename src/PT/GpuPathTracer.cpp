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
#include <stdexcept>
namespace pt {
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
};
struct alignas(16) Pixel {glm::vec4 mean{0},m2{0};glm::uvec4 stats{0};glm::vec4 albedo{0},normal{0};};
static_assert(sizeof(Parameters)==464 && sizeof(Pixel)==80,"GPU path tracing ABI");
struct alignas(16) GuideCell {glm::uvec4 meta{0};glm::vec4 tail{0};std::array<float,64> cdf{},environmentCdf{};};
static_assert(sizeof(GuideCell)==544,"Guide cell ABI");
class Kernel {
  public:
    Kernel(const CpuScene &scene,const Options &options,std::shared_ptr<rhi::GraphicsDevice> device):resources_(std::move(device)) {
        validateOptions(options);
        if(options.bdpt)throw std::invalid_argument("GPU PT does not implement BDPT; use the CPU BDPT reference");
        if(!resources_.device->computeLimits().supported || resources_.device->backend()==rhi::Backend::OpenGL)throw std::invalid_argument("GPU PT requires Metal or Vulkan compute");
        if(!options.sobol)throw std::invalid_argument("GPU PT uses Sobol; use CPU for the PCG reference");
        if(scene.scatteringCount()&&(options.guiding||options.radianceCache))throw std::invalid_argument("GPU volume PT requires guiding/cache disabled");
        if(options.photonMapping)photons_=std::make_unique<PhotonMap>(scene,options);
        parameters_.photonControl={options.photonMapping?1u:0u,photons_?uint32_t(photons_->cells().size()):0u,options.photonPaths,0u};
        if(photons_)parameters_.photonSettings={options.photonRadius,2.f/(3.14159265358979323846f*options.photonRadius*options.photonRadius*options.photonPaths),.9f,.1f};
        const auto exportStarted=std::chrono::steady_clock::now();auto data=scene.exportData();exportSeconds_=std::chrono::duration<double>(std::chrono::steady_clock::now()-exportStarted).count();parameters_.acceleration.x=uint32_t(data.instances.size());parameters_.acceleration.y=uint32_t(std::count_if(data.materials.begin(),data.materials.end(),[](const auto &m){return m.optics.x>0&&(m.extra.w&1u)==0;}));
        parameters_.cameraMedia=data.cameraMedia;parameters_.cameraWinding=data.cameraWinding;parameters_.inverseProjection=data.inverseProjection;parameters_.camera=glm::vec4(data.camera,0);
        parameters_.acceleration.w=options.shadowAnyHit?(scene.opaqueShadows()?3u:1u):0u;
        parameters_.acceleration.z=uint32_t(scene.thinSolarNormals().size());for(size_t i=0;i<scene.thinSolarNormals().size();++i)parameters_.thinSolarNormals[i]=glm::vec4(scene.thinSolarNormals()[i],0);
        const uint32_t highSeed=uint32_t(options.seed>>32);std::memcpy(&parameters_.camera.w,&highSeed,4);
        parameters_.sunDirectionRadius=glm::vec4(scene.sunDirection,scene.sunRadius);parameters_.sunIrradiance=glm::vec4(scene.sunIrradiance,0);
        parameters_.dimensions={options.width,options.height,0,0};parameters_.counts={uint32_t(data.nodes.size()),uint32_t(data.lights.size()),scene.environment?scene.environment->width():0,scene.environment?scene.environment->height():0};
        parameters_.control={options.maxDepth,uint32_t(options.seed),0,uint32_t(data.emitters.size())};parameters_.settings={float(data.emitterWeight),options.relativeError,options.absoluteError,data.inverseSquare?1.f:0.f};parameters_.adaptive={options.adaptive?1:0,options.minimumSamples,options.samples,1};
        parameters_.learning={options.guiding||options.radianceCache?16384u:0u,(options.guiding?1u:0u)|(options.radianceCache?2u:0u)|(options.waterSunProposal?0u:4u)|(options.thinSunProposal?0u:8u),options.cacheMinimum,options.cacheDepth};
        float extent=1;if(!data.nodes.empty()){auto size=glm::vec3(data.nodes[0].high-data.nodes[0].low);extent=std::max({size.x,size.y,size.z,1.f});}
        parameters_.guideSettings={options.guideCellSize>0?options.guideCellSize:extent/48,.35f,.5f,scene.scatteringCount()?1.f:0.f};
        using namespace rhi;
        const auto path=defaultShaderDirectory()+"/path-trace.comp";
        ComputePipelineDesc desc;desc.shader={path+".glsl",path+".metallib",path+".spv",path+".json","main0"};desc.threads={8,8,1};desc.label="Portable GPU path tracing";
        desc.bindings={{0,{{0,BindingType::UniformBuffer,ShaderStage::Compute,"Parameters",sizeof(Parameters)},
                          {1,BindingType::StorageRead,ShaderStage::Compute,"Nodes",0},{2,BindingType::StorageRead,ShaderStage::Compute,"Vertices",0},
                          {3,BindingType::StorageRead,ShaderStage::Compute,"Triangles",0},{4,BindingType::StorageRead,ShaderStage::Compute,"Materials",0},
                          {5,BindingType::StorageRead,ShaderStage::Compute,"Texels",0},
                          {6,BindingType::StorageRead,ShaderStage::Compute,"Instances",0},{7,BindingType::StorageRead,ShaderStage::Compute,"InstanceOrder",0}}},
                       {1,{{0,BindingType::StorageRead,ShaderStage::Compute,"Images",0},{1,BindingType::StorageRead,ShaderStage::Compute,"Environment",0},
                          {2,BindingType::StorageRead,ShaderStage::Compute,"Lights",0},{3,BindingType::StorageRead,ShaderStage::Compute,"Emitters",0},
                          {4,BindingType::StorageReadWrite,ShaderStage::Compute,"Pixels",0},{5,BindingType::StorageRead,ShaderStage::Compute,"Guide",0},{6,BindingType::StorageReadWrite,ShaderStage::Compute,"Training",0}}},
                       {2,{{0,BindingType::StorageRead,ShaderStage::Compute,"Photons",0},{1,BindingType::StorageRead,ShaderStage::Compute,"PhotonCells",0}}}};
        pipeline_=resources_.computePipeline(desc);
        std::vector<BindingEntry> geometry,lighting,photonEntries;
        memoryBytes_=sizeof(Parameters);
        parameterBuffer_=resources_.buffer({sizeof(Parameters),BufferUsage::Uniform|BufferUsage::CopyDestination,"PT parameters"},&parameters_);geometry.push_back({0,parameterBuffer_,0,sizeof(Parameters),{},{}});
        auto add=[&](const auto &values,std::vector<BindingEntry> &entries,uint32_t binding,const char *name){
            using Value=typename std::decay_t<decltype(values)>::value_type;
            const Value dummy{};const size_t bytes=std::max(values.size(),size_t(1))*sizeof(Value);
            if(bytes>resources_.device->computeLimits().maxStorageRange)throw std::length_error(std::string("GPU PT buffer exceeds device storage range: ")+name);
            memoryBytes_+=bytes;
            auto buffer=resources_.buffer({bytes,BufferUsage::Storage,name},values.empty()?static_cast<const void*>(&dummy):static_cast<const void*>(values.data()));entries.push_back({binding,buffer,0,bytes,{},{}});
        };
        add(data.nodes,geometry,1,"PT BVH");add(data.vertices,geometry,2,"PT vertices");add(data.triangles,geometry,3,"PT triangles");add(data.materials,geometry,4,"PT materials");add(data.texels,geometry,5,"PT texels");
        add(data.instances,geometry,6,"PT instances");add(data.instanceOrder,geometry,7,"PT TLAS instance order");
        add(data.images,lighting,0,"PT image descriptors");
        std::vector<glm::vec4> environment;if(scene.environment){environment.reserve(scene.environment->pixels().size());const auto &cdf=scene.environment->cumulative();for(size_t i=0;i<cdf.size();++i)environment.emplace_back(scene.environment->pixels()[i],float(cdf[i]/scene.environment->totalWeight()));environment.back().w=1;}
        add(environment,lighting,1,"PT HDR environment");add(data.lights,lighting,2,"PT lights");add(data.emitters,lighting,3,"PT emitters");
        pixels_.resize(size_t(options.width)*options.height);
        const size_t pixelBytes=pixels_.size()*sizeof(Pixel);if(pixelBytes>resources_.device->computeLimits().maxStorageRange)throw std::length_error("GPU PT output exceeds device storage range");
        memoryBytes_+=pixelBytes;
        pixelBuffer_=resources_.buffer({pixelBytes,BufferUsage::Storage|BufferUsage::CopySource|BufferUsage::CopyDestination,"PT progressive pixels"},pixels_.data());lighting.push_back({4,pixelBuffer_,0,pixelBytes,{},{}});
        const size_t capacity=std::max(1u,parameters_.learning.x);guide_.resize(capacity);training_.resize(capacity*133,0);
        const size_t guideBytes=guide_.size()*sizeof(GuideCell),trainBytes=training_.size()*sizeof(uint32_t);memoryBytes_+=guideBytes+trainBytes;
        guideBuffer_=resources_.buffer({guideBytes,BufferUsage::Storage|BufferUsage::CopyDestination,"PT frozen guiding/cache"},guide_.data());
        trainingBuffer_=resources_.buffer({trainBytes,BufferUsage::Storage|BufferUsage::CopySource,"PT integer training"},training_.data());
        lighting.push_back({5,guideBuffer_,0,guideBytes,{},{}});lighting.push_back({6,trainingBuffer_,0,trainBytes,{},{}});
        const std::vector<Photon> noPhotons;const std::vector<PhotonCell> noCells;
        add(photons_?photons_->photons():noPhotons,photonEntries,0,"PT frozen photon vertices");add(photons_?photons_->cells():noCells,photonEntries,1,"PT sorted photon cells");
        sets_={resources_.bindings({desc.bindings[0],geometry}),resources_.bindings({desc.bindings[1],lighting}),resources_.bindings({desc.bindings[2],photonEntries})};
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
        commands.dispatch(pipeline_,sets_,{(parameters_.dimensions.x+7)/8,(parameters_.dimensions.y+7)/8,1});resources_.device->submit(commands);
    }
    const std::vector<Pixel> &read() {resources_.device->readBuffer(pixelBuffer_,0,pixels_.size()*sizeof(Pixel),pixels_.data());return pixels_;}
    void uploadPixels(const std::vector<Pixel> &pixels) {if(pixels.size()!=pixels_.size())throw std::invalid_argument("GPU PT probe extent mismatch");resources_.device->writeBuffer(pixelBuffer_,0,pixels.size()*sizeof(Pixel),pixels.data());}
  private:
    std::unique_ptr<PhotonMap> photons_;
    uint64_t memoryBytes_=0;
    double exportSeconds_=0;
    render::Resources resources_;
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
Image renderGpu(const CpuScene &scene,const Options &options,std::shared_ptr<rhi::GraphicsDevice> device,const std::function<void(const Image &)> &progress) {
    const auto setup=std::chrono::steady_clock::now();Kernel kernel(scene,options,device);Image image;image.setupSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-setup).count();image.sceneExportSeconds=kernel.exportSeconds();image.gpuBufferBytes=kernel.memoryBytes();image.width=options.width;image.height=options.height;image.execution=device->backend()==rhi::Backend::Metal?"GPU Metal compute BLAS/TLAS":"GPU Vulkan compute BLAS/TLAS";
    const size_t count=size_t(options.width)*options.height;image.radiance.resize(count);image.albedo.resize(count);image.normal.resize(count);image.sampleCounts.resize(count);
    kernel.photonStats(image);if(options.photonMapping){image.caustics.resize(count);image.execution+=" + photon density indirect (CPU emission)";}
    kernel.train(options,image);
    const auto started=std::chrono::steady_clock::now();
    for(uint32_t first=0;first<options.samples;) {
        const uint32_t interval=options.adaptive?32:options.checkpointSamples;
        const uint32_t end=std::min(options.samples,first<4?4u:first<16?16u:uint32_t(std::min(uint64_t(options.samples),uint64_t(first)+interval)));
        auto stage=std::chrono::steady_clock::now();for(uint32_t begin=first;begin<end;){const uint32_t next=std::min(end,begin+options.gpuBatchSamples);kernel.dispatch(begin,next,options.adaptive&&next==end);++image.dispatches;begin=next;}image.dispatchSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-stage).count();
        stage=std::chrono::steady_clock::now();const auto &pixels=kernel.read();image.readbackSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-stage).count();++image.readbacks;image.readbackBytes+=pixels.size()*sizeof(Pixel);
        stage=std::chrono::steady_clock::now();image.rays=0;image.nonFiniteSamples=0;image.totalSamples=0;image.convergedPixels=0;image.guideHits=0;image.cacheHits=0;image.volumeEvents=0;
        for(size_t i=0;i<count;++i){if(options.photonMapping)image.caustics[i]={pixels[i].mean.w,pixels[i].m2.w,pixels[i].albedo.w};image.radiance[i]=glm::vec3(pixels[i].mean);image.albedo[i]=glm::vec3(pixels[i].albedo);image.normal[i]=glm::vec3(pixels[i].normal);image.sampleCounts[i]=pixels[i].stats.x;image.totalSamples+=pixels[i].stats.x;image.rays+=pixels[i].stats.z;image.nonFiniteSamples+=pixels[i].stats.w;image.convergedPixels+=pixels[i].stats.y>=2;if(!options.photonMapping){image.guideHits+=uint64_t(pixels[i].mean.w);image.cacheHits+=uint64_t(pixels[i].m2.w);}image.volumeEvents+=uint64_t(pixels[i].normal.w);}
        image.filmUpdateSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-stage).count();if(image.nonFiniteSamples)throw std::runtime_error("GPU PT non-finite path contribution");image.samples=end;image.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        std::cout<<image.execution<<": "<<end<<" spp budget, "<<double(image.totalSamples)/count<<" average spp, "<<image.seconds<<" s, "<<image.rays<<" rays\n"<<std::flush;
        if(progress){stage=std::chrono::steady_clock::now();progress(image);image.checkpointSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-stage).count();}first=end;if(image.convergedPixels==count)break;
    }
    image.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    return image;
}
void validateGpuPathTracing(std::shared_ptr<rhi::GraphicsDevice> device) {
    if(device->backend()==rhi::Backend::OpenGL || !device->computeLimits().supported)return;
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
    probe.dispatch(0,1,false,2);const auto samples=probe.read();
    const uint32_t probeDimensions[]={0,258,514,24578};
    for(uint32_t i=0;i<samples.size();++i){uint32_t pixelSeed=sampleHash(1^sampleHash(0)^sampleHash(i));for(uint32_t d=0;d<4;++d)check(samples[i].mean[d]==sobolSample(pixelSeed,i,probeDimensions[d]),"GPU Sobol sequence differs from CPU");}
    world=fixture();world.draws.push_back(triangle({-20,-20,0},{20,-20,0},{0,20,0}));world.frame.lights.push_back({{0,0,0,0},{2,2,2,0},{0,0,-1,0}});CpuScene scene(world);scene.environment=std::make_shared<Environment>(16,8,std::vector<glm::vec3>(128,glm::vec3(.3f)));
    Options options;options.width=32;options.height=32;options.samples=128;options.maxDepth=4;options.threads=1;options.adaptive=false;
    const auto cpu=render(scene,options),gpu=renderGpu(scene,options,device);double error=0,energy=0;
    for(size_t i=0;i<cpu.radiance.size();++i){check(glm::length(cpu.albedo[i]-gpu.albedo[i])<1e-5f,"GPU albedo AOV changed");error+=glm::length(cpu.radiance[i]-gpu.radiance[i]);energy+=glm::length(cpu.radiance[i]);}
    check(error/std::max(energy,1e-9)<.02,"GPU VNDF/MIS transport differs from CPU reference");
    options.checkpointSamples=32;auto frequent=renderGpu(scene,options,device);check(frequent.radiance==gpu.radiance&&frequent.sampleCounts==gpu.sampleCounts&&frequent.rays==gpu.rays,"GPU fixed-spp checkpoint frequency changed transport");check(frequent.readbacks>gpu.readbacks,"GPU checkpoint interval did not reduce readback count");options.checkpointSamples=256;
    auto compare=[&](const CpuScene &fixtureScene,const char *message){auto a=render(fixtureScene,options),b=renderGpu(fixtureScene,options,device);double error=0,energy=0;for(size_t i=0;i<a.radiance.size();++i){error+=glm::length(a.radiance[i]-b.radiance[i]);energy+=glm::length(a.radiance[i]);}check(error/std::max(energy,1e-9)<.03,message);check(energy>.01,"GPU lighting fixture has no energy");};
    for(uint32_t batch:{1u,4u,16u}){auto batched=options;batched.gpuBatchSamples=batch;auto image=renderGpu(scene,batched,device);check(image.radiance==gpu.radiance&&image.sampleCounts==gpu.sampleCounts&&image.rays==gpu.rays,"GPU batch size changed deterministic PT samples");}
    auto anyHitOptions=options;anyHitOptions.shadowAnyHit=false;auto closestShadows=renderGpu(scene,anyHitOptions,device);check(closestShadows.radiance==gpu.radiance,"GPU any-hit changed opaque shadow transport");
    CpuScene pool(makePoolCausticsScene(32,32).snapshot);pool.sunDirection=glm::normalize(glm::vec3(-.25f,1,.3f));pool.sunIrradiance=glm::vec3(4);pool.sunRadius=.01f;
    options.photonMapping=true;options.photonPaths=30000;options.photonRadius=.16f;compare(pool,"GPU pool photon gathering differs from shared CPU map");auto photonImage=renderGpu(pool,options,device);double causticEnergy=0;for(size_t i=0;i<photonImage.caustics.size();++i){causticEnergy+=photonImage.caustics[i].x;check(glm::all(glm::lessThanEqual(photonImage.caustics[i],photonImage.radiance[i]+glm::vec3(1e-4f))),"GPU photon caustics exceed complete image");}check(causticEnergy>1&&photonImage.causticPhotons>100,"GPU pool photon caustics AOV is empty");options.photonMapping=false;
    auto leafWorld=world;leafWorld.frame.lights.push_back({{0,0,-2,1},{2,3,4,0},{0,0,1,0}});leafWorld.frame.inverseSquareLocalLights=true;leafWorld.draws[0].pathTracingBsdfModel=3;leafWorld.draws[0].pathTracingDiffuseTransmission={.6f,.2f,.1f};leafWorld.draws[0].pathTracingTransmissionTexture=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{1,1,{200,150,100,255}});CpuScene leaf(leafWorld);leaf.environment=scene.environment;compare(leaf,"GPU thin diffuse transmission, backside NEE or texture differs from CPU");
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
    world=fixture();world.draws.push_back(triangle({-20,-20,0},{20,-20,0},{0,20,0}));world.frame.lights.push_back({{0,0,1,1},{3,2,1,0},{0,0,-1,0}});world.frame.lights.push_back({{1,0,1,2},{1,2,3,.95f},{-.707f,0,-.707f,.6f}});world.frame.inverseSquareLocalLights=true;CpuScene local(world);compare(local,"GPU point / spot lighting differs from CPU");
    world.frame.lights.clear();auto emitter=triangle({-.5f,-.5f,1},{0,.5f,1},{.5f,-.5f,1});emitter.parameters.emissiveNormal={3,4,2,0};world.draws.push_back(emitter);CpuScene area(world);compare(area,"GPU emitter NEE / MIS differs from CPU");
    auto instancedLights=world;for(int i=0;i<4;++i){auto duplicate=emitter;duplicate.model=glm::translate(glm::mat4(1),glm::vec3((i-1.5f)*.6f,0,.3f))*glm::scale(glm::mat4(1),glm::vec3(i%2?-.4f:.5f,.7f,1.2f));duplicate.parameters.emissiveNormal=glm::vec4(1.f+i,2,1,0);instancedLights.draws.push_back(duplicate);}CpuScene sharedLights(instancedLights);check(sharedLights.accelerationStats().uniqueMeshes<sharedLights.meshCount(),"GPU emitter fixture did not share BLAS");compare(sharedLights,"GPU mirrored/shared emitter world area or material differs from CPU");
    auto mediumWorld=makeMediumValidationScene(32,32,true).snapshot;mediumWorld.frame=fixture().frame;auto medium=mediumWorld.draws[0];medium.pathTracingScattering=glm::vec3(0);medium.model=glm::translate(glm::mat4(1),glm::vec3(-1.2f,0,0))*glm::scale(glm::mat4(1),glm::vec3(.6f,1,1));auto mirrorMedium=medium;mirrorMedium.model=glm::translate(glm::mat4(1),glm::vec3(1.2f,0,0))*glm::scale(glm::mat4(1),glm::vec3(-.6f,1,1));mirrorMedium.pathTracingAbsorption={.8f,.05f,.1f};mediumWorld.draws={medium,mirrorMedium};CpuScene sharedMedia(mediumWorld);sharedMedia.environment=scene.environment;check(sharedMedia.accelerationStats().uniqueMeshes==1&&sharedMedia.media().size()==2,"GPU dielectric fixture lost independent instance media");compare(sharedMedia,"GPU shared mirrored dielectric medium transport differs from CPU");
    world.draws.pop_back();world.frame.lights.push_back({{0,0,0,0},{1,1,1,0},{0,0,-1,0}});CpuScene sun(world);sun.environment=std::make_shared<Environment>(16,8,std::vector<glm::vec3>(128,glm::vec3(.2f)));sun.sunRadius=.005f;sun.sunDirection={0,0,1};sun.sunIrradiance={1,2,3};compare(sun,"GPU finite sun / environment MIS differs from CPU");
    auto mask=std::make_shared<render::MaterialPayload>();mask->images[0]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{2,1,{255,255,255,255,255,255,255,0}});world.draws[0].material=mask;world.draws[0].parameters.factors.w=.5f;CpuScene cutout(world);cutout.environment=scene.environment;compare(cutout,"GPU primary / shadow alpha mask differs from CPU");
    CpuScene empty(fixture());empty.environment=std::make_shared<Environment>(8,4,std::vector<glm::vec3>(32,glm::vec3(.5f)));options.samples=256;options.adaptive=true;auto adaptive=renderGpu(empty,options,device);check(adaptive.convergedPixels==1024 && adaptive.totalSamples<uint64_t(1024)*256,"GPU adaptive pixels did not stop");for(auto value:adaptive.radiance)check(glm::length(value-glm::vec3(.5f))<1e-6f,"GPU adaptive mean changed constant environment");
    options.samples=512;options.adaptive=false;options.guiding=true;options.trainingSamples=64;options.guideCellSize=100;
    auto guided=renderGpu(scene,options,device);check(guided.trainedCells>0&&guided.guideHits>0,"GPU guiding did not learn or use a proposal");double baseEnergy=0,guidedEnergy=0;for(auto value:gpu.radiance)baseEnergy+=glm::length(value);for(auto value:guided.radiance)guidedEnergy+=glm::length(value);check(std::abs(guidedEnergy/baseEnergy-1)<.04,"GPU mixture PDF changed transport energy");
    world=fixture();world.draws.push_back(triangle({-20,-20,0},{20,-20,0},{0,20,0}));world.draws.push_back(triangle({-20,-20,3},{0,20,3},{20,-20,3}));world.frame.lights.push_back({{0,0,2,1},{2,2,2,0},{0,0,-1,0}});CpuScene cacheScene(world);cacheScene.environment=scene.environment;
    options.radianceCache=true;options.cacheDepth=1;options.cacheMinimum=16;auto cached=renderGpu(cacheScene,options,device);check(cached.nonFiniteSamples==0&&cached.cacheHits>0,"GPU cache was not exercised");
    options.guiding=false;options.radianceCache=false;options.samples=128;options.maxDepth=6;auto validation=makeCausticsScene(32,32);CpuScene glassScene(validation.snapshot,validation.dielectrics);compare(glassScene,"GPU smooth dielectric transport differs from CPU");
    options.guiding=true;options.radianceCache=true;options.adaptive=true;options.samples=256;auto fallback=renderGpu(empty,options,device);check(fallback.trainedCells==0&&fallback.guideHits==0&&fallback.cacheHits==0,"Empty training table must fall back");for(auto value:fallback.radiance)check(glm::length(value-glm::vec3(.5f))<1e-6f,"Empty guiding proposal changed constant energy");
    std::cout<<"GPU PT: brute-force/BVH, texture alpha, CPU-identical Sobol, VNDF/MIS and adaptive tests passed\n";
}
} // namespace pt
