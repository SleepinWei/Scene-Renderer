#include "renderer/rhi/FeatureScenes.h"
#include "renderer/rhi/SceneAdapter.h"
#include "rhi/ShaderAssets.h"
#include "renderer/RenderScene.h"
#include "system/RenderManager.h"
#include "system/InputManager.h"
#include "utils/Utils.h"
#include "utils/Camera.h"
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
    const bool water=selection=="ocean" || selection=="ocean-clear";int width=water?1920:960,height=water?1080:720;
#ifdef __APPLE__
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER,GLFW_FALSE);
#endif
    try {
        if(createWindow(window,width,height)!=0 || gladInit()!=0)throw std::runtime_error("Gallery device initialization failed");
        auto device=rhi::graphicsDevice();glfwGetFramebufferSize(window,&width,&height);framebuffer_size_callback(window,width,height);
        std::filesystem::create_directories(directory);auto manager=RenderManager::GetInstance();manager->init();
        for(const auto& name:names){
            auto scene=makeClassicScene(name);scene->main_camera->aspect_ratio=float(width)/height;
            {
                SceneAdapter adapter(device);ForwardPbrRenderer renderer(device,rhi::defaultShaderDirectory(),width,height,PbrPath::Scene);
                auto capture=[&](const std::string& suffix,bool rsm,bool only,bool sun,bool sky){
                    for(int i=0;i<16;++i){glfwPollEvents();device->beginFrame();auto frame=adapter.collect(scene,8);frame.frame.shadows=manager->setting.enableShadow;frame.frame.ssao=manager->setting.enableSSAO;frame.frame.rsm=rsm;frame.frame.rsmSettings=manager->setting.rsmSettings;frame.frame.rsmSettings.indirectOnly=only;frame.frame.rsmSettings.sunBounce=sun;frame.frame.rsmSettings.skyBounce=sky;renderer.render(frame.frame,frame.packets,frame.exposure);device->copyToBackbuffer(renderer.output());device->present();}
                    auto pixels=renderer.readOutput();const auto path=(std::filesystem::path(directory)/(name+suffix+".png")).string();if(!stbi_write_png(path.c_str(),width,height,4,pixels.data(),width*4))throw std::runtime_error("Cannot save "+path);std::cout<<"Rendered "<<path<<'\n';
                };
                const bool gi=manager->setting.enableRSM;if(gi)capture("-direct",false,false,true,true);capture("",gi,false,true,true);
                if(gi && (name=="sponza" || name=="san-miguel")){capture("-indirect",true,true,true,true);capture("-sun-indirect",true,true,true,false);capture("-sky-indirect",true,true,false,true);}
            }
            scene->destroy();
        }
        manager->releaseNative();rhi::shutdown();glfwDestroyWindow(window);glfwTerminate();
    }catch(...){try{rhi::shutdown();}catch(...){}if(window)glfwDestroyWindow(window);glfwTerminate();throw;}
}
}
