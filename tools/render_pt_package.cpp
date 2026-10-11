// Render a frozen PT package without the real-time scene, editor, or window loop.
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>
#include "PT/ScenePackage.h"
#include "PT/ValidationScenes.h"
#include "PT/GpuPathTracer.h"
#include "PT/Denoiser.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <algorithm>
#include <cmath>
#if defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#endif
namespace {
uint32_t number(const std::string &text,uint32_t maximum){size_t end=0;auto value=std::stoull(text,&end);if(end!=text.size()||text.empty()||text[0]=='-'||!value||value>maximum)throw std::invalid_argument("Invalid positive integer: "+text);return uint32_t(value);}
std::shared_ptr<rhi::GraphicsDevice> device(const std::string &backend){
#ifdef SCENERENDERER_METAL
    if(backend=="Metal")return rhi::makeMetalDevice();
#endif
#ifdef SCENERENDERER_HAS_VULKAN
    if(backend=="Vulkan")return rhi::makeVulkanDevice();
#endif
    throw std::invalid_argument("Backend unavailable in this build: "+backend);
}
nlohmann::json processPeakRss() {
#if defined(__APPLE__) || defined(__linux__)
    struct rusage usage{};
    if(getrusage(RUSAGE_SELF,&usage))throw std::runtime_error("Cannot read process peak RSS");
    uint64_t peak=uint64_t(usage.ru_maxrss);
#ifdef __linux__
    peak*=1024; // Linux reports KiB; macOS reports bytes.
#endif
    return peak;
#else
    return nullptr;
#endif
}
// Run sequentially on the device owner thread. Cache comparisons and initial
// film writes count toward the same preparation budget used by the viewport.
void benchmarkPreparation(const pt::ScenePackage &package,const pt::Options &options,
                          const std::string &backend,const std::string &input,const std::string &output) {
    if(backend=="CPU")throw std::invalid_argument("Preparation benchmark requires Metal or Vulkan");
    if(options.photonMapping||options.sppm)throw std::invalid_argument("Preparation benchmark excludes photon rebuilds; use ordinary rendering for photon measurements");
    using Clock=std::chrono::steady_clock;
    auto gpu=device(backend);auto cache=std::make_shared<pt::GpuSceneCache>(gpu);
    auto geometry=std::make_shared<pt::GeometryCache>();
    auto configure=[&](pt::CpuScene &scene){scene.environment=package.environment;scene.sunDirection=package.sunDirection;scene.sunIrradiance=package.sunIrradiance;scene.sunRadius=package.sunRadius;};
    auto report=nlohmann::json{{"backend",backend},{"scene",input},{"scene_package",package.metadata},{"width",options.width},{"height",options.height},{"samples",options.samples},{"seed",options.seed},{"max_depth",options.maxDepth},{"byte_budget",4*1024*1024},{"seconds_budget",.004},{"scope","sequential preparation API calls; no editor loop or UI latency measurement; cold means fresh device/cache, driver cache state uncontrolled"}};
    report["operations"]=nlohmann::json::array();
    auto run=[&](const char *name,const render::RenderWorldSnapshot &world,const pt::CpuScene *previous){
        auto start=Clock::now();auto scene=std::make_unique<pt::CpuScene>(world,std::vector<pt::DielectricMaterial>{},geometry,previous);configure(*scene);
        const double cpu=std::chrono::duration<double>(Clock::now()-start).count();
        auto payload=std::make_shared<pt::GpuSceneUpload>(pt::prepareGpuScene(*scene,&options));
        const auto &data=payload->data;const auto tlasBytes=data.tlasNodes.size()*sizeof(pt::PackedNode),blasBytes=data.geometry->blasNodes.size()*sizeof(pt::PackedNode),instanceBytes=data.instances.size()*sizeof(pt::PackedInstance);
        pt::Image image;uint64_t maxWork=0;double maxCall=0,wall=0;
        pt::GpuPreparationStats prep;
        {
            start=Clock::now();pt::GpuRenderSession session(*scene,options,gpu,cache,std::shared_ptr<const pt::GpuSceneUpload>(payload));payload.reset();
            while(!session.ready()){
                auto call=Clock::now();session.prepareStep(4*1024*1024,.004);
                maxCall=std::max(maxCall,std::chrono::duration<double>(Clock::now()-call).count());maxWork=std::max(maxWork,session.preparationStatistics().lastStepWorkBytes);
                if(maxWork>4*1024*1024)throw std::runtime_error("Preparation exceeded byte budget");
            }
            wall=std::chrono::duration<double>(Clock::now()-start).count();prep=session.preparationStatistics();
            while(session.step()){}image=session.readback();
        }
        for(auto value:image.radiance)if(!std::isfinite(value.x)||!std::isfinite(value.y)||!std::isfinite(value.z))throw std::runtime_error("Preparation benchmark produced a nonfinite film");
        auto a=scene->accelerationStats();
        report["operations"].push_back({
            {"name",name},{"cpu_scene_seconds",cpu},{"cpu_export_seconds",image.sceneExportSeconds},
            {"geometry_export_reused",image.sceneGeometryExportReused},{"texture_export_reused",image.sceneTextureExportReused},
            {"alpha_image_scans",a.alphaImageScans},{"alpha_pixels_scanned",a.alphaPixelsScanned},
            {"preparation_wall_seconds",wall},{"pipeline_seconds",prep.pipelineSeconds},
            {"allocation_seconds",prep.allocationSeconds},{"cache_compare_seconds",prep.cacheCompareSeconds},
            {"upload_seconds",prep.uploadSeconds},{"finalize_seconds",prep.finalizeSeconds},
            {"identity_cache_hits",prep.identityCacheHits},{"identity_cache_bytes",prep.identityCacheBytes},
            {"prepare_calls",prep.steps},{"max_prepare_call_seconds",maxCall},{"max_prepare_work_bytes",maxWork},
            {"scene_uploads",image.sceneBufferUploads},{"scene_cache_hits",image.sceneBufferCacheHits},
            {"gpu_cloned_bytes",prep.clonedBytes},{"gpu_clone_seconds",prep.cloneSeconds},{"patch_bytes",prep.patchBytes},{"patch_buffers",prep.patchBuffers},{"patch_ranges",prep.patchRanges},{"scene_upload_bytes",image.sceneUploadBytes},{"total_transferred_bytes",prep.transferredBytes},
            {"tlas_bytes",tlasBytes},{"blas_bytes",blasBytes},{"instance_bytes",instanceBytes},
            {"gpu_buffer_bytes",image.gpuBufferBytes},{"process_peak_rss_bytes",processPeakRss()},
            {"instances",a.instances},{"unique_meshes",a.uniqueMeshes},{"tlas_builds",a.tlasBuilds},
            {"tlas_refits",a.tlasRefits},{"tlas_reuses",a.tlasReuses},{"render_seconds",image.seconds}});
        return std::make_pair(std::move(scene),std::move(image));
    };
    auto cold=run("cold",package.snapshot,nullptr);
    bool hotEqual=true;for(int i=0;i<3;++i){auto hot=run("hot",package.snapshot,cold.first.get());hotEqual&=hot.second.radiance==cold.second.radiance&&hot.second.sampleCounts==cold.second.sampleCounts;}
    auto world=package.snapshot;
    auto index=std::find_if(world.draws.begin(),world.draws.end(),[](const auto &d){return glm::vec3(d.parameters.emissiveNormal)==glm::vec3(0);});
    if(index==world.draws.end())throw std::invalid_argument("Benchmark needs a nonemissive instance");
    report["edited_object_id"]=index->objectId;index->model[3].x+=.01f;
    auto moved=run("transform",world,cold.first.get());
    const auto &operation=report["operations"].back();
    if(operation["scene_uploads"]!=2||operation["scene_upload_bytes"].get<uint64_t>()>=operation["tlas_bytes"].get<uint64_t>()+operation["instance_bytes"].get<uint64_t>())throw std::runtime_error("Transform did not reduce host upload bytes");
    pt::CpuScene fresh(world);configure(fresh);auto reference=pt::renderGpu(fresh,options,gpu);
    const bool movedEqual=moved.second.radiance==reference.radiance&&moved.second.sampleCounts==reference.sampleCounts;
    index->parameters.albedoAlpha.x=index->parameters.albedoAlpha.x==0?.01f:index->parameters.albedoAlpha.x*.9f;
    auto material=run("material",world,moved.first.get());
    if(report["operations"].back()["scene_uploads"]!=1||report["operations"].back()["patch_bytes"]!=4||report["operations"].back()["patch_buffers"]!=1)throw std::runtime_error("Material edit uploaded unchanged buffers");
    auto materialReference=pt::renderGpu(*material.first,options,gpu);
    const bool materialEqual=material.second.radiance==materialReference.radiance&&material.second.sampleCounts==materialReference.sampleCounts;
    report["material_patch_vs_full_upload_raw_exact"]=materialEqual;
    report["hot_raw_exact"]=hotEqual;report["transform_refit_vs_rebuild_raw_exact"]=movedEqual;
    if(!hotEqual||!movedEqual||!materialEqual)throw std::runtime_error("Preparation changed fixed-seed raw film");
    pt::writeImage(cold.second,options.exposure,output+"-cold");pt::writeImage(moved.second,options.exposure,output+"-transform");pt::writeImage(material.second,options.exposure,output+"-material");
    std::ofstream out(output+".preparation.json");out<<report.dump(2)<<'\n';if(!out)throw std::runtime_error("Cannot save preparation benchmark");
    cache.reset();gpu->close();std::cout<<report.dump(2)<<'\n';
}
}
int main(int argc,char **argv){try{
    pt::Options options;options.adaptive=false;std::string backend="CPU",input,output,fixture;bool selfTest=false,filter=false,benchmark=false;
    for(int i=1;i<argc;++i){std::string flag=argv[i];auto value=[&](){if(++i>=argc)throw std::invalid_argument("Missing value for "+flag);return std::string(argv[i]);};
        if(flag=="--fixture")fixture=value();else if(flag=="--backend")backend=value();else if(flag=="--scene")input=value();else if(flag=="--output")output=value();
        else if(flag=="--size"){auto size=value();auto at=size.find('x');if(at==std::string::npos)throw std::invalid_argument("Size must be WIDTHxHEIGHT");options.width=number(size.substr(0,at),16384);options.height=number(size.substr(at+1),16384);}
        else if(flag=="--gpu-batch-samples")options.gpuBatchSamples=number(value(),64);
        else if(flag=="--samples")options.samples=number(value(),1048576);else if(flag=="--bounces")options.maxDepth=number(value(),128);
        else if(flag=="--seed")options.seed=number(value(),UINT32_MAX);else if(flag=="--threads")options.threads=number(value(),256);
        else if(flag=="--no-shadow-any-hit")options.shadowAnyHit=false;
        else if(flag=="--gpu-traversal"){auto mode=value();if(mode=="auto")options.gpuTraversal=pt::GpuTraversal::Auto;else if(mode=="software")options.gpuTraversal=pt::GpuTraversal::Software;else if(mode=="native")options.gpuTraversal=pt::GpuTraversal::Native;else throw std::invalid_argument("Traversal must be auto, software or native");}
        else if(flag=="--photons")options.photonMapping=true;
        else if(flag=="--sppm")options.sppm=true;
        else if(flag=="--sppm-alpha"){auto text=value();size_t end=0;options.sppmAlpha=std::stof(text,&end);if(end!=text.size())throw std::invalid_argument("Invalid SPPM alpha");}
        else if(flag=="--photon-paths")options.photonPaths=number(value(),4000000);
        else if(flag=="--photon-radius"){auto text=value();size_t end=0;options.photonRadius=std::stof(text,&end);if(end!=text.size())throw std::invalid_argument("Invalid photon radius");}
        else if(flag=="--no-thin-sun-proposal")options.thinSunProposal=false;else if(flag=="--no-solar-proposal")options.waterSunProposal=false;
        else if(flag=="--denoise")filter=true;else if(flag=="--benchmark-preparation")benchmark=true;else if(flag=="--self-test")selfTest=true;else throw std::invalid_argument("Unknown option: "+flag);
    }
    if(backend!="CPU"&&backend!="Metal"&&backend!="Vulkan")throw std::invalid_argument("Backend must be CPU, Metal or Vulkan");
    if(benchmark&&filter)throw std::invalid_argument("Preparation benchmark excludes denoising; omit --denoise");
    pt::validateOptions(options);
    if(backend!="CPU"&&options.sppm)throw std::invalid_argument("SPPM is CPU-only; use --backend CPU --sppm");
    if(backend=="CPU"&&options.gpuTraversal==pt::GpuTraversal::Native)throw std::invalid_argument("Native GPU traversal requires Metal or Vulkan backend");
    if(selfTest){if(backend=="CPU")throw std::invalid_argument("Use pt-cpu-tests for CPU validation");auto gpu=device(backend);pt::validateGpuPathTracing(gpu);gpu->close();std::cout<<backend<<" GPU PT validation passed\n";return 0;}
    if((input.empty()&&fixture.empty())||(!input.empty()&&!fixture.empty())||output.empty())throw std::invalid_argument("Usage: pt-package-render (--scene scene.json | --fixture pool-caustics|pool-flat|pool-no-water|pool-sunlit|pool-sunlit-underwater|pool-sunlit-flat) --output prefix [--backend CPU|Metal|Vulkan --size 160x90 --samples 512 --bounces 24 --seed 1 --threads 2 --photons | --sppm --sppm-alpha .666667 --photon-paths 200000 --photon-radius .08 --gpu-batch-samples 8 --no-shadow-any-hit --denoise --no-thin-sun-proposal --no-solar-proposal]; SPPM is CPU-only, samples = iterations, photon-paths = per iteration; native validation: --backend Metal|Vulkan --self-test");
    pt::ScenePackage package;
    if(fixture.empty())package=pt::loadScenePackage(input,options.width,options.height);
    else {
        bool sunlit=fixture=="pool-sunlit"||fixture=="pool-sunlit-underwater"||fixture=="pool-sunlit-flat";
        if(!sunlit&&fixture!="pool-caustics"&&fixture!="pool-flat"&&fixture!="pool-no-water")throw std::invalid_argument("Unknown fixture: "+fixture);
        package.snapshot=pt::makePoolCausticsScene(options.width,options.height,fixture!="pool-flat"&&fixture!="pool-sunlit-flat",fixture!="pool-no-water",sunlit,fixture=="pool-sunlit-underwater").snapshot;
        package.sunDirection=glm::normalize(sunlit?glm::vec3(-.3f,1,.2f):glm::vec3(-.25f,1,.3f));package.sunIrradiance=sunlit?glm::vec3(4.5f,4.35f,4.1f):glm::vec3(4);package.sunRadius=sunlit?.00465f:.01f;
        if(sunlit)package.environment=std::make_shared<pt::Environment>(16,8,std::vector<glm::vec3>(128,glm::vec3(.025f,.04f,.06f)));
        package.metadata={{"source",{{"name",fixture}}},{"water_surface",sunlit?"frozen analytic five-wave ripple mesh (not an FFT capture)":"frozen analytic two-wave validation mesh (not an FFT capture)"},{"camera_underwater",fixture=="pool-sunlit-underwater"},{"sun_angular_radius_radians",package.sunRadius}};
    }
    options.exposure=package.snapshot.exposure;
    if(benchmark){benchmarkPreparation(package,options,backend,input.empty()?fixture:input,output);return 0;}
    pt::CpuScene scene(package.snapshot);scene.environment=package.environment;scene.sunDirection=package.sunDirection;scene.sunIrradiance=package.sunIrradiance;scene.sunRadius=package.sunRadius;
    pt::Image image;if(backend=="CPU")image=pt::render(scene,options);else{auto gpu=device(backend);image=pt::renderGpu(scene,options,gpu);gpu->close();}
    if(filter)pt::denoise(image);pt::writeImage(image,options.exposure,output);pt::writeReport(image,scene,options,output,package.metadata.value("source",nlohmann::json::object()).value("name",std::string("package")));
    std::ifstream in(output+".json");nlohmann::json report;in>>report;in.close();report["scene_package"]=package.metadata;report["entry_point"]="pt-package-render";std::ofstream out(output+".json");out<<report.dump(2)<<'\n';if(!out)throw std::runtime_error("Cannot save package report");
    return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
