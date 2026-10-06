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
}
int main(int argc,char **argv){try{
    pt::Options options;options.adaptive=false;std::string backend="CPU",input,output,fixture;bool selfTest=false,filter=false;
    for(int i=1;i<argc;++i){std::string flag=argv[i];auto value=[&](){if(++i>=argc)throw std::invalid_argument("Missing value for "+flag);return std::string(argv[i]);};
        if(flag=="--fixture")fixture=value();else if(flag=="--backend")backend=value();else if(flag=="--scene")input=value();else if(flag=="--output")output=value();
        else if(flag=="--size"){auto size=value();auto at=size.find('x');if(at==std::string::npos)throw std::invalid_argument("Size must be WIDTHxHEIGHT");options.width=number(size.substr(0,at),16384);options.height=number(size.substr(at+1),16384);}
        else if(flag=="--gpu-batch-samples")options.gpuBatchSamples=number(value(),64);
        else if(flag=="--samples")options.samples=number(value(),1048576);else if(flag=="--bounces")options.maxDepth=number(value(),128);
        else if(flag=="--seed")options.seed=number(value(),UINT32_MAX);else if(flag=="--threads")options.threads=number(value(),256);
        else if(flag=="--no-shadow-any-hit")options.shadowAnyHit=false;
        else if(flag=="--photons")options.photonMapping=true;
        else if(flag=="--photon-paths")options.photonPaths=number(value(),4000000);
        else if(flag=="--photon-radius"){auto text=value();size_t end=0;options.photonRadius=std::stof(text,&end);if(end!=text.size())throw std::invalid_argument("Invalid photon radius");}
        else if(flag=="--no-thin-sun-proposal")options.thinSunProposal=false;else if(flag=="--no-solar-proposal")options.waterSunProposal=false;
        else if(flag=="--denoise")filter=true;else if(flag=="--self-test")selfTest=true;else throw std::invalid_argument("Unknown option: "+flag);
    }
    if(backend!="CPU"&&backend!="Metal"&&backend!="Vulkan")throw std::invalid_argument("Backend must be CPU, Metal or Vulkan");
    pt::validateOptions(options);
    if(selfTest){if(backend=="CPU")throw std::invalid_argument("Use pt-cpu-tests for CPU validation");auto gpu=device(backend);pt::validateGpuPathTracing(gpu);gpu->close();std::cout<<backend<<" native PT validation passed\n";return 0;}
    if((input.empty()&&fixture.empty())||(!input.empty()&&!fixture.empty())||output.empty())throw std::invalid_argument("Usage: pt-package-render (--scene scene.json | --fixture pool-caustics|pool-flat|pool-no-water|pool-sunlit|pool-sunlit-underwater|pool-sunlit-flat) --output prefix [--backend CPU|Metal|Vulkan --size 160x90 --samples 512 --bounces 24 --seed 1 --threads 2 --photons --photon-paths 200000 --photon-radius .08 --gpu-batch-samples 8 --no-shadow-any-hit --denoise --no-thin-sun-proposal --no-solar-proposal]; native validation: --backend Metal|Vulkan --self-test");
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
    pt::CpuScene scene(package.snapshot);scene.environment=package.environment;scene.sunDirection=package.sunDirection;scene.sunIrradiance=package.sunIrradiance;scene.sunRadius=package.sunRadius;
    pt::Image image;if(backend=="CPU")image=pt::render(scene,options);else{auto gpu=device(backend);image=pt::renderGpu(scene,options,gpu);gpu->close();}
    if(filter)pt::denoise(image);pt::writeImage(image,options.exposure,output);pt::writeReport(image,scene,options,output,package.metadata.value("source",nlohmann::json::object()).value("name",std::string("package")));
    std::ifstream in(output+".json");nlohmann::json report;in>>report;in.close();report["scene_package"]=package.metadata;report["entry_point"]="pt-package-render";std::ofstream out(output+".json");out<<report.dump(2)<<'\n';if(!out)throw std::runtime_error("Cannot save package report");
    return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
