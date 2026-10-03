#include "PT/CpuPathTracer.h"
#include "renderer/rhi/AtmosphereBake.h"
#include "renderer/rhi/FeatureScenes.h"
#include "renderer/RenderScene.h"
#include "utils/Camera.h"
#include "utils/Utils.h"
#include "system/InputManager.h"
#include "rhi/Device.h"
#include <GLFW/glfw3.h>
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <limits>
#include <cstdlib>
namespace pt {
namespace {
uint32_t number(const std::string &s,const char *name,uint32_t maximum) {
    size_t end=0;const auto value=std::stoull(s,&end);if(s.empty() || s[0]=='-' || end!=s.size() || !value || value>maximum)throw std::invalid_argument(std::string("PT: invalid ")+name);return uint32_t(value);
}
}
int runCommandLine(int argc,char **argv) {
    Options options;std::string name="sponza",prefix,environmentPath;bool sky=true;int argument=2;
    if(argument<argc && std::string(argv[argument]).rfind("--",0)!=0)name=argv[argument++];
    for(int i=argument;i<argc;++i) {
        const std::string flag=argv[i];auto value=[&](){if(++i>=argc)throw std::invalid_argument("PT: missing argument for "+flag);return std::string(argv[i]);};
        if(flag=="--pt-size"){const auto size=value();const auto at=size.find('x');if(at==std::string::npos)throw std::invalid_argument("PT: size must be WIDTHxHEIGHT");options.width=number(size.substr(0,at),"width",16384);options.height=number(size.substr(at+1),"height",16384);}
        else if(flag=="--pt-samples")options.samples=number(value(),"samples",1048576);
        else if(flag=="--pt-bounces")options.maxDepth=number(value(),"bounces",128);
        else if(flag=="--pt-threads")options.threads=number(value(),"threads",256);
        else if(flag=="--pt-seed")options.seed=number(value(),"seed",UINT32_MAX);
        else if(flag=="--pt-exposure"){const auto s=value();size_t end;options.exposure=std::stof(s,&end);if(end!=s.size() || !std::isfinite(options.exposure) || options.exposure<=0)throw std::invalid_argument("PT: invalid exposure");}
        else if(flag=="--pt-output")prefix=value();
        else if(flag=="--pt-environment")environmentPath=value();
        else if(flag=="--pt-no-sky")sky=false;
        else if(flag=="--backend")value();
        else throw std::invalid_argument("PT: unknown argument "+flag);
    }
    if(prefix.empty())prefix="build/path-tracing/"+name;
    const auto parent=std::filesystem::path(prefix).parent_path();if(!parent.empty())std::filesystem::create_directories(parent);
    auto scene=render::makeClassicScene(name);scene->mainCamera()->setAspect(float(options.width)/options.height);
    std::shared_ptr<const render::RenderWorldSnapshot> snapshot;
    {render::SceneSnapshotBuilder builder;snapshot=builder.capture(scene,8,options.width,options.height);}
    render::BakedAtmosphere baked;
    if(sky && environmentPath.empty() && snapshot->frame.sky) {
#ifdef SCENERENDERER_HAS_VULKAN
        if(rhi::requestedBackend()==rhi::Backend::Vulkan)rhi::configureVulkanWindowing();
#endif
        if(!glfwInit())throw std::runtime_error("PT: sky bake GLFW initialization failed");
        GLFWwindow *window=nullptr;
        try{
            glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
            if(createWindow(window,64,64)!=0 || gladInit()!=0)throw std::runtime_error("PT: sky bake device initialization failed");
            baked=render::bakeAtmosphere(rhi::graphicsDevice(),snapshot->frame);
            rhi::shutdown();glfwDestroyWindow(window);window=nullptr;glfwTerminate();
        }catch(...){try{rhi::shutdown();}catch(...){}if(window)glfwDestroyWindow(window);glfwTerminate();throw;}
    }
    CpuScene cpu(*snapshot);
    // Apply the same exposure by default, while retaining an explicit CLI override.
    bool explicitExposure=false;for(int i=2;i<argc;++i)explicitExposure|=std::string(argv[i])=="--pt-exposure";
    if(!explicitExposure)options.exposure=snapshot->exposure;
    if(!environmentPath.empty()) {
        int w,h,channels;stbi_set_flip_vertically_on_load_thread(false);auto pixels=stbi_loadf(environmentPath.c_str(),&w,&h,&channels,3);
        if(!pixels)throw std::runtime_error("PT: cannot load HDR environment "+environmentPath);
        std::vector<glm::vec3> radiance;try{radiance.resize(size_t(w)*h);for(size_t i=0;i<radiance.size();++i)radiance[i]={pixels[i*3],pixels[i*3+1],pixels[i*3+2]};}catch(...){stbi_image_free(pixels);throw;}stbi_image_free(pixels);
        cpu.environment=std::make_shared<Environment>(uint32_t(w),uint32_t(h),std::move(radiance));
        std::ifstream sidecar(environmentPath+".json");
        if(sidecar){nlohmann::json sun;sidecar>>sun;auto direction=sun.at("sun_direction").get<std::array<float,3>>();auto irradiance=sun.at("sun_irradiance").get<std::array<float,3>>();cpu.sunDirection={direction[0],direction[1],direction[2]};cpu.sunIrradiance={irradiance[0],irradiance[1],irradiance[2]};cpu.sunRadius=sun.at("sun_radius").get<float>();if(!std::isfinite(cpu.sunRadius) || cpu.sunRadius<=0 || cpu.sunRadius>.1f || !std::isfinite(glm::length(cpu.sunDirection)) || glm::length(cpu.sunDirection)<.99f || glm::length(cpu.sunDirection)>1.01f || glm::any(glm::lessThan(cpu.sunIrradiance,glm::vec3(0))) || !std::isfinite(glm::length(cpu.sunIrradiance)))throw std::invalid_argument("PT: invalid environment sun sidecar");}

    }else if(!baked.radiance.empty()) {
        cpu.environment=std::make_shared<Environment>(baked.width,baked.height,std::move(baked.radiance));cpu.sunDirection=baked.sunDirection;cpu.sunIrradiance=baked.sunIrradiance;cpu.sunRadius=baked.sunRadius;
    }
    if(cpu.environment) {
        std::vector<float> pixels;pixels.reserve(cpu.environment->pixels().size()*3);for(auto value:cpu.environment->pixels()){pixels.push_back(value.r);pixels.push_back(value.g);pixels.push_back(value.b);}
        const auto skyPath=prefix+"-environment.hdr";
        if(!stbi_write_hdr(skyPath.c_str(),int(cpu.environment->width()),int(cpu.environment->height()),3,pixels.data()))throw std::runtime_error("PT: cannot save baked sky HDR");
        if(cpu.sunRadius>0){std::ofstream sidecar(skyPath+".json");sidecar<<nlohmann::json{{"sun_direction",{cpu.sunDirection.x,cpu.sunDirection.y,cpu.sunDirection.z}},{"sun_irradiance",{cpu.sunIrradiance.x,cpu.sunIrradiance.y,cpu.sunIrradiance.z}},{"sun_radius",cpu.sunRadius}}.dump(2)<<'\n';if(!sidecar)throw std::runtime_error("PT: cannot save solar environment metadata");}
    }
    snapshot.reset();scene->destroy();scene.reset();
    auto result=render(cpu,options,[&](const Image &image){const auto progressPrefix=prefix+"-"+std::to_string(image.samples)+"spp";writeImage(image,options.exposure,progressPrefix);writeReport(image,cpu,options,progressPrefix,name);});
    writeImage(result,options.exposure,prefix);writeReport(result,cpu,options,prefix,name);
    std::cout<<"CPU PT output: "<<prefix<<".png / .pfm / .json\n";return 0;
}
} // namespace pt
