#include "PT/CpuPathTracer.h"
#include "PT/GpuPathTracer.h"
#include "PT/ValidationScenes.h"
#include "PT/Denoiser.h"
#include "PT/ProceduralCapture.h"
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
class DeviceContext {
  public:
    ~DeviceContext(){close();}
    void open(){
#ifdef SCENERENDERER_HAS_VULKAN
        if(rhi::requestedBackend()==rhi::Backend::Vulkan)rhi::configureVulkanWindowing();
#endif
        if(!glfwInit())throw std::runtime_error("PT: GLFW initialization failed");initialized_=true;
        try {glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);if(createWindow(window_,64,64)!=0 || gladInit()!=0)throw std::runtime_error("PT: device initialization failed");}
        catch(...){close();throw;}
    }
    void close() noexcept {if(!initialized_)return;try{rhi::shutdown();}catch(...){}if(window_)glfwDestroyWindow(window_);window_=nullptr;glfwTerminate();initialized_=false;}
  private:
    GLFWwindow *window_=nullptr;bool initialized_=false;
};
float realNumber(const std::string &s,const char *name,bool zero=false) {size_t end=0;float value=std::stof(s,&end);if(end!=s.size()||!std::isfinite(value)||(zero?value<0:value<=0))throw std::invalid_argument(std::string("PT: invalid ")+name);return value;}

}
int runCommandLine(int argc,char **argv) {
    const bool gpu=argc>1 && std::string(argv[1])=="--path-trace-gpu";
    if(gpu && rhi::requestedBackend()==rhi::Backend::OpenGL)throw std::invalid_argument("GPU PT requires Metal or Vulkan");
    Options options;DenoiseOptions denoiseOptions;CaptureOptions captureOptions;float time=8;std::string denoiseInput,dragonPath="samples/assets/pt/dragon/dragon_vrip.ply";
    std::string name="sponza",prefix,environmentPath;bool sky=true,glass=true,filter=false;int argument=2;
    if(argument<argc && std::string(argv[argument]).rfind("--",0)!=0)name=argv[argument++];
    for(int i=argument;i<argc;++i) {
        const std::string flag=argv[i];auto value=[&](){if(++i>=argc)throw std::invalid_argument("PT: missing argument for "+flag);return std::string(argv[i]);};
        if(flag=="--pt-size"){const auto size=value();const auto at=size.find('x');if(at==std::string::npos)throw std::invalid_argument("PT: size must be WIDTHxHEIGHT");options.width=number(size.substr(0,at),"width",16384);options.height=number(size.substr(at+1),"height",16384);}
        else if(flag=="--pt-samples")options.samples=number(value(),"samples",1048576);
        else if(flag=="--pt-bounces")options.maxDepth=number(value(),"bounces",128);
        else if(flag=="--pt-threads")options.threads=number(value(),"threads",256);
        else if(flag=="--pt-seed")options.seed=number(value(),"seed",UINT32_MAX);
        else if(flag=="--pt-exposure"){const auto s=value();size_t end;options.exposure=std::stof(s,&end);if(end!=s.size() || !std::isfinite(options.exposure) || options.exposure<=0)throw std::invalid_argument("PT: invalid exposure");}
        else if(flag=="--pt-sampler"){auto sampler=value();if(sampler!="sobol" && sampler!="pcg")throw std::invalid_argument("PT: sampler must be sobol or pcg");options.sobol=sampler=="sobol";}
        else if(flag=="--pt-guiding")options.guiding=true;
        else if(flag=="--pt-cache")options.radianceCache=true;
        else if(flag=="--pt-bdpt"){options.bdpt=true;options.adaptive=false;}
        else if(flag=="--pt-no-glass")glass=false;
        else if(flag=="--pt-time")time=realNumber(value(),"frozen time",true);
        else if(flag=="--pt-terrain-grid")captureOptions.terrainGrid=number(value(),"terrain grid",1025);
        else if(flag=="--pt-ocean-grid")captureOptions.oceanGrid=number(value(),"ocean grid",1025);
        else if(flag=="--pt-texture-size")captureOptions.textureExtent=number(value(),"capture texture size",4096);
        else if(flag=="--pt-grass-limit")captureOptions.grassLimit=number(value(),"grass limit",1048576);
        else if(flag=="--pt-no-grass")captureOptions.grass=false;
        else if(flag=="--pt-dragon-mesh")dragonPath=value();
        else if(flag=="--pt-denoise")filter=true;
        else if(flag=="--pt-denoise-device"){denoiseOptions.device=value();filter=true;}
        else if(flag=="--pt-denoise-color-only"){denoiseOptions.auxiliary=false;filter=true;}
        else if(flag=="--pt-denoise-input"){denoiseInput=value();filter=true;}
        else if(flag=="--pt-training-samples")options.trainingSamples=number(value(),"training samples",512);
        else if(flag=="--pt-guide-cell")options.guideCellSize=realNumber(value(),"guide cell size");
        else if(flag=="--pt-cache-min")options.cacheMinimum=number(value(),"cache minimum",8192);
        else if(flag=="--pt-cache-depth")options.cacheDepth=number(value(),"cache depth",128);
        else if(flag=="--pt-fixed")options.adaptive=false;
        else if(flag=="--pt-adaptive")options.adaptive=true;
        else if(flag=="--pt-min-samples")options.minimumSamples=number(value(),"minimum samples",1048576);
        else if(flag=="--pt-error")options.relativeError=realNumber(value(),"relative error");
        else if(flag=="--pt-absolute-error")options.absoluteError=realNumber(value(),"absolute error",true);
        else if(flag=="--pt-output")prefix=value();
        else if(flag=="--pt-environment")environmentPath=value();
        else if(flag=="--pt-no-sky")sky=false;
        else if(flag=="--backend")value();
        else throw std::invalid_argument("PT: unknown argument "+flag);
    }
    if(filter&&!denoiserAvailable())throw std::runtime_error("Open Image Denoise is not enabled in this build; see docs/path-tracing-denoising.md");
    if(denoiseOptions.device!="auto"&&denoiseOptions.device!="cpu"&&denoiseOptions.device!="metal")throw std::invalid_argument("PT: denoise device must be auto, cpu or metal");
    if(!denoiseInput.empty()){
        auto result=readPfm(denoiseInput);const auto inputPrefix=std::filesystem::path(denoiseInput).replace_extension().string();
        if(denoiseOptions.auxiliary)for(auto feature:{std::pair<const char*,std::vector<glm::vec3>*>{"-albedo.pfm",&result.albedo},{"-normal.pfm",&result.normal}}){const auto path=inputPrefix+feature.first;if(std::filesystem::exists(path)){auto auxiliary=readPfm(path);if(auxiliary.width!=result.width||auxiliary.height!=result.height)throw std::invalid_argument("PT: auxiliary PFM dimensions do not match beauty");*feature.second=std::move(auxiliary.radiance);}}
        if(prefix.empty())prefix=inputPrefix+"-filtered";
        denoise(result,denoiseOptions);writeImage(result,options.exposure,prefix);
        std::ofstream report(prefix+".json");report<<nlohmann::json{{"input",denoiseInput},{"width",result.width},{"height",result.height},{"exposure",options.exposure},{"denoiser",result.denoiser},{"denoise_device",result.denoiseDevice},{"denoise_seconds",result.denoiseSeconds},{"denoise_auxiliary",result.denoiseAuxiliary}}.dump(2)<<'\n';if(!report)throw std::runtime_error("PT: cannot save denoise report");
        return 0;
    }
    if(!gpu && (options.guiding || options.radianceCache))throw std::invalid_argument("Guiding/cache currently require --path-trace-gpu");
    if(gpu && options.bdpt)throw std::invalid_argument("BDPT is a CPU reference: use --path-trace --pt-bdpt");
    if(prefix.empty())prefix="build/path-tracing/"+name;
    const auto parent=std::filesystem::path(prefix).parent_path();if(!parent.empty())std::filesystem::create_directories(parent);
    std::shared_ptr<RenderScene> scene;std::vector<DielectricMaterial> dielectrics;
    std::shared_ptr<const render::RenderWorldSnapshot> snapshot;
    if(name=="caustics"||name=="dragon-caustics"){auto validation=name=="caustics"?makeCausticsScene(options.width,options.height,glass):makeDragonScene(options.width,options.height,dragonPath,glass);snapshot=std::make_shared<render::RenderWorldSnapshot>(std::move(validation.snapshot));dielectrics=std::move(validation.dielectrics);}
    else {scene=render::makeClassicScene(name);scene->mainCamera()->setAspect(float(options.width)/options.height);render::SceneSnapshotBuilder builder;snapshot=builder.capture(scene,time,options.width,options.height);}
    render::BakedAtmosphere baked;
    DeviceContext context;
    const bool proceduralDevice=!snapshot->frame.oceans.empty()||(snapshot->terrain&&captureOptions.grass&&snapshot->terrain->source->grass);
    if(gpu || proceduralDevice || (sky && environmentPath.empty() && snapshot->frame.sky))context.open();
    if(sky && environmentPath.empty() && snapshot->frame.sky)baked=render::bakeAtmosphere(rhi::graphicsDevice(),snapshot->frame);
    snapshot=std::make_shared<render::RenderWorldSnapshot>(captureProcedural(*snapshot,proceduralDevice?rhi::graphicsDevice():nullptr,captureOptions));
    if(!gpu)context.close();
    CpuScene cpu(*snapshot,dielectrics);
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
    snapshot.reset();if(scene){scene->destroy();scene.reset();}
    auto save=[&](const Image &image){const auto progressPrefix=prefix+"-"+std::to_string(image.samples)+"spp";writeImage(image,options.exposure,progressPrefix);writeReport(image,cpu,options,progressPrefix,name);};
    auto result=gpu?renderGpu(cpu,options,rhi::graphicsDevice(),save):render(cpu,options,save);
    if(filter)denoise(result,denoiseOptions);
    writeImage(result,options.exposure,prefix);writeReport(result,cpu,options,prefix,name);
    std::cout<<result.execution<<" PT output: "<<prefix<<".png / .pfm / .json\n";return 0;
}
} // namespace pt
