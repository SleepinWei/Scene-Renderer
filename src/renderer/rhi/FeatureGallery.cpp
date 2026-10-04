#include "renderer/rhi/FeatureScenes.h"
#include "renderer/rhi/SceneAdapter.h"
#include "rhi/ShaderAssets.h"
#include "renderer/RenderScene.h"
#include "system/RenderManager.h"
#include "system/InputManager.h"
#include "utils/Utils.h"
#include "utils/Camera.h"
#include "component/Atmosphere.h"
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
    else if(selection=="gi")names={"sponza","san-miguel"};else names={selection};
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
                auto capture=[&](const std::string& suffix,bool rsm,bool only,bool sun,bool sky){
                    renderer.resetTemporal();
                    for(int i=0;i<16;++i){glfwPollEvents();device->beginFrame();auto frame=adapter.collect(scene,8);frame.frame.shadows=manager->setting.enableShadow;frame.frame.ssao=manager->setting.enableSSAO;frame.frame.rsm=rsm;frame.frame.rsmSettings=manager->setting.rsmSettings;frame.frame.shadowSettings=manager->setting.shadowSettings;frame.frame.rsmSettings.indirectOnly=only;frame.frame.rsmSettings.sunBounce=sun;frame.frame.rsmSettings.skyBounce=sky;renderer.render(frame.frame,frame.packets,frame.exposure);device->copyToBackbuffer(renderer.output());device->present();}
                    auto hdr=renderer.readHDR();double energy=0;float peak=0;for(size_t i=0;i<hdr.size();i+=4)for(int c=0;c<3;++c){if(!std::isfinite(hdr[i+c]))throw std::runtime_error("Nonfinite gallery HDR");energy+=hdr[i+c];peak=std::max(peak,hdr[i+c]);}
                    std::cout<<name<<suffix<<" HDR mean RGB "<<energy/(3*width*height)<<", peak "<<peak<<"\n";
                    auto pixels=renderer.readOutput();const auto path=(std::filesystem::path(directory)/(name+suffix+".png")).string();if(!stbi_write_png(path.c_str(),width,height,4,pixels.data(),width*4))throw std::runtime_error("Cannot save "+path);std::cout<<"Rendered "<<path<<'\n';
                };
                const bool gi=manager->setting.enableRSM;if(gi)capture("-direct",false,false,true,true);capture("",gi,false,true,true);
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
                if(gi && (name=="sponza" || name=="san-miguel")){capture("-indirect",true,true,true,true);capture("-sun-indirect",true,true,true,false);capture("-sky-indirect",true,true,false,true);}
            }
            scene->destroy();
        }
        manager->releaseNative();rhi::shutdown();glfwDestroyWindow(window);glfwTerminate();
    }catch(...){try{rhi::shutdown();}catch(...){}if(window)glfwDestroyWindow(window);glfwTerminate();throw;}
}
}
