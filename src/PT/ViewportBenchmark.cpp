#include "PT/ViewportBenchmark.h"
#include "PT/ScenePackage.h"
#include "PT/ValidationScenes.h"
#include "engine/RenderRuntime.h"
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>
#include <json/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#if defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#endif
namespace pt {
namespace {
using Clock=std::chrono::steady_clock;
uint64_t peakRss(){
#if defined(__APPLE__) || defined(__linux__)
    rusage usage{};if(getrusage(RUSAGE_SELF,&usage))return 0;
#ifdef __APPLE__
    return usage.ru_maxrss;
#else
    return uint64_t(usage.ru_maxrss)*1024;
#endif
#else
    return 0;
#endif
}
std::vector<char> bytes(const std::string &path){std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Cannot read benchmark film: "+path);return {std::istreambuf_iterator<char>(in),{}};}
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
}
void runViewportBenchmark(int argc,char **argv,std::shared_ptr<rhi::GraphicsDevice> device,GLFWwindow *window){
    std::string scenePath,fixture,output;uint32_t cycles=3;bool batching=true,reusePreview=true,culling=true,navigationProbe=false;
    uint32_t width=32,height=18;
    for(int i=2;i<argc;++i){std::string flag=argv[i];auto value=[&](){if(++i>=argc)throw std::invalid_argument("Missing benchmark option: "+flag);return std::string(argv[i]);};
        if(flag=="--scene")scenePath=value();else if(flag=="--fixture")fixture=value();else if(flag=="--output")output=value();else if(flag=="--backend")value();
        else if(flag=="--navigation-probe")navigationProbe=true;
        else if(flag=="--no-raster-culling")culling=false;
        else if(flag=="--raster-size"){auto text=value();auto split=text.find('x');if(split==std::string::npos)throw std::invalid_argument("Benchmark size must be WIDTHxHEIGHT");size_t used=0;auto w=std::stoul(text.substr(0,split),&used);if(used!=split||w<16||w>1920)throw std::invalid_argument("Invalid benchmark width");auto h=std::stoul(text.substr(split+1),&used);if(used!=text.size()-split-1||h<16||h>1080)throw std::invalid_argument("Invalid benchmark height");width=uint32_t(w);height=uint32_t(h);}
        else if(flag=="--no-raster-batching")batching=false;else if(flag=="--raster-during-preparation")reusePreview=false;
        else if(flag=="--cycles"){auto t=value();size_t n=0;auto v=std::stoul(t,&n);if(n!=t.size()||v<1||v>16)throw std::invalid_argument("Benchmark cycles must be 1..16");cycles=uint32_t(v);}
        else throw std::invalid_argument("Unknown viewport benchmark option: "+flag);
    }
    if(output.empty()||scenePath.empty()==fixture.empty()||(!fixture.empty()&&fixture!="fft"))throw std::invalid_argument("Use --scene PATH or --fixture fft, and --output PREFIX");
    Options options;options.width=32;options.height=18;options.samples=4;options.maxDepth=8;options.adaptive=false;options.gpuBatchSamples=1;
    std::cout<<"Viewport benchmark loading "<<(scenePath.empty()?fixture:scenePath)<<"\n"<<std::flush;
    const auto importStart=Clock::now();ScenePackage package;
    if(!scenePath.empty())package=loadScenePackage(scenePath,width,height);
    else {
        package.snapshot=makeCausticsScene(width,height,false).snapshot;
        render::OceanSurfaceSettings water;water.spectrum.size=512;water.spectrum.length=64;water.meshSize=129;water.seaLevel=.4f;water.detailWaves=true;water.absorption={.2f,.1f,.05f};water.scattering={.025f,.05f,.07f};
        std::unordered_map<const render::MeshPayload*,std::shared_ptr<const render::MeshPayload>> meshes;
        uint64_t id=uint64_t(1)<<62;
        for(auto &draw:package.snapshot.draws){auto &mesh=meshes[draw.mesh.get()];if(!mesh){auto copy=std::make_shared<render::MeshPayload>(*draw.mesh);copy->id=++id;copy->revision=1;mesh=copy;}draw.mesh=mesh;}
        package.snapshot.frame.oceans={water};package.snapshot.frame.timeSeconds=1;
        package.environment=std::make_shared<Environment>(16,8,std::vector<glm::vec3>(128,glm::vec3(.3f)));
        package.sunDirection=glm::normalize(glm::vec3(.3f,.9f,.2f));package.sunIrradiance={3,3,3};package.sunRadius=.00465f;
    }
    auto world=std::make_shared<render::RenderWorldSnapshot>(package.snapshot);
    world->asynchronousStreaming=true;world->automaticQuality=false;
    world->rasterInstanceBatching=batching;world->rasterFrustumCulling=culling;
    world->frame.shadows=world->frame.ssao=world->frame.rsm=world->frame.taa=false;
    auto lighting=std::make_shared<engine::PathTracingViewportLighting>();lighting->environment=package.environment;lighting->sunDirection=package.sunDirection;lighting->sunIrradiance=package.sunIrradiance;lighting->sunRadius=package.sunRadius;
    const auto importSeconds=std::chrono::duration<double>(Clock::now()-importStart).count();
    auto parent=std::filesystem::path(output).parent_path();if(!parent.empty())std::filesystem::create_directories(parent);
    nlohmann::json report={{"backend",device->backend()==rhi::Backend::Metal?"Metal":"Vulkan"},{"scene",scenePath.empty()?fixture:scenePath},{"cycles",cycles},{"width",options.width},{"height",options.height},{"raster_width",width},{"raster_height",height},{"samples",options.samples},{"seed",options.seed},{"max_depth",options.maxDepth},{"import_seconds",importSeconds},{"scene_package",package.metadata},{"scope","actual RenderRuntime, hidden 64x64 surface, configured Raster/PT resolution; effects and denoise disabled, package/fixed lighting; producer polls GLFW at 5ms, no GUI input-to-photon measurement; measurement retains first-cycle reference world snapshots and performs explicit film exports"}};
    report["operations"]=nlohmann::json::array();report["navigation"]=nlohmann::json::array();report["cycle_memory"]=nlohmann::json::array();
    report["raster_frustum_culling"]=culling;report["raster_instance_batching"]=batching;report["reuse_preparation_preview"]=reusePreview;
    engine::RenderRuntime runtime(device,nullptr,2,true);
    int surfaceWidth=0,surfaceHeight=0;glfwGetFramebufferSize(window,&surfaceWidth,&surfaceHeight);
    runtime.notifySurfaceExtent(uint32_t(surfaceWidth),uint32_t(surfaceHeight));
    report["surface_width"]=surfaceWidth;report["surface_height"]=surfaceHeight;
    std::shared_ptr<engine::PathTracingViewportRequest> request;
    std::shared_ptr<engine::PathTracingViewportExport> exportTicket;
    bool navigation=false;uint64_t epoch=0,lastFrames=0;auto lastProgress=Clock::now();
    std::string pendingScreenshot;
    bool measureProgress=true;
    double maxFrameGap=0,maxProducerCall=0;uint64_t skipped=0;
    auto pump=[&](auto predicate){
        const auto deadline=Clock::now()+std::chrono::seconds(180);
        while(Clock::now()<deadline){
            const auto tick=Clock::now();runtime.rethrowFailure();auto status=runtime.pathTracingStatus();
            if(status.state=="Error")throw std::runtime_error("Viewport benchmark: "+status.message);
            if(predicate(status))return status;
            engine::RenderPacket packet;packet.world=world;packet.pathTracingViewport=request;packet.pathTracingExport=exportTicket;packet.pathTracingNavigation=navigation;
            packet.screenshot=pendingScreenshot;
            if(!runtime.trySubmitFrame(std::move(packet)))++skipped;else pendingScreenshot.clear();
            glfwPollEvents();maxProducerCall=std::max(maxProducerCall,std::chrono::duration<double>(Clock::now()-tick).count());
            const auto frames=runtime.framesRendered();if(frames!=lastFrames){const auto now=Clock::now();if(measureProgress)maxFrameGap=std::max(maxFrameGap,std::chrono::duration<double>(now-lastProgress).count());lastProgress=now;lastFrames=frames;}
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        throw std::runtime_error("Viewport benchmark deadline exceeded at epoch "+std::to_string(epoch)+" ("+runtime.pathTracingStatus().state+")");
    };
    std::cout<<"Viewport benchmark startup warmup\n"<<std::flush;
    const auto startup=Clock::now();auto warm=pump([](const auto &s){return s.state!="Warming";});
    report["startup_seconds"]=std::chrono::duration<double>(Clock::now()-startup).count();report["startup_trace_seconds"]=warm.startupTraceSeconds;report["startup_display_seconds"]=warm.startupDisplaySeconds;
    check(warm.startupWarmupError.empty(),"PT startup warmup failed");
    std::cout<<"Viewport benchmark Raster streaming\n"<<std::flush;
    pump([&](const auto &){return runtime.framesRendered()>=4&&runtime.pendingMeshUploads()==0&&runtime.pendingAssetUploads()==0;});
    report["raster_warmup_seconds"]=std::chrono::duration<double>(Clock::now()-startup).count()-report["startup_seconds"].get<double>();
    const auto screenshotStart=Clock::now();pendingScreenshot=output+"-raster.ppm";
    const auto screenshotTarget=runtime.framesRendered()+4;pump([&](const auto&){return pendingScreenshot.empty()&&runtime.framesRendered()>=screenshotTarget;});
    check(std::filesystem::exists(output+"-raster.ppm"),"Initial Raster screenshot was not saved");
    report["raster_screenshot_seconds"]=std::chrono::duration<double>(Clock::now()-screenshotStart).count();
    // Startup compilation / initial Raster asset streaming are reported above.
    maxFrameGap=maxProducerCall=0;lastProgress=Clock::now();lastFrames=runtime.framesRendered();
    auto replace=[&](bool recapture,size_t budget=4*1024*1024){if(request)request->cancelled=true;request=std::make_shared<engine::PathTracingViewportRequest>();request->epoch=++epoch;request->world=world;request->lighting=lighting;request->options=options;request->forceRecapture=recapture;request->paused=true;request->denoise=false;request->preparationByteBudget=budget;request->cullRasterPreview=culling;request->batchRasterPreview=batching;request->reusePreparationPreview=reusePreview;};
    struct ReferenceJob {std::shared_ptr<const render::RenderWorldSnapshot> world;std::string prefix;size_t operation;};
    std::vector<ReferenceJob> references;bool retainReference=true;
    std::shared_ptr<const render::RenderWorldSnapshot> initialWorld;std::string initialFilm;
    auto run=[&](const std::string &name,bool recapture){
        const auto rasterStart=runtime.rasterFrames();replace(recapture);const auto ready=pump([&](const auto &s){return s.epoch==epoch&&s.state=="Paused"&&s.samples==0;});
        check(ready.maxPreparationWorkBytes<=request->preparationByteBudget,"Viewport exceeded preparation work budget");
        request->paused=false;auto done=pump([&](const auto &s){return s.epoch==epoch&&s.state=="Complete"&&s.samples==options.samples;});
        const auto prefix=output+"-"+std::to_string(epoch)+"-"+name;
        exportTicket=std::make_shared<engine::PathTracingViewportExport>();exportTicket->epoch=epoch;exportTicket->prefix=prefix;auto exported=exportTicket->completion->get_future();
        pump([&](const auto &s){return s.epoch==epoch&&exported.wait_for(std::chrono::seconds(0))==std::future_status::ready;});check(exported.get()==prefix,"Export completion prefix changed");exportTicket.reset();
        report["operations"].push_back({{"name",name},{"epoch",epoch},{"frozen_time_seconds",world->frame.timeSeconds},{"camera_y",world->frame.cameraPosition.y},{"camera_media_count",done.cameraMediaCount},{"accepted_seconds",done.requestAcceptedSeconds},{"capture_seconds",done.captureSeconds},{"cpu_scene_seconds",done.cpuSceneSeconds},{"cpu_export_seconds",done.cpuExportSeconds},{"ready_latency_seconds",done.readyLatencySeconds},{"first_sample_latency_seconds",done.firstSampleLatencySeconds},{"max_prepare_frame_seconds",done.maxPreparationFrameSeconds},{"max_prepare_work_bytes",done.maxPreparationWorkBytes},{"pipeline_seconds",done.pipelineSeconds},{"scene_uploads",done.sceneUploads},{"host_scene_upload_bytes",done.sceneUploadBytes},{"gpu_clone_bytes",done.cloneBytes},{"patch_bytes",done.patchBytes},{"gpu_cache_bytes",done.gpuCacheBytes},{"cpu_blas_cache_bytes",done.blasCacheBytes},{"tlas_builds",done.tlasBuilds},{"tlas_refits",done.tlasRefits},{"tlas_reuses",done.tlasReuses},{"process_peak_rss_bytes",peakRss()},{"raw_reference_exact",nullptr},{"film",prefix+".pfm"}});
        report["operations"].back()["frozen_preview_frames"]=done.previewFrames;report["operations"].back()["raster_frames"]=runtime.rasterFrames()-rasterStart;
        if((retainReference&&name!="hot"&&name!="restored")||(fixture=="fft"&&(name=="time"||name=="underwater")))references.push_back({world,prefix,report["operations"].size()-1});
        std::cout<<"Viewport "<<name<<": ready "<<done.readyLatencySeconds*1000<<" ms, host scene "<<done.sceneUploadBytes<<" B\n"<<std::flush;
        return done;
    };
    // No CPU readback in the frame loop except explicit checkpoint exports.
    run("cold",true);initialWorld=world;initialFilm=report["operations"].back()["film"].get<std::string>();
    for(uint32_t i=0;i<cycles;++i){
        auto hot=run("hot",true);check(hot.sceneUploads==0,"Unchanged viewport uploaded scene again");check(bytes(report["operations"].back()["film"].get<std::string>())==bytes(initialFilm),"Hot raw film changed");
        navigation=true;
        const auto beforeNavigation=world;
        for(int pose=0;pose<(navigationProbe?3:1);++pose){
            auto nav=std::make_shared<render::RenderWorldSnapshot>(*beforeNavigation);const float shift=pose==1?1.f:pose==2?-1.f:0.f;
            nav->frame.cameraPosition.x+=shift;nav->frame.view*=glm::translate(glm::mat4(1),glm::vec3(-shift,0,0));nav->frame.viewProjection*=glm::translate(glm::mat4(1),glm::vec3(-shift,0,0));world=nav;
            // First settle queued old packets, then time continuous Raster progress.
            const auto settled=runtime.framesRendered()+4;pump([&](const auto &s){return s.state=="Navigating"&&runtime.framesRendered()>=settled;});
            const auto start=Clock::now();const auto uploadStart=runtime.instancePoseUploadBytes();const auto materialStart=runtime.rasterMaterialBuilds();const auto first=runtime.framesRendered();const auto target=first+(navigationProbe?20:3);
            pump([&](const auto &s){return s.state=="Navigating"&&runtime.framesRendered()>=target;});
            nlohmann::json sample={{"cycle",i},{"pose",pose},{"camera_x_offset",shift},{"frames",runtime.framesRendered()-first},{"seconds",std::chrono::duration<double>(Clock::now()-start).count()},{"source_packets",runtime.rasterSourcePackets()},{"culled_packets",runtime.rasterCulledPackets()},{"output_packets",runtime.rasterPackets()},{"steady_pose_upload_bytes",runtime.instancePoseUploadBytes()-uploadStart}};
            sample["steady_material_builds"]=runtime.rasterMaterialBuilds()-materialStart;
            sample["live_material_variants"]=runtime.rasterMaterialVariants();
            check(sample["steady_material_builds"].get<uint64_t>()==0,"Stable navigation rebuilt material variants");
            if(navigationProbe){
                const auto captureStart=Clock::now();measureProgress=false;pendingScreenshot=output+"-nav-"+std::to_string(i)+"-"+std::to_string(pose)+".ppm";sample["image"]=pendingScreenshot;
                const auto screenshotTarget=runtime.framesRendered()+4;pump([&](const auto&){return pendingScreenshot.empty()&&runtime.framesRendered()>=screenshotTarget;});
                check(std::filesystem::exists(sample["image"].get<std::string>()),"Navigation screenshot was not saved");sample["screenshot_seconds"]=std::chrono::duration<double>(Clock::now()-captureStart).count();measureProgress=true;lastProgress=Clock::now();lastFrames=runtime.framesRendered();
            }
            report["navigation"].push_back(std::move(sample));
        }
        world=beforeNavigation;navigation=false;
        auto moved=std::make_shared<render::RenderWorldSnapshot>(*world);moved->frame.cameraPosition.x+=.01f;moved->frame.view*=glm::translate(glm::mat4(1),glm::vec3(-.01f,0,0));moved->frame.viewProjection*=glm::translate(glm::mat4(1),glm::vec3(-.01f,0,0));world=moved;
        auto camera=run("camera",false);check(camera.sceneUploads==0&&camera.preparationSteps==0,"Camera reused neither scene nor film allocation path");
        if(fixture=="fft"){
            auto next=std::make_shared<render::RenderWorldSnapshot>(*world);next->frame.timeSeconds=2.f+i;world=next;run("time",true);
            auto under=std::make_shared<render::RenderWorldSnapshot>(*world);under->frame.cameraPosition={0,.1f,2};under->frame.view=glm::lookAt(under->frame.cameraPosition,glm::vec3(0,.2f,0),glm::vec3(0,1,0));glm::mat4 depth(1);depth[2][2]=.5f;depth[3][2]=.5f;under->frame.viewProjection=depth*glm::perspective(glm::radians(50.f),float(width)/height,.05f,100.f)*under->frame.view;world=under;auto wet=run("underwater",false);check(wet.cameraMediaCount>0,"Underwater camera was not initialized inside a medium");
        }else{
            auto changed=std::make_shared<render::RenderWorldSnapshot>(*world);changed->draws[0].parameters.albedoAlpha.x*=.9f;world=changed;auto material=run("material",false);check(material.sceneUploads==1&&material.patchBytes==4,"Material scalar did not patch exactly one word");
            auto transformed=std::make_shared<render::RenderWorldSnapshot>(*world);transformed->draws[0].model[3].x+=.01f;world=transformed;auto transform=run("transform",false);check(transform.tlasRefits==1&&transform.sceneUploads==2,"Transform rebuilt / uploaded unchanged scene data");
        }
        // Cancel an incompletely prepared material edit, then restore the same
        // frozen initial world. Stale queued packets must not revive its epoch.
        auto interrupted=std::make_shared<render::RenderWorldSnapshot>(*world);interrupted->draws[0].parameters.albedoAlpha.y*=.83f;world=interrupted;replace(false,fixture=="fft"?1024:256*1024);
        auto partial=pump([&](const auto &s){return s.epoch==epoch&&s.state=="Uploading"&&s.preparationBytes<s.preparationTotalBytes;});check(partial.samples==0,"Partial scene sampled before cancellation");
        request->cancelled=true;world=std::make_shared<render::RenderWorldSnapshot>(*initialWorld);
        run("restored",true);check(bytes(report["operations"].back()["film"].get<std::string>())==bytes(initialFilm),"Cancellation / restoration changed raw film");
        const auto retired=runtime.framesRendered()+4;pump([&](const auto& s){return s.epoch==epoch&&s.state=="Complete"&&runtime.framesRendered()>=retired;});
        report["cycle_memory"].push_back({{"cycle",i},{"tracked_gpu_bytes",runtime.currentResourceBytes()},
            {"native_gpu_bytes",runtime.nativeMemorySupported()?nlohmann::json(runtime.currentNativeBytes()):nlohmann::json(nullptr)},
            {"process_peak_rss_bytes",peakRss()},{"live_material_variants",runtime.rasterMaterialVariants()}});
        retainReference=false;
    }
    const auto continuousEnd=Clock::now();
    report["retained_reference_snapshots"]=references.size();
    report["raster_culled_packets"]=runtime.rasterCulledPackets();report["raster_source_packets"]=runtime.rasterSourcePackets();report["raster_packets"]=runtime.rasterPackets();report["instance_pose_upload_bytes"]=runtime.instancePoseUploadBytes();report["raster_frames"]=runtime.rasterFrames();
    report["raster_material_builds"]=runtime.rasterMaterialBuilds();report["raster_material_cache_hits"]=runtime.rasterMaterialCacheHits();
    report["raster_material_live_variants"]=runtime.rasterMaterialVariants();report["raster_material_parameter_upload_bytes"]=runtime.rasterMaterialUploadBytes();
    report["max_render_progress_gap_seconds"]=maxFrameGap;report["max_producer_poll_submit_seconds"]=maxProducerCall;report["skipped_producer_snapshots"]=skipped;
    report["frames_rendered"]=runtime.framesRendered();report["render_p95_ms"]=runtime.renderP95Milliseconds();report["render_p99_ms"]=runtime.renderP99Milliseconds();report["peak_gpu_payload_bytes"]=runtime.peakResourceBytes();report["peak_native_bytes"]=runtime.nativeMemorySupported()?nlohmann::json(runtime.peakNativeBytes()):nlohmann::json(nullptr);report["peak_completion_latency_ms"]=runtime.peakCompletionLatencyMilliseconds();
    // Recapture the retained first-cycle immutable worlds on the owner thread
    // after measurement. References do not enter frame-progress latency.
    std::vector<render::RenderWorldSnapshot> frozenReferences;
    for(const auto &job:references)frozenReferences.push_back(runtime.capturePathTracingScene(*job.world));
    runtime.finish();
    for(size_t i=0;i<references.size();++i){auto &job=references[i];CpuScene fresh(frozenReferences[i]);fresh.environment=lighting->environment;fresh.sunDirection=lighting->sunDirection;fresh.sunIrradiance=lighting->sunIrradiance;fresh.sunRadius=lighting->sunRadius;
        auto reference=renderGpu(fresh,options,device);writeImage(reference,job.world->exposure,job.prefix+"-reference");
        check(bytes(job.prefix+".pfm")==bytes(job.prefix+"-reference.pfm"),"Viewport raw film differs from fresh capture / full upload");report["operations"][job.operation]["raw_reference_exact"]=true;
    }
    report["initial_restored_vs_fresh_raw_exact"]=true;report["verification_seconds"]=std::chrono::duration<double>(Clock::now()-continuousEnd).count();report["process_peak_rss_including_verification_bytes"]=peakRss();
    std::ofstream out(output+".viewport.json");out<<report.dump(2)<<'\n';if(!out)throw std::runtime_error("Cannot write viewport benchmark report");
    std::cout<<"Viewport benchmark passed: "<<output<<".viewport.json\n";
}
}
