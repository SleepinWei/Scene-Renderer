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
    const bool diagnostics=selection=="diagnostics";
    const bool water=selection=="ocean" || selection=="ocean-clear" || selection=="mountain-lake" || selection=="mountain-lake-ground" || selection=="mountain-lake-beach";int width=water?1920:960,height=water?1080:720;
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
                const bool cloudScene=name.rfind("clouds",0)==0;
                nlohmann::json cloudMetrics;
                SceneSnapshotBuilder diagnosticBuilder;
                auto collect=[&]() {
                    if(!diagnostics)return adapter.collect(scene,8);
                    // Prepare CPU sources synchronously, then exercise the editor's
                    // asynchronous page IO and depth-feedback path on the GPU.
                    auto snapshot=*diagnosticBuilder.capture(scene,8,width,height,true);
                    snapshot.asynchronousStreaming=true;
                    return adapter.resolve(snapshot);
                };
                auto capture=[&](const std::string& suffix,bool rsm,bool only,bool sun,bool sky){
                    renderer.resetTemporal();
                    std::vector<double> gpuTimes;const int captureFrames=cloudScene?32:(diagnostics?64:16);
                    for(int i=0;i<captureFrames;++i){
                        glfwPollEvents();device->beginFrame();auto frame=collect();
                        frame.frame.shadows=manager->setting.enableShadow;frame.frame.ssao=manager->setting.enableSSAO;
                        frame.frame.rsm=rsm;frame.frame.rsmSettings=manager->setting.rsmSettings;
                        frame.frame.shadowSettings=manager->setting.shadowSettings;
                        frame.frame.rsmSettings.indirectOnly=only;frame.frame.rsmSettings.sunBounce=sun;frame.frame.rsmSettings.skyBounce=sky;
                        renderer.render(frame.frame,frame.packets,frame.exposure);
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
                            {"width",width},{"height",height},{"frames",captureFrames},{"time_seconds",8},
                            {"coverage",settings.coverage},{"density",settings.density},{"enabled",settings.enabled},
                            {"primary_steps",settings.steps},{"light_steps",settings.lightSteps},{"downsample",settings.downsample},
                            {"base_height",settings.baseHeight},{"thickness",settings.thickness},{"seed",settings.seed},
                            {"measurement","whole frame native command buffer, including presentation copy; first 4 frames excluded"},
                            {"gpu_ms_samples",gpuTimes}};
                        if(!gpuTimes.empty()){cloudMetrics[name+suffix]["mean_gpu_ms"]=std::accumulate(gpuTimes.begin(),gpuTimes.end(),0.)/gpuTimes.size();std::sort(gpuTimes.begin(),gpuTimes.end());cloudMetrics[name+suffix]["median_gpu_ms"]=(gpuTimes[(gpuTimes.size()-1)/2]+gpuTimes[gpuTimes.size()/2])*.5;}
                    }
                    auto hdr=renderer.readHDR();double energy=0;float peak=0;for(size_t i=0;i<hdr.size();i+=4)for(int c=0;c<3;++c){if(!std::isfinite(hdr[i+c]))throw std::runtime_error("Nonfinite gallery HDR");energy+=hdr[i+c];peak=std::max(peak,hdr[i+c]);}
                    std::cout<<name<<suffix<<" HDR mean RGB "<<energy/(3*width*height)<<", peak "<<peak<<"\n";
                    auto pixels=renderer.readOutput();const auto path=(std::filesystem::path(directory)/(name+suffix+".png")).string();if(!stbi_write_png(path.c_str(),width,height,4,pixels.data(),width*4))throw std::runtime_error("Cannot save "+path);std::cout<<"Rendered "<<path<<'\n';
                };
                const bool gi=manager->setting.enableRSM;if(gi)capture("-direct",false,false,true,true);capture("",gi,false,true,true);
                if(diagnostics)exportGalleryDiagnostics(directory,name,device,adapter,renderer,lastFrame);
                if(name=="clouds" || name=="clouds-sunset" || name=="clouds-storm") {
                    auto counts=renderer.cloudTileCounts();auto cloud=renderer.readClouds();auto meta=renderer.readCloudMetadata();
                    const auto factor=scene->sky()->getComponent<Cloud>()->settings().downsample;
                    const int cw=(width+factor-1)/factor,ch=(height+factor-1)/factor;std::vector<uint8_t> transmittance(size_t(cw)*ch*4);
                    double opacity=0,steps=0;for(size_t i=0;i<cloud.size();i+=4){for(int c=0;c<4;c++)if(!std::isfinite(cloud[i+c]))throw std::runtime_error("Nonfinite cloud gallery");opacity+=1-cloud[i+3];steps+=meta[i+2];for(int c=0;c<3;c++)transmittance[i+c]=uint8_t(glm::clamp(1-cloud[i+3],0.f,1.f)*255);transmittance[i+3]=255;}
                    auto path=(std::filesystem::path(directory)/(name+"-opacity.png")).string();if(!stbi_write_png(path.c_str(),cw,ch,4,transmittance.data(),cw*4))throw std::runtime_error("Cannot save cloud opacity");
                    std::cout<<"Cloud tiles "<<counts[0]<<"/"<<counts[1]<<", mean opacity "<<opacity/(cw*ch)<<", mean primary steps "<<steps/(cw*ch)<<"\n";
                    cloudMetrics[name]["active_tiles"]=counts[0];cloudMetrics[name]["total_tiles"]=counts[1];cloudMetrics[name]["mean_opacity"]=opacity/(cw*ch);cloudMetrics[name]["mean_primary_steps"]=steps/(cw*ch);
                    auto component=scene->sky()->getComponent<Cloud>();component->updateSettings([](auto& s){s.enabled=false;});capture("-clear",false,false,true,true);
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
            }
            scene->destroy();
        }
        manager->releaseNative();rhi::shutdown();glfwDestroyWindow(window);glfwTerminate();
    }catch(...){try{rhi::shutdown();}catch(...){}if(window)glfwDestroyWindow(window);glfwTerminate();throw;}
}
}
