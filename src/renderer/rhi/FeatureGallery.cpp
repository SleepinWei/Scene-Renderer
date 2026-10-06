#include "renderer/rhi/FeatureScenes.h"
#include "renderer/rhi/SceneAdapter.h"
#include "renderer/rhi/GalleryDiagnostics.h"
#include "rhi/ShaderAssets.h"
#include "renderer/RenderScene.h"
#include "system/RenderManager.h"
#include "system/InputManager.h"
#include "utils/Utils.h"
#include "utils/Camera.h"
#include "component/Atmosphere.h"
#include "component/Cloud.h"
#include "component/Ocean.h"
#include "component/TerrainComponent.h"
#include "component/Lights.h"
#include "object/SkyBox.h"
#include "object/Terrain.h"
#include <cmath>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stb/stb_image_write.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <numeric>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <json/json.hpp>
namespace render {
void runFeatureGallery(const std::string& directory,const std::string& selection){
    if(rhi::requestedBackend()==rhi::Backend::OpenGL)throw std::invalid_argument("The native gallery requires Metal or Vulkan");
#ifdef SCENERENDERER_HAS_VULKAN
    if(rhi::requestedBackend()==rhi::Backend::Vulkan)rhi::configureVulkanWindowing();
#endif
    if(!glfwInit())throw std::runtime_error("Gallery GLFW initialization failed");
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);GLFWwindow* window=nullptr;
    std::vector<std::string> names;
    if(selection.empty() || selection=="core")names={"bunny","helmet","cornell"};
    else if(selection=="gi")names={"sponza","san-miguel"};
    else if(selection=="benchmarks")names={"dragon","buddha","armadillo","sibenik"};
    else if(selection=="diagnostics")names={"terrain","shadow-test"};else names={selection};
    if(selection=="cloud-gallery")names={"clouds","clouds-sunset","clouds-storm"};
    if(selection=="cloud-volume-gallery")names={"cloud-volume","cloud-inside","cloud-vortex"};
    if(selection=="ao")names={"cornell","sponza"};
    const bool coastalPerformance=selection=="coastal-performance"||selection=="coastal-performance-water";
    if(coastalPerformance)names={selection=="coastal-performance-water"?"coastal-water":"coastal-beach"};
    const bool coastalMotion=selection=="coastal-motion"||selection=="coastal-motion-camera";
    if(coastalMotion)names={"coastal-beach"};
    const bool underwaterGallery=selection=="underwater-gallery";
    if(underwaterGallery)names={"coastal-underwater"};
    const bool diagnostics=selection=="diagnostics";
    const bool water=selection.rfind("coastal-",0)==0 || selection.rfind("cloud-",0)==0 || selection=="ocean" || selection=="ocean-clear" || selection=="mountain-lake" || selection=="mountain-lake-ground" || selection=="mountain-lake-beach";int width=water?1920:960,height=water?1080:720;
    if(coastalMotion){width=960;height=540;}
    if(underwaterGallery){width=1280;height=720;}
    if(coastalPerformance){width=1280;height=720;if(auto e=std::getenv("SCENERENDERER_BENCH_WIDTH")){width=std::max(64,std::atoi(e));height=width*9/16;}}
#ifdef __APPLE__
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER,GLFW_FALSE);
#endif
    try {
        if(createWindow(window,width,height)!=0 || gladInit()!=0)throw std::runtime_error("Gallery device initialization failed");
        auto device=rhi::graphicsDevice();glfwGetFramebufferSize(window,&width,&height);framebuffer_size_callback(window,width,height);
        std::filesystem::create_directories(directory);auto manager=RenderManager::GetInstance();manager->init();
        for(const auto& name:names){
            auto scene=makeClassicScene(name);scene->mainCamera()->setAspect(float(width)/height);
            {
                SceneAdapter adapter(device);ForwardPbrRenderer renderer(device,rhi::defaultShaderDirectory(),width,height,PbrPath::Scene);
                FrameData lastFrame;
                const bool cloudScene=name.rfind("cloud",0)==0;
                const bool waterScene=name=="coastal-water" || name=="coastal-beach" || name=="coastal-underwater" || name=="ocean" || name=="ocean-clear" || name=="mountain-lake" || name=="mountain-lake-ground" || name=="mountain-lake-beach";
                nlohmann::json cloudMetrics,waterMetrics;
                SceneSnapshotBuilder diagnosticBuilder;
                float galleryTime=8;
                auto collect=[&]() {
                    if(!diagnostics)return adapter.collect(scene,galleryTime);
                    // Prepare CPU sources synchronously, then exercise the editor's
                    // asynchronous page IO and depth-feedback path on the GPU.
                    auto snapshot=*diagnosticBuilder.capture(scene,8,width,height,true);
                    snapshot.asynchronousStreaming=true;
                    return adapter.resolve(snapshot);
                };
                if(coastalPerformance) {
                    const bool moving=std::getenv("SCENERENDERER_BENCH_MOVE")!=nullptr;
                    const bool baseline=std::getenv("SCENERENDERER_BENCH_BASELINE")!=nullptr;
                    if(baseline)scene->terrain()->getComponent<Ocean>()->updateSettings([](auto& s){s.robustRefraction=false;s.multipleScattering=false;s.shore.enabled=false;});
                    if(std::getenv("SCENERENDERER_BENCH_EDGE")){scene->mainCamera()->setPosition({-55,5,-50});scene->mainCamera()->setAngles(-130,-10);}
                    const auto initialPosition=scene->mainCamera()->getPosition();
                    nlohmann::json report;report["scene"]=name;report["width"]=width;report["height"]=height;report["moving_camera"]=moving;report["new_effects_disabled"]=baseline;
                    report["warmup_frames"]=8;report["simulation_step_seconds"]=1./15.;report["frames"]=nlohmann::json::array();
                    auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
                    using Clock=std::chrono::steady_clock;
                    for(int i=0;i<40;++i) {
                        glfwPollEvents();device->waitIdle();device->drainGpuProfile();
                        if(moving)scene->mainCamera()->setPosition(initialPosition+glm::vec3(std::sin(float(i)*.13f)*5,0,float(i)*.2f));
                        const auto start=Clock::now();device->beginFrame();const auto acquired=Clock::now();
                        auto frame=adapter.collect(scene,8+float(i)/15);
                        const auto& settings=manager->setting;frame.frame.shadows=settings.enableShadow;frame.frame.ssao=settings.enableSSAO;frame.frame.rsm=settings.enableRSM;frame.frame.taa=settings.enableTSAA;
                        frame.frame.forwardShading=!settings.useDefer;frame.frame.toneMapping=settings.enableHDR;frame.frame.shadowSettings=settings.shadowSettings;frame.frame.rsmSettings=settings.rsmSettings;
                        const auto collected=Clock::now();
                        renderer.render(frame.frame,frame.packets,frame.exposure);const auto recorded=Clock::now();
                        device->copyToBackbuffer(renderer.output());device->present();const auto submitted=Clock::now();
                        device->waitIdle();const auto completed=Clock::now();auto timing=device->gpuTimingStats();auto samples=device->drainGpuProfile();
                        nlohmann::json entry={{"frame",i},{"simulation_time",8+float(i)/15},{"acquire_cpu_ms",ms(start,acquired)},{"collect_cpu_ms",ms(acquired,collected)},
                            {"record_cpu_ms",ms(collected,recorded)},{"submit_cpu_ms",ms(recorded,submitted)},{"wait_cpu_ms",ms(submitted,completed)},{"wall_ms",ms(start,completed)},{"gpu_submission_ms",timing.milliseconds}};
                        entry["samples"]=nlohmann::json::array();
                        double origin=0;for(const auto& sample:samples)if(sample.kind=="submission"){origin=sample.startMilliseconds;break;}
                        for(const auto& sample:samples)entry["samples"].push_back({{"label",sample.label},{"kind",sample.kind},{"start_ms",sample.startMilliseconds-origin},{"end_ms",sample.endMilliseconds-origin},
                            {"vertex_ms",sample.vertexMilliseconds},{"fragment_ms",sample.fragmentMilliseconds},{"vertex_start_ms",sample.kind=="render"?sample.vertexStartMilliseconds-origin:0},{"vertex_end_ms",sample.kind=="render"?sample.vertexEndMilliseconds-origin:0},{"fragment_start_ms",sample.kind=="render"?sample.fragmentStartMilliseconds-origin:0},{"fragment_end_ms",sample.kind=="render"?sample.fragmentEndMilliseconds-origin:0},{"queue_delay_ms",sample.kind=="submission"?sample.startMilliseconds-sample.commitMilliseconds:0}});
                        report["frames"].push_back(std::move(entry));
                        if(i==8||i==39){auto pixels=renderer.readOutput();auto path=(std::filesystem::path(directory)/("coastal-performance-"+std::to_string(i)+".png")).string();
                            if(!stbi_write_png(path.c_str(),width,height,4,pixels.data(),width*4))throw std::runtime_error("Cannot save coastal performance image");}
                        if(i%10==0)std::cout<<"Coastal benchmark "<<i<<" wall "<<ms(start,completed)<<" ms, GPU "<<timing.milliseconds<<" ms, "<<samples.size()<<" counters\n"<<std::flush;
                    }
                    report["measurement"]="Release native frame: acquisition, asset collection, FFT/SWE/shadows/main render, presentation and completion; synchronized latency, not pipelined editor throughput. Counters sampled at original encoder boundaries without splitting submissions.";
                    std::ofstream(std::filesystem::path(directory)/"coastal-performance.json")<<report.dump(2)<<'\n';continue;
                }
                auto capture=[&](const std::string& suffix,bool rsm,bool only,bool sun,bool sky){
                    renderer.resetTemporal();
                    std::vector<double> gpuTimes;const int captureFrames=cloudScene?32:(diagnostics?64:16);
                    for(int i=0;i<captureFrames;++i){
                        glfwPollEvents();
                        // Drain the previous presentation fence before timing the
                        // current renderer submission; its completion must not
                        // overwrite this frame's GPU duration during waitIdle.
                        if(waterScene)device->waitIdle();
                        device->beginFrame();auto frame=collect();
                        frame.frame.shadows=manager->setting.enableShadow;frame.frame.ssao=manager->setting.enableSSAO;
                        const auto& ao=manager->setting;frame.frame.aoRadius=ao.aoRadius;frame.frame.aoBias=ao.aoBias;frame.frame.aoPower=ao.aoPower;frame.frame.aoHorizon=ao.aoHorizon;frame.frame.aoDenoise=ao.aoDenoise;frame.frame.aoSlices=ao.aoSlices;frame.frame.aoSteps=ao.aoSteps;
                        if(selection=="ao")frame.frame.taa=false;
                        frame.frame.rsm=rsm;frame.frame.rsmSettings=manager->setting.rsmSettings;
                        frame.frame.shadowSettings=manager->setting.shadowSettings;
                        frame.frame.rsmSettings.indirectOnly=only;frame.frame.rsmSettings.sunBounce=sun;frame.frame.rsmSettings.skyBounce=sky;
                        renderer.render(frame.frame,frame.packets,frame.exposure);
                        if(waterScene){device->waitIdle();auto timing=device->gpuTimingStats();if(i>=4 && timing.supported)gpuTimes.push_back(timing.milliseconds);}
                        if(diagnostics){
                            auto feedbackFrame=frame.frame;feedbackFrame.viewProjection=renderer.renderedViewProjection();
                            adapter.recordVirtualFeedback(feedbackFrame,renderer.depthView(),renderer.shadowVisibilityViews());
                        }
                        lastFrame=frame.frame;device->copyToBackbuffer(renderer.output());device->present();
                        if(cloudScene){device->waitIdle();auto timing=device->gpuTimingStats();if(i>=4 && timing.supported)gpuTimes.push_back(timing.milliseconds);}
                    }
                    if(cloudScene){
                        auto settings=scene->sky()->getComponent<Cloud>()->settings();
                        cloudMetrics[name+suffix]={{"backend",rhi::requestedBackend()==rhi::Backend::Metal?"Metal":"Vulkan"},
                            {"width",width},{"height",height},{"frames",captureFrames},{"time_seconds",galleryTime},
                            {"coverage",settings.coverage},{"density",settings.density},{"enabled",settings.enabled},
                            {"primary_steps",settings.steps},{"light_steps",settings.lightSteps},{"downsample",settings.downsample},
                            {"voxel",settings.voxel},{"voxel_resolution",settings.voxelResolution},{"temporal",settings.temporal},{"distance_skipping",settings.distanceSkipping},{"core_integration",settings.coreIntegration},{"storm",settings.storm},{"lightning",settings.lightning},
                            {"base_height",settings.baseHeight},{"thickness",settings.thickness},{"seed",settings.seed},
                            {"measurement","whole frame native command buffer, including presentation copy; first 4 frames excluded"},
                            {"gpu_ms_samples",gpuTimes}};
                        if(!gpuTimes.empty()){cloudMetrics[name+suffix]["mean_gpu_ms"]=std::accumulate(gpuTimes.begin(),gpuTimes.end(),0.)/gpuTimes.size();std::sort(gpuTimes.begin(),gpuTimes.end());cloudMetrics[name+suffix]["median_gpu_ms"]=(gpuTimes[(gpuTimes.size()-1)/2]+gpuTimes[gpuTimes.size()/2])*.5;}
                    }
                    if(waterScene){
                        const auto settings=scene->terrain()->getComponent<Ocean>()->settings();
                        const double domain=settings.MeshLength,focus=settings.gridFocus;
                        const auto camera=lastFrame.cameraPosition;
                        auto spacing=[&](double centre){centre=std::clamp(centre,-domain*.5,domain*.5);return focus*(std::asinh((domain*.5-centre)/focus)-std::asinh((-domain*.5-centre)/focus))/(settings.MeshSize-1);};
                        auto& metric=waterMetrics[name+suffix];
                        metric={{"backend",rhi::requestedBackend()==rhi::Backend::Metal?"Metal":"Vulkan"},{"width",width},{"height",height},{"frames",captureFrames},{"time_seconds",galleryTime},
                            {"camera_grid",settings.cameraGrid},{"grid_focus_m",settings.gridFocus},{"underwater_capture",settings.underwaterCapture},{"volume_integration",settings.volumeIntegration},
                            {"robust_refraction",settings.robustRefraction},{"multiple_scattering_lut",settings.multipleScattering},{"shallow_water",settings.shore.enabled},{"shore_foam",settings.shore.foam},{"wet_sand",settings.shore.wetSand},
                            {"underwater_view",settings.underwaterView},{"underwater_fog",settings.underwaterFog},{"short_wave_ripples",settings.shortWaveRipples},{"ripple_rms_height_m",settings.rippleRmsHeight},{"detail_strength",settings.detailStrength},
                            {"mesh_size",settings.MeshSize},{"mesh_domain_m",settings.MeshLength},{"detail_waves",settings.detailWaves},{"subsurface_strength",settings.subsurfaceStrength},
                            {"near_camera_cell_m",settings.cameraGrid?nlohmann::json({spacing(camera.x),spacing(camera.z)}):nlohmann::json({domain/(settings.MeshSize-1),domain/(settings.MeshSize-1)})},
                            {"measurement","main native rendering submission: capture, scene, water, TSAA and tone mapping; separately submitted FFT and shallow simulation excluded; first 4 frames excluded"},{"gpu_ms_samples",gpuTimes}};
                        if(!gpuTimes.empty()){metric["mean_gpu_ms"]=std::accumulate(gpuTimes.begin(),gpuTimes.end(),0.)/gpuTimes.size();std::sort(gpuTimes.begin(),gpuTimes.end());metric["median_gpu_ms"]=(gpuTimes[(gpuTimes.size()-1)/2]+gpuTimes[gpuTimes.size()/2])*.5;}
                        if(underwaterGallery){auto positions=renderer.readWaterCapture(lastFrame.oceans.at(0).id,true);size_t terrainPixels=0;
                            for(size_t i=3;i<positions.size();i+=4)if(positions[i]>1.5f)++terrainPixels;
                            metric["captured_terrain_pixels"]=terrainPixels;
                            if(suffix=="-above"&&!terrainPixels)throw std::runtime_error("Air-side capture did not mark the virtual-textured terrain");}
                    }
                    auto hdr=renderer.readHDR();double energy=0;float peak=0;for(size_t i=0;i<hdr.size();i+=4)for(int c=0;c<3;++c){if(!std::isfinite(hdr[i+c]))throw std::runtime_error("Nonfinite gallery HDR");energy+=hdr[i+c];peak=std::max(peak,hdr[i+c]);}
                    std::cout<<name<<suffix<<" HDR mean RGB "<<energy/(3*width*height)<<", peak "<<peak<<"\n";
                    auto pixels=renderer.readOutput();const auto path=(std::filesystem::path(directory)/(name+suffix+".png")).string();if(!stbi_write_png(path.c_str(),width,height,4,pixels.data(),width*4))throw std::runtime_error("Cannot save "+path);std::cout<<"Rendered "<<path<<'\n';
                };
                if(coastalMotion){
                    auto settings=scene->terrain()->getComponent<Ocean>()->settings();nlohmann::json motion;
                    std::vector<double> renderTimes,gpuTimes;
                    for(int i=0;i<=120;++i){device->waitIdle();device->beginFrame();auto frame=adapter.collect(scene,float(i)/12);
                        if(selection=="coastal-motion-camera"){
                            scene->mainCamera()->setPosition(glm::vec3(-12+6*std::sin(float(i)/120*6.2831853f),6,23+2*std::sin(float(i)/120*3.1415927f)));
                            frame=adapter.collect(scene,float(i)/12);
                        }
                        frame.frame.ssao=false;frame.frame.shadows=true;auto started=std::chrono::steady_clock::now();renderer.render(frame.frame,frame.packets,frame.exposure);device->waitIdle();
                        double renderMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
                        auto timing=device->gpuTimingStats();auto pixels=renderer.readOutput();
                        if(i>=4){renderTimes.push_back(renderMilliseconds);if(timing.supported)gpuTimes.push_back(timing.milliseconds);}
                        device->copyToBackbuffer(renderer.output());device->present();
                        char filename[64];std::snprintf(filename,sizeof(filename),"coastal-%04d.png",i);
                        auto path=(std::filesystem::path(directory)/filename).string();if(!stbi_write_png(path.c_str(),width,height,4,pixels.data(),width*4))throw std::runtime_error("Cannot save coastal motion");
                        if(i%12==0){auto state=renderer.readShoreWater(frame.frame.oceans[0].id),foam=renderer.readShoreWater(frame.frame.oceans[0].id,true);
                            size_t runup=0,residual=0;float peakFoam=0;for(size_t p=0;p<state.size();p+=4){if(!std::isfinite(state[p])||state[p]<0)throw std::runtime_error("Invalid moving shoreline state");
                                if(foam[p+3]>settings.seaLevel&&state[p]>.01)++runup;if(foam[p+3]>settings.seaLevel&&state[p]<.008&&foam[p+1]>.1)++residual;peakFoam=std::max(peakFoam,foam[p]);}
                            motion[std::to_string(i)]={{"time",float(i)/12},{"runup_cells",runup},{"residual_wet_cells",residual},{"peak_foam",peakFoam},{"main_gpu_ms",timing.milliseconds},
                                {"end_to_end_render_ms",renderMilliseconds},{"shallow_substeps",renderer.shoreSubsteps(frame.frame.oceans[0].id)}};
                            std::cout<<"Coastal motion "<<i<<"/120, runup "<<runup<<", residual wet sand "<<residual<<", foam "<<peakFoam<<'\n';
                        }
                    }
                    auto stats=[](std::vector<double> a){std::sort(a.begin(),a.end());return nlohmann::json{{"median_ms",a.empty()?0:(a[(a.size()-1)/2]+a[a.size()/2])*.5},{"p95_ms",a.empty()?0:a[size_t(std::ceil(a.size()*.95))-1]},{"samples",a}};};
                    motion["capture"]={{"backend",rhi::requestedBackend()==rhi::Backend::Metal?"Metal":"Vulkan"},{"width",width},{"height",height},{"duration_seconds",10},{"sampling_fps",12},{"moving_camera",selection=="coastal-motion-camera"},
                        {"shore_resolution",settings.shore.resolution},{"patch_length_m",settings.shore.length},{"main_gpu",stats(gpuTimes)},{"render_cpu_and_gpu",stats(renderTimes)},
                        {"measurement","main GPU excludes separately submitted FFT and shallow simulation; render CPU+GPU includes those submissions and waitIdle, excludes asset collection, screenshot and presentation; first 4 frames excluded"}};
                    std::ofstream(std::filesystem::path(directory)/"coastal-motion.json")<<motion.dump(2)<<'\n';continue;
                }
                if(selection=="ao"){
                    const auto previous=manager->setting;nlohmann::json metrics;
                    for(const auto& mode:std::vector<std::string>{"off","legacy","gtao-raw","gtao"}){
                        auto& setting=manager->setting;setting.enableSSAO=mode!="off";
                        setting.aoHorizon=mode!="legacy";setting.aoDenoise=mode=="gtao";
                        capture("-ao-"+mode,false,false,true,true);
                        auto visibility=renderer.readSSAO(),positions=renderer.readGBuffer(0);std::vector<uint8_t> image(visibility.size());
                        double sum=0;size_t covered=0;
                        for(size_t i=0;i<visibility.size();i+=4){
                            if(!std::isfinite(visibility[i]))throw std::runtime_error("Nonfinite AO gallery");
                            if(positions[i+3]>0){sum+=visibility[i];++covered;}
                            for(int c=0;c<3;++c)image[i+c]=uint8_t(glm::clamp(visibility[i],0.f,1.f)*255);image[i+3]=255;
                        }
                        auto path=(std::filesystem::path(directory)/(name+"-ao-"+mode+"-visibility.png")).string();
                        if(!stbi_write_png(path.c_str(),width,height,4,image.data(),width*4))throw std::runtime_error("Cannot save AO visibility");
                        metrics[mode]={{"mean_visibility_on_geometry",sum/std::max(size_t(1),covered)},{"covered_pixels",covered},{"radius",setting.aoRadius},{"bias",setting.aoBias},{"power",setting.aoPower},{"horizon",setting.aoHorizon},{"denoise",setting.aoDenoise},{"slices",setting.aoSlices},{"steps_per_side",setting.aoSteps}};
                    }
                    manager->setting=previous;metrics["capture"]={{"backend",rhi::requestedBackend()==rhi::Backend::Metal?"Metal":"Vulkan"},{"width",width},{"height",height},{"rsm",false},{"taa",false},{"time_seconds",galleryTime},{"frames_per_mode",16}};
                    std::ofstream file(std::filesystem::path(directory)/(name+"-ao-metrics.json"));file<<metrics.dump(2)<<'\n';if(!file)throw std::runtime_error("Cannot save AO metrics");
                } else {
                const bool gi=manager->setting.enableRSM;if(gi)capture("-direct",false,false,true,true);capture("",gi,false,true,true);
                if(diagnostics)exportGalleryDiagnostics(directory,name,device,adapter,renderer,lastFrame);
                if(cloudScene) {
                    auto counts=renderer.cloudTileCounts();auto cloud=renderer.readClouds();auto meta=renderer.readCloudMetadata();
                    const auto factor=scene->sky()->getComponent<Cloud>()->settings().downsample;
                    const int cw=(width+factor-1)/factor,ch=(height+factor-1)/factor;std::vector<uint8_t> transmittance(size_t(cw)*ch*4);
                    double opacity=0,steps=0;for(size_t i=0;i<cloud.size();i+=4){for(int c=0;c<4;c++)if(!std::isfinite(cloud[i+c]))throw std::runtime_error("Nonfinite cloud gallery");opacity+=1-cloud[i+3];steps+=meta[i+2];for(int c=0;c<3;c++)transmittance[i+c]=uint8_t(glm::clamp(1-cloud[i+3],0.f,1.f)*255);transmittance[i+3]=255;}
                    auto path=(std::filesystem::path(directory)/(name+"-opacity.png")).string();if(!stbi_write_png(path.c_str(),cw,ch,4,transmittance.data(),cw*4))throw std::runtime_error("Cannot save cloud opacity");
                    std::cout<<"Cloud tiles "<<counts[0]<<"/"<<counts[1]<<", mean opacity "<<opacity/(cw*ch)<<", mean primary steps "<<steps/(cw*ch)<<"\n";
                    cloudMetrics[name]["active_tiles"]=counts[0];cloudMetrics[name]["total_tiles"]=counts[1];cloudMetrics[name]["mean_opacity"]=opacity/(cw*ch);cloudMetrics[name]["mean_primary_steps"]=steps/(cw*ch);
                    auto settings=scene->sky()->getComponent<Cloud>()->settings();
                    if(settings.voxel){
                        double skipped=0;for(size_t i=3;i<meta.size();i+=4)skipped+=meta[i];cloudMetrics[name]["mean_skipped_samples"]=skipped/(cw*ch);
                        auto density=renderer.readCloudVoxels();auto distance=renderer.readCloudDistance();auto light=renderer.readCloudLight();
                        const int n=settings.voxelResolution,columns=n==128?16:8,z=n/2;
                        std::vector<uint8_t> slice(n*n*4);for(int y=0;y<n;y++)for(int x=0;x<n;x++){
                            size_t source=(((z/columns)*(n+2)+y+1)*(n+2)*columns+(z%columns)*(n+2)+x+1)*4;
                            size_t dest=(y*n+x)*4;for(int c=0;c<3;c++)slice[dest+c]=density[source];slice[dest+3]=255;
                        }
                        auto save=[&](const std::string& suffix,int w,int h,const std::vector<uint8_t>& pixels){auto path=(std::filesystem::path(directory)/(name+suffix+".png")).string();if(!stbi_write_png(path.c_str(),w,h,4,pixels.data(),w*4))throw std::runtime_error("Cannot save voxel diagnostic");};
                        save("-density-slice",n,n,slice);
                        slice.resize(32*32*4);for(int y=0;y<32;y++)for(int x=0;x<32;x++){
                            size_t source=((2*32+y)*256+x)*4,dest=(y*32+x)*4;
                            slice[dest]=std::min(int(distance[source])*8,255);slice[dest+1]=0;
                            slice[dest+2]=distance[source+2]?std::min(int(distance[source+1])*16+40,255):0;slice[dest+3]=255;
                        }save("-distance-slice",32,32,slice);
                        slice.resize(64*64*4);for(int y=0;y<64;y++)for(int x=0;x<64;x++){
                            size_t source=((4*66+y+1)*528+x+1)*4,dest=(y*64+x)*4;
                            for(int c=0;c<3;c++)slice[dest+c]=uint8_t(std::clamp(std::exp(-light[source]),0.f,1.f)*255);slice[dest+3]=255;
                        }save("-light-slice",64,64,slice);
                    }
                    auto component=scene->sky()->getComponent<Cloud>();
                    if(settings.voxel){
                        component->updateSettings([](auto& s){s.distanceSkipping=false;s.coreIntegration=false;});capture("-reference",false,false,true,true);
                        component->setSettings(settings);
                        if(settings.lightning>0){component->updateSettings([](auto& s){s.lightning=0;});capture("-no-flash",false,false,true,true);component->setSettings(settings);}
                    }
                    component->updateSettings([](auto& s){s.enabled=false;});capture("-clear",false,false,true,true);
                    auto metricPath=std::filesystem::path(directory)/(name+"-metrics.json");std::ofstream metricFile(metricPath);metricFile<<cloudMetrics.dump(2)<<'\n';if(!metricFile)throw std::runtime_error("Cannot save cloud metrics");
                }
                if(name=="shadow-test") {
                    const auto previous=manager->setting.shadowSettings;
                    manager->setting.shadowSettings.pcss=false;capture("-pcf",false,false,true,true);
                    manager->setting.shadowSettings.pcss=true;manager->setting.shadowSettings.sunAngularRadius=.04f;
                    capture("-pcss-wide",false,false,true,true);manager->setting.shadowSettings=previous;
                }
                if(name=="terrain"){
                    auto terrain=std::static_pointer_cast<TerrainComponent>(scene->terrain()->GetComponent("TerrainComponent"));
                    terrain->setPolyMode(GL_LINE);capture("-wireframe",false,false,true,true);terrain->setPolyMode(GL_FILL);
                }
                if(name=="coastal-beach"||name=="coastal-water") {
                    auto ocean=scene->terrain()->getComponent<Ocean>();auto original=ocean->settings();
                    ocean->updateSettings([](auto& v){v.robustRefraction=false;v.multipleScattering=false;v.shore.enabled=false;});capture("-baseline",false,false,true,true);
                    ocean->setSettings(original);ocean->updateSettings([](auto& v){v.multipleScattering=false;});capture("-no-multiple",false,false,true,true);
                    ocean->setSettings(original);ocean->updateSettings([](auto& v){v.shore.foam=false;});capture("-no-foam",false,false,true,true);
                    ocean->setSettings(original);ocean->updateSettings([](auto& v){v.shore.wetSand=false;});capture("-no-wet-sand",false,false,true,true);
                    ocean->setSettings(original);ocean->updateSettings([](auto& v){v.opticalDebug=4;});capture("-hit-source",false,false,true,true);ocean->setSettings(original);
                }
                if(underwaterGallery){
                    auto ocean=scene->terrain()->getComponent<Ocean>();const auto original=ocean->settings();
                    ocean->updateSettings([](auto& s){s.shortWaveRipples=false;});capture("-no-ripples",false,false,true,true);ocean->setSettings(original);
                    galleryTime=8.35f;capture("-ripples-later",false,false,true,true);galleryTime=8;
                    ocean->updateSettings([](auto& s){s.underwaterFog=false;});capture("-no-fog",false,false,true,true);ocean->setSettings(original);
                    scene->mainCamera()->setAngles(80,75);capture("-snell-window",false,false,true,true);
                    scene->mainCamera()->setAngles(80,-20);capture("-bottom",false,false,true,true);
                    scene->mainCamera()->setPosition({-8,2,-28});capture("-above",false,false,true,true);
                }
                if(name=="ocean" || name=="ocean-clear") {
                    auto ocean=std::static_pointer_cast<Ocean>(scene->terrain()->GetComponent("Ocean"));
                    if(name=="ocean") {
                        const bool detail=ocean->settings().detailWaves;ocean->updateSettings([&](auto& value){value.detailWaves=false;});capture("-no-detail",false,false,true,true);ocean->updateSettings([&](auto& value){value.detailWaves=detail;});
                        const float scattering=ocean->settings().subsurfaceStrength;ocean->updateSettings([&](auto& value){value.subsurfaceStrength=0;});capture("-no-scattering",false,false,true,true);ocean->updateSettings([&](auto& value){value.subsurfaceStrength=scattering;});
                    } else {
                        const bool refract=ocean->settings().refraction;const float scattering=ocean->settings().subsurfaceStrength;
                        ocean->updateSettings([&](auto& value){value.refraction=false;});ocean->updateSettings([&](auto& value){value.subsurfaceStrength=0;});capture("-opaque",false,false,true,true);ocean->updateSettings([&](auto& value){value.refraction=refract;});ocean->updateSettings([&](auto& value){value.subsurfaceStrength=scattering;});
                    }
                }
                if(waterScene){std::ofstream file(std::filesystem::path(directory)/(name+"-water-metrics.json"));file<<waterMetrics.dump(2)<<'\n';if(!file)throw std::runtime_error("Cannot save water metrics");}
                if(name=="sky") {
                    auto atmo=std::static_pointer_cast<Atmosphere>(scene->sky()->GetComponent("Atmosphere"));
                    auto pointSun=[&](float elevation,float fov,float pitch) {
                        atmo->updateSettings([&](auto& value){value.sunAngle=elevation;});scene->setCamera(std::make_shared<Camera>(glm::vec3(0,2,0),glm::vec3(0,1,0),-90,pitch,float(width)/height));
                        scene->mainCamera()->setZoom(fov);scene->mainCamera()->setExposure(1);
                    };
                    pointSun(30,10,30);capture("-sun-closeup",false,false,true,true);
                    pointSun(45,60,25);capture("-day",false,false,true,true);
                    pointSun(0,20,2);capture("-sunset",false,false,true,true);
                    pointSun(-5,30,0);capture("-night",false,false,true,true);
                }
                if(gi && (name=="sponza" || name=="san-miguel" || name=="sibenik")){capture("-indirect",true,true,true,true);capture("-sun-indirect",true,true,true,false);capture("-sky-indirect",true,true,false,true);}
                } // Regular gallery selection.
            }
            scene->destroy();
        }
        manager->releaseNative();rhi::shutdown();glfwDestroyWindow(window);glfwTerminate();
    }catch(...){try{rhi::shutdown();}catch(...){}if(window)glfwDestroyWindow(window);glfwTerminate();throw;}
}
}
