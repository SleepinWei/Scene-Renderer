#include "PT/CpuPathTracer.h"
#include "PT/GpuPathTracer.h"
#include "PT/ValidationScenes.h"
#include "PT/Denoiser.h"
#include "PT/ProceduralCapture.h"
#include "PT/ScenePackage.h"
#include "PT/DdgiReference.h"
#include "PT/OceanReference.h"
#include "PT/WorldLod.h"
#include <chrono>
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
#include <map>
#include <algorithm>
namespace pt {
namespace {
// Deterministic primary-ray geometry diagnostic: actual PT visibility, including
// alpha tests, with projected triangle edges. No transport, textures or MC noise.
void writeLodWireframe(const CpuScene &scene,const std::vector<bool> &reduced,
                       uint32_t width,uint32_t height,const std::string &path) {
    const auto data=scene.exportData();const auto projection=glm::inverse(data.inverseProjection);
    struct Face {std::array<glm::vec4,3> clip;bool reduced=false;};
    std::vector<Face> faces(scene.triangles());
    for(size_t i=0;i<data.instances.size();++i){const auto &instance=data.instances[i];
        for(uint32_t j=0;j<instance.range.x;++j){auto &face=faces[instance.info.z+j];face.reduced=reduced.at(i);
            const auto indices=data.geometry->triangles[instance.info.w+j];
            for(int k=0;k<3;++k)face.clip[k]=projection*instance.model*glm::vec4(glm::vec3(data.geometry->vertices[indices[k]].positionU),1);
        }
    }
    std::vector<unsigned char> pixels(size_t(width)*height*3);
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x){glm::vec3 color(0);
        for(int sy=0;sy<2;++sy)for(int sx=0;sx<2;++sx){glm::vec2 pixel(x+(sx+.5f)/2,y+(sy+.5f)/2);glm::vec3 origin,direction;
            scene.cameraRay(pixel.x/width,pixel.y/height,origin,direction);Surface hit;glm::vec3 sample(.035f,.045f,.065f);
            if(scene.intersect(origin,direction,1e-4f,std::numeric_limits<float>::infinity(),hit)){
                const auto &face=faces.at(hit.primitive);float distance=INFINITY;
                for(int k=0;k<3;++k){auto a=face.clip[k],b=face.clip[(k+1)%3];
                    // Clip edges crossing the eye plane before perspective divide.
                    constexpr float nearW=1e-5f;if(a.w<=nearW&&b.w<=nearW)continue;
                    if(a.w<=nearW)a=glm::mix(a,b,(nearW-a.w)/(b.w-a.w));
                    if(b.w<=nearW)b=glm::mix(b,a,(nearW-b.w)/(a.w-b.w));
                    glm::vec2 A((a.x/a.w*.5f+.5f)*width,(.5f-a.y/a.w*.5f)*height);
                    glm::vec2 B((b.x/b.w*.5f+.5f)*width,(.5f-b.y/b.w*.5f)*height);
                    auto edge=B-A;float length2=glm::dot(edge,edge);if(length2<1e-12f)continue;
                    distance=std::min(distance,glm::length(pixel-(A+edge*glm::clamp(glm::dot(pixel-A,edge)/length2,0.f,1.f))));
                }
                float light=.4f+.6f*std::abs(glm::dot(hit.geometricNormal,glm::normalize(glm::vec3(-.3f,.8f,.5f))));
                auto base=face.reduced?glm::vec3(.08f,.29f,.34f):glm::vec3(.19f,.21f,.24f);
                auto line=face.reduced?glm::vec3(.25f,.88f,.95f):glm::vec3(.66f,.7f,.76f);
                sample=glm::mix(base*light,line,glm::clamp(1.15f-distance,0.f,1.f));
            }
            color+=sample*.25f;
        }
        for(int k=0;k<3;++k)pixels[(size_t(y)*width+x)*3+k]=static_cast<unsigned char>(glm::clamp(color[k],0.f,1.f)*255+.5f);
    }
    if(!stbi_write_png(path.c_str(),int(width),int(height),3,pixels.data(),int(width)*3))throw std::runtime_error("PT: cannot save LOD wireframe");
}
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
    bool ddgiPathCapture=false,lodVisualize=false;uint32_t ddgiPathSamples=4,ddgiPathDepth=8;
    std::map<std::string,float> seaOverrides;float sharedWaterRoughness=-1;bool legacyWaterSurface=false;std::string spectrumOverride;
    const auto commandStarted=std::chrono::steady_clock::now();
    const bool gpu=argc>1 && std::string(argv[1])=="--path-trace-gpu";
    if(gpu && rhi::requestedBackend()==rhi::Backend::OpenGL)throw std::invalid_argument("GPU PT requires Metal or Vulkan");
    Options options;DenoiseOptions denoiseOptions;CaptureOptions captureOptions;bool compareDdgi=false,compareOcean=false;uint32_t ddgiFrames=960;float time=8,sssScale=1,sssScatteringScale=1,waterScatteringScale=1,sunRadius=0,floorDepth=20,sssRoughness=.22f,waterRoughness=0;std::string denoiseInput,dragonPath="samples/assets/pt/dragon/dragon_vrip.ply";
    std::string name="sponza",prefix,environmentPath,packagePath;bool captureClouds=false,sky=true,glass=true,filter=false,depthExplicit=false,floorExplicit=false,sssRoughExplicit=false,waterRoughExplicit=false;int argument=2;
    if(argument<argc && std::string(argv[argument]).rfind("--",0)!=0)name=argv[argument++];
    for(int i=argument;i<argc;++i) {
        const std::string flag=argv[i];auto value=[&](){if(++i>=argc)throw std::invalid_argument("PT: missing argument for "+flag);return std::string(argv[i]);};
        if(flag=="--pt-size"){const auto size=value();const auto at=size.find('x');if(at==std::string::npos)throw std::invalid_argument("PT: size must be WIDTHxHEIGHT");options.width=number(size.substr(0,at),"width",16384);options.height=number(size.substr(at+1),"height",16384);}
        else if(flag=="--pt-samples")options.samples=number(value(),"samples",1048576);
        else if(flag=="--pt-gpu-batch-samples")options.gpuBatchSamples=number(value(),"GPU batch samples",64);
        else if(flag=="--pt-checkpoint-samples")options.checkpointSamples=number(value(),"checkpoint interval",1048576);
        else if(flag=="--pt-bounces"){options.maxDepth=number(value(),"bounces",128);depthExplicit=true;}
        else if(flag=="--pt-threads")options.threads=number(value(),"threads",256);
        else if(flag=="--pt-seed")options.seed=number(value(),"seed",UINT32_MAX);
        else if(flag=="--pt-exposure"){const auto s=value();size_t end;options.exposure=std::stof(s,&end);if(end!=s.size() || !std::isfinite(options.exposure) || options.exposure<=0)throw std::invalid_argument("PT: invalid exposure");}
        else if(flag=="--pt-sampler"){auto sampler=value();if(sampler!="sobol" && sampler!="pcg")throw std::invalid_argument("PT: sampler must be sobol or pcg");options.sobol=sampler=="sobol";}
        else if(flag=="--pt-guiding")options.guiding=true;
        else if(flag=="--pt-cache")options.radianceCache=true;
        else if(flag=="--pt-no-shadow-any-hit")options.shadowAnyHit=false;
        else if(flag=="--pt-traversal"){auto mode=value();if(mode=="auto")options.gpuTraversal=GpuTraversal::Auto;else if(mode=="software")options.gpuTraversal=GpuTraversal::Software;else if(mode=="native")options.gpuTraversal=GpuTraversal::Native;else throw std::invalid_argument("PT: traversal must be auto, software or native");}
        else if(flag=="--pt-photons"){options.photonMapping=true;options.adaptive=false;}
        else if(flag=="--pt-sppm"){options.sppm=true;options.adaptive=false;}
        else if(flag=="--pt-sppm-alpha")options.sppmAlpha=realNumber(value(),"SPPM alpha");
        else if(flag=="--pt-photon-paths")options.photonPaths=number(value(),"photon paths",4000000);
        else if(flag=="--pt-photon-radius")options.photonRadius=realNumber(value(),"photon radius");
        else if(flag=="--pt-bdpt"){options.bdpt=true;options.adaptive=false;}
        else if(flag=="--pt-no-glass")glass=false;
        else if(flag=="--pt-sss-roughness"){sssRoughness=realNumber(value(),"jade roughness",true);sssRoughExplicit=true;if(sssRoughness>1)throw std::invalid_argument("PT: jade roughness must be in [0,1]");}
        else if(flag=="--pt-water-surface-roughness"){sharedWaterRoughness=realNumber(value(),"shared water roughness",true);if(sharedWaterRoughness>1)throw std::invalid_argument("PT: shared water roughness must be in [0,1]");}
        else if(flag=="--pt-water-surface-legacy")legacyWaterSurface=true;
        else if(flag=="--pt-water-roughness"){waterRoughness=realNumber(value(),"water roughness",true);waterRoughExplicit=true;if(waterRoughness>1)throw std::invalid_argument("PT: water roughness must be in [0,1]");}
        else if(flag=="--pt-sss-scale")sssScale=realNumber(value(),"subsurface coefficient scale",true);
        else if(flag=="--pt-sss-scattering-scale")sssScatteringScale=realNumber(value(),"subsurface scattering scale",true);
        else if(flag=="--pt-water-scattering-scale")waterScatteringScale=realNumber(value(),"water scattering scale",true);
        else if(flag=="--pt-sun-radius"){sunRadius=realNumber(value(),"sun angular radius in degrees");if(sunRadius>=glm::degrees(.1f))throw std::invalid_argument("PT: sun angular radius must be below 5.73 degrees");}
        else if(flag=="--pt-ocean-spectrum"){spectrumOverride=value();if(spectrumOverride!="phillips"&&spectrumOverride!="jonswap")throw std::invalid_argument("PT: ocean spectrum must be phillips or jonswap");}
        else if(flag=="--pt-ocean-wind-height"||flag=="--pt-ocean-wind-period"||flag=="--pt-ocean-wind-gamma"||flag=="--pt-ocean-wind-spread"||
                flag=="--pt-ocean-swell-height"||flag=="--pt-ocean-swell-period"||flag=="--pt-ocean-swell-spread"||flag=="--pt-ocean-depth")seaOverrides[flag]=realNumber(value(),"sea state",true);
        else if(flag=="--pt-ocean-swell-direction"){auto s=value();size_t end=0;float angle=std::stof(s,&end);if(end!=s.size()||!std::isfinite(angle))throw std::invalid_argument("PT: invalid swell direction");seaOverrides[flag]=angle;}
        else if(flag=="--pt-ocean-floor-depth"){floorDepth=realNumber(value(),"ocean floor depth");floorExplicit=true;}
        else if(flag=="--pt-clouds")captureClouds=true;
        else if(flag=="--pt-time")time=realNumber(value(),"frozen time",true);
        else if(flag=="--pt-terrain-grid")captureOptions.terrainGrid=number(value(),"terrain grid",1025);
        else if(flag=="--pt-ocean-grid")captureOptions.oceanGrid=number(value(),"ocean grid",1025);
        else if(flag=="--pt-texture-size")captureOptions.textureExtent=number(value(),"capture texture size",4096);
        else if(flag=="--pt-grass-limit")captureOptions.grassLimit=number(value(),"grass limit",1048576);
        else if(flag=="--pt-no-water-sun-proposal")options.waterSunProposal=false;
        else if(flag=="--pt-no-thin-sun-proposal")options.thinSunProposal=false;
        else if(flag=="--pt-no-grass")captureOptions.grass=false;
        else if(flag=="--pt-dragon-mesh")dragonPath=value();
        else if(flag=="--pt-scene-file")packagePath=value();
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
        else if(flag=="--pt-compare-ocean")compareOcean=true;
        else if(flag=="--pt-compare-ddgi")compareDdgi=true;
        else if(flag=="--pt-ddgi-capture"){auto backend=value();if(backend!="pt"&&backend!="raster")throw std::invalid_argument("DDGI capture expects pt or raster");ddgiPathCapture=backend=="pt";}
        else if(flag=="--pt-ddgi-path-samples")ddgiPathSamples=number(value(),"probe path samples",4096);
        else if(flag=="--pt-ddgi-path-depth")ddgiPathDepth=number(value(),"probe path depth",32);
        else if(flag=="--pt-world-lod")options.worldLod.enabled=true;
        else if(flag=="--pt-lod-visualize")lodVisualize=true;
        else if(flag=="--pt-lod-error")options.worldLod.maxError=realNumber(value(),"LOD world error");
        else if(flag=="--pt-lod-ratio")options.worldLod.targetRatio=realNumber(value(),"LOD target ratio");
        else if(flag=="--pt-lod-uv-error")options.worldLod.maxUvError=realNumber(value(),"LOD UV error",true);
        else if(flag=="--pt-ddgi-frames")ddgiFrames=number(value(),"DDGI frames",100000);
        else if(flag=="--backend")value();
        else throw std::invalid_argument("PT: unknown argument "+flag);
    }
    if(!depthExplicit&&(name=="dragon-jade"||name=="dragon-jade-ocean"))options.maxDepth=96;
    if(lodVisualize&&!options.worldLod.enabled)throw std::invalid_argument("PT: LOD visualization requires --pt-world-lod");
    if(floorExplicit&&name!="dragon-jade-ocean")throw std::invalid_argument("PT: ocean floor depth requires dragon-jade-ocean");
    if(sssRoughExplicit&&name!="dragon-jade"&&name!="dragon-jade-ocean")throw std::invalid_argument("PT: jade roughness requires a jade dragon scene");
    if(sunRadius>0&&(!sky||!environmentPath.empty()||!denoiseInput.empty()))throw std::invalid_argument("PT: sun radius requires a rendered scene with baked sky");
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
    if(gpu && options.sppm)throw std::invalid_argument("SPPM is CPU-only: use --path-trace --pt-sppm");
    if(compareDdgi&&(!gpu||!sky||options.adaptive||!environmentPath.empty()||!packagePath.empty()||captureClouds||options.guiding||options.radianceCache||options.photonMapping||options.sppm||(name!="sponza"&&name!="cornell")))
        throw std::invalid_argument("DDGI comparison requires fixed native Sponza/Cornell GPU PT with baked sky, without custom environment/package, clouds or biased estimators");
    if(compareOcean&&(!gpu||!sky||options.adaptive||!environmentPath.empty()||!packagePath.empty()||!denoiseInput.empty()||captureClouds||compareDdgi||waterRoughExplicit||options.guiding||options.radianceCache||options.photonMapping||options.sppm))
        throw std::invalid_argument("Ocean comparison requires fixed GPU PT with native baked sky and unmodified water BSDF; custom environment/package, clouds, biased estimators and DDGI comparison are excluded");
    if(prefix.empty())prefix="build/path-tracing/"+name;
    const auto parent=std::filesystem::path(prefix).parent_path();if(!parent.empty())std::filesystem::create_directories(parent);
    std::shared_ptr<RenderScene> scene;std::vector<DielectricMaterial> dielectrics;
    std::shared_ptr<const render::RenderWorldSnapshot> snapshot;
    ScenePackage package;
    if(packagePath.empty()&&(name=="blender-classroom"||name=="blender-barcelona"))packagePath="samples/assets/blender/"+name.substr(8)+"/scene.json";
    if(!packagePath.empty()){auto start=std::chrono::steady_clock::now();package=loadScenePackage(packagePath,options.width,options.height);package.metadata["import_seconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();snapshot=std::make_shared<render::RenderWorldSnapshot>(std::move(package.snapshot));std::cout<<"PT scene package: "<<package.metadata.dump()<<'\n';}
    else if(name=="dragon-jade"||name=="dragon-jade-ocean"){auto validation=makeJadeDragonScene(options.width,options.height,dragonPath,name=="dragon-jade-ocean",floorDepth);validation.snapshot.frame.timeSeconds=time;for(auto &draw:validation.snapshot.draws)if(draw.pathTracingKind==4){draw.pathTracingRoughness=sssRoughness;draw.pathTracingAbsorption*=sssScale;draw.pathTracingScattering*=sssScale*sssScatteringScale;}for(auto &ocean:validation.snapshot.frame.oceans)ocean.scattering*=waterScatteringScale;snapshot=std::make_shared<render::RenderWorldSnapshot>(std::move(validation.snapshot));}
    else if(name=="pool-caustics"||name=="pool-flat"||name=="pool-no-water"||name=="pool-sunlit"||name=="pool-sunlit-underwater"||name=="pool-sunlit-flat"){auto validation=makePoolCausticsScene(options.width,options.height,name!="pool-flat"&&name!="pool-sunlit-flat",name!="pool-no-water",name.find("pool-sunlit")==0,name=="pool-sunlit-underwater");snapshot=std::make_shared<render::RenderWorldSnapshot>(std::move(validation.snapshot));}
    else if(name=="caustics"||name=="dragon-caustics"){auto validation=name=="caustics"?makeCausticsScene(options.width,options.height,glass):makeDragonScene(options.width,options.height,dragonPath,glass);snapshot=std::make_shared<render::RenderWorldSnapshot>(std::move(validation.snapshot));dielectrics=std::move(validation.dielectrics);}
    else {scene=render::makeClassicScene(name);scene->mainCamera()->setAspect(float(options.width)/options.height);render::SceneSnapshotBuilder builder;snapshot=builder.capture(scene,time,options.width,options.height);}
    if(!spectrumOverride.empty()||!seaOverrides.empty()){
        if(snapshot->frame.oceans.empty())throw std::invalid_argument("PT: sea-state options require an ocean scene");
        if(spectrumOverride=="phillips"&&!seaOverrides.empty())throw std::invalid_argument("PT: sea-state options require JONSWAP");
        auto adjusted=std::make_shared<render::RenderWorldSnapshot>(*snapshot);
        for(auto& o:adjusted->frame.oceans){
            auto& a=o.spectrum.seaState;a.enabled=spectrumOverride!="phillips";
            auto set=[&](const char* key,float& field){auto it=seaOverrides.find(key);if(it!=seaOverrides.end())field=it->second;};
            set("--pt-ocean-wind-height",a.windHeight);set("--pt-ocean-wind-period",a.windPeriod);set("--pt-ocean-wind-gamma",a.windGamma);set("--pt-ocean-wind-spread",a.windSpread);
            set("--pt-ocean-swell-height",a.swellHeight);set("--pt-ocean-swell-period",a.swellPeriod);set("--pt-ocean-swell-spread",a.swellSpread);set("--pt-ocean-depth",a.depth);
            if(seaOverrides.count("--pt-ocean-swell-direction")){float angle=glm::radians(seaOverrides.at("--pt-ocean-swell-direction"));a.swellDirection={std::cos(angle),std::sin(angle)};}
            a.validate();o.spectrum.minWavelength=a.enabled?std::max(2.f,2*o.spectrum.length/o.spectrum.size):0;o.spectrum.maxWavelength=a.enabled?o.spectrum.length:0;
        }
        snapshot=adjusted;
    }
    if(captureClouds){if(!snapshot->frame.sky||!sky||!environmentPath.empty()||!packagePath.empty())throw std::invalid_argument("PT clouds require a native scene sky without a custom HDR/package");auto adjusted=std::make_shared<render::RenderWorldSnapshot>(*snapshot);adjusted->frame.clouds.enabled=true;adjusted->frame.clouds.validate();snapshot=adjusted;}
    if(waterScatteringScale!=1&&name!="dragon-jade-ocean"){auto scaled=std::make_shared<render::RenderWorldSnapshot>(*snapshot);for(auto &ocean:scaled->frame.oceans)ocean.scattering*=waterScatteringScale;snapshot=scaled;}
    if(sunRadius>0){if(!snapshot->frame.sky)throw std::invalid_argument("PT: sun radius requires a scene with sky");auto adjusted=std::make_shared<render::RenderWorldSnapshot>(*snapshot);adjusted->frame.atmosphere.radii.y=glm::radians(sunRadius);snapshot=adjusted;}
    if(sharedWaterRoughness>=0||legacyWaterSurface){
        if(snapshot->frame.oceans.empty()||waterRoughExplicit||(sharedWaterRoughness>=0&&legacyWaterSurface))throw std::invalid_argument("PT: shared water model requires an unfrozen ocean and unambiguous roughness/model selection");
        auto adjusted=std::make_shared<render::RenderWorldSnapshot>(*snapshot);
        for(auto& o:adjusted->frame.oceans){o.surfaceModel.physical=!legacyWaterSurface;if(sharedWaterRoughness>=0)o.surfaceModel.roughness=sharedWaterRoughness;}
        snapshot=adjusted;
    }
    std::shared_ptr<const render::RenderWorldSnapshot> oceanReference;
    if(compareOcean){
        if(snapshot->frame.oceans.empty())throw std::invalid_argument("Ocean comparison requires an ocean scene");
        auto adjusted=std::make_shared<render::RenderWorldSnapshot>(*snapshot);
        adjusted->frame.weather.enabled=false;
        for(auto& o:adjusted->frame.oceans)if(captureOptions.oceanGrid)o.meshSize=captureOptions.oceanGrid;
        snapshot=adjusted;oceanReference=adjusted;
    }
    render::BakedAtmosphere baked;
    DeviceContext context;
    const bool proceduralDevice=!snapshot->frame.oceans.empty()||(snapshot->terrain&&captureOptions.grass&&snapshot->terrain->source->grass);
    if(gpu || proceduralDevice || (sky && environmentPath.empty() && snapshot->frame.sky))context.open();
    if(sky && environmentPath.empty() && snapshot->frame.sky)baked=render::bakeAtmosphere(rhi::graphicsDevice(),snapshot->frame);
    nlohmann::json oceanCapture=nlohmann::json::array();
    for(const auto& o:snapshot->frame.oceans){const auto& s=o.spectrum;const auto& a=s.seaState;
        oceanCapture.push_back({{"spectrum",a.enabled?"jonswap-tma-wind-pm-swell":"phillips"},{"seed",s.seed},{"time_seconds",o.animate?snapshot->frame.timeSeconds*o.timeScale:0},
            {"domain_metres",s.length},{"surface_metres",o.surfaceLength>0?o.surfaceLength:s.length},{"fft_size",s.size},{"mesh_grid",captureOptions.oceanGrid?captureOptions.oceanGrid:o.meshSize},{"grid_sampling",a.enabled&&o.cameraGrid?"camera-focused-sinh":"uniform"},{"grid_focus_metres",o.gridFocus},
            {"wind_height_metres",a.windHeight},{"wind_period_seconds",a.windPeriod},{"wind_gamma",a.windGamma},{"wind_spread",a.windSpread},{"wind_direction_xz",{s.windDirection.x,s.windDirection.y}},
            {"swell_height_metres",a.swellHeight},{"swell_period_seconds",a.swellPeriod},{"swell_spread",a.swellSpread},{"swell_direction_xz",{a.swellDirection.x,a.swellDirection.y}},
            {"uniform_depth_metres",a.depth},{"height_scale",s.heightScale},{"choppiness",s.choppiness},{"main_wavelength_band_metres",{s.minWavelength,s.maxWavelength}},
            {"physical_surface",o.surfaceModel.physical},{"surface_ior",WaterSurfaceModel::ior},{"surface_roughness",o.surfaceModel.physical?o.surfaceModel.effectiveRoughness():0.f},{"detail_waves",o.detailWaves},{"detail_strength",o.detailStrength},{"wind_speed_metres_per_second",s.windSpeed},{"ripple_rms_metres",o.rippleRmsHeight*o.detailStrength},{"absorption",{o.absorption.x,o.absorption.y,o.absorption.z}},{"scattering",{o.scattering.x,o.scattering.y,o.scattering.z}},{"anisotropy",o.anisotropy}});
    }
    const size_t drawsBeforeCapture=snapshot->draws.size();
    const auto captureStarted=std::chrono::steady_clock::now();
    snapshot=std::make_shared<render::RenderWorldSnapshot>(captureProcedural(*snapshot,proceduralDevice?rhi::graphicsDevice():nullptr,captureOptions));
    size_t oceanIndex=0;
    for(size_t drawIndex=drawsBeforeCapture;drawIndex<snapshot->draws.size();++drawIndex){const auto& draw=snapshot->draws[drawIndex];if(draw.pathTracingKind!=3||oceanIndex>=oceanCapture.size())continue;
        const auto& vertices=draw.mesh->vertices;double sum=0,squared=0,low=INFINITY,high=-INFINITY;
        for(const auto& v:vertices){double y=v.position.y;sum+=y;squared+=y*y;low=std::min(low,y);high=std::max(high,y);}
        if(!vertices.empty()){double mean=sum/vertices.size();oceanCapture[oceanIndex]["captured_vertex_height_rms_metres"]=std::sqrt(std::max(0.,squared/vertices.size()-mean*mean));oceanCapture[oceanIndex]["captured_vertex_height_range_metres"]={low,high};}
        oceanCapture[oceanIndex]["captured_triangles"]=draw.mesh->indices.size()/3;
        if(draw.material&&draw.material->oceanNormals){const auto &fields=*draw.material->oceanNormals;
            oceanCapture[oceanIndex]["normal_encoding"]="native-rgba32f-separate-periods";
            oceanCapture[oceanIndex]["normal_grid"]={fields.images[0]->width,fields.images[1]?fields.images[1]->width:0};
            oceanCapture[oceanIndex]["normal_uv_scales"]={fields.scales.x,fields.scales.y};
            oceanCapture[oceanIndex]["normal_sampling"]="periodic bilinear, FFT grid-point phase, normalized fields then slope sum";
        }
        ++oceanIndex;
    }
    const double proceduralCaptureSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-captureStarted).count();
    if(waterRoughExplicit){for(auto& o:oceanCapture){o["surface_roughness"]=waterRoughness<.02f?0.f:waterRoughness;o["pt_only_roughness_override"]=waterRoughness;}auto adjusted=std::make_shared<render::RenderWorldSnapshot>(*snapshot);bool found=false;for(auto &draw:adjusted->draws)if(draw.pathTracingKind==3){draw.pathTracingRoughness=waterRoughness;found=true;}if(!found)throw std::invalid_argument("PT: water roughness requires a captured ocean");snapshot=adjusted;}
    if(!gpu)context.close();
    WorldLodReport lodReport;std::shared_ptr<render::RenderWorldSnapshot> lowWorld;
    if(options.worldLod.enabled){auto protectedWorld=*snapshot;for(auto &draw:protectedWorld.draws)for(auto optical:dielectrics)if(draw.objectId==optical.objectId)draw.pathTracingIor=optical.ior;lowWorld=std::make_shared<render::RenderWorldSnapshot>(buildLowLodWorld(protectedWorld,options.worldLod,&lodReport));writeWorldLodReport(lodReport,options.worldLod,prefix+"-world-lod.json");std::cout<<"PT world LOD: "<<lodReport.sourceUniqueTriangles<<" -> "<<lodReport.selectedUniqueTriangles<<" unique triangles, "<<lodReport.seconds<<" seconds\n";}
    CpuScene cpu(lowWorld?*lowWorld:*snapshot,dielectrics);
    if(lodVisualize){
        CpuScene full(*snapshot,dielectrics);std::vector<bool> reduced;
        for(size_t i=0;i<snapshot->draws.size();++i)if(const auto mesh=snapshot->draws[i].mesh){
            bool valid=false;for(size_t j=0;j<mesh->indices.size();j+=3){auto a=mesh->vertices[mesh->indices[j]].position,b=mesh->vertices[mesh->indices[j+1]].position,c=mesh->vertices[mesh->indices[j+2]].position;
                auto normal=glm::cross(b-a,c-a);if(glm::dot(normal,normal)>1e-24f){valid=true;break;}}
            if(valid)reduced.push_back(lowWorld->draws[i].mesh->indices.size()<mesh->indices.size());
        }
        writeLodWireframe(full,reduced,options.width,options.height,prefix+"-full-wireframe.png");
        writeLodWireframe(cpu,reduced,options.width,options.height,prefix+"-lod-wireframe.png");
        if(scene){scene->destroy();scene.reset();}
        std::cout<<"LOD primary-ray wireframes: "<<prefix<<"-{full,lod}-wireframe.png\n";return 0;
    }
    if(name=="pool-caustics"||name=="pool-flat"||name=="pool-no-water"||name=="pool-sunlit"||name=="pool-sunlit-underwater"||name=="pool-sunlit-flat"){bool sunlit=name.find("pool-sunlit")==0;cpu.sunDirection=glm::normalize(sunlit?glm::vec3(-.3f,1,.2f):glm::vec3(-.25f,1,.3f));cpu.sunIrradiance=sunlit?glm::vec3(4.5f,4.35f,4.1f):glm::vec3(4);cpu.sunRadius=sunlit?.00465f:.01f;if(sunlit&&sky)cpu.environment=std::make_shared<Environment>(16,8,std::vector<glm::vec3>(128,glm::vec3(.025f,.04f,.06f)));}
    if(!packagePath.empty()){cpu.sunDirection=package.sunDirection;cpu.sunIrradiance=package.sunIrradiance;cpu.sunRadius=package.sunRadius;if(sky)cpu.environment=std::move(package.environment);}
    if(options.bdpt&&cpu.scatteringCount())cpu.validateBidirectional();
    // Apply the same exposure by default, while retaining an explicit CLI override.
    bool explicitExposure=false;for(int i=2;i<argc;++i)explicitExposure|=std::string(argv[i])=="--pt-exposure";
    if(!explicitExposure)options.exposure=snapshot->exposure;
    if(!environmentPath.empty()) {
        int w,h,channels;stbi_set_flip_vertically_on_load_thread(false);auto pixels=stbi_loadf(environmentPath.c_str(),&w,&h,&channels,3);
        if(!pixels)throw std::runtime_error("PT: cannot load HDR environment "+environmentPath);
        std::vector<glm::vec3> radiance;try{radiance.resize(size_t(w)*h);for(size_t i=0;i<radiance.size();++i)radiance[i]={pixels[i*3],pixels[i*3+1],pixels[i*3+2]};}catch(...){stbi_image_free(pixels);throw;}stbi_image_free(pixels);
        cpu.environment=std::make_shared<Environment>(uint32_t(w),uint32_t(h),std::move(radiance));
        std::ifstream sidecar(environmentPath+".json");
        if(sidecar){nlohmann::json sun;sidecar>>sun;if(sun.contains("cloud_shadow"))cpu.cloudShadow=render::deserializeCloudShadow(sun.at("cloud_shadow").dump());auto direction=sun.at("sun_direction").get<std::array<float,3>>();auto irradiance=sun.at("sun_irradiance").get<std::array<float,3>>();cpu.sunDirection={direction[0],direction[1],direction[2]};cpu.sunIrradiance={irradiance[0],irradiance[1],irradiance[2]};cpu.sunRadius=sun.at("sun_radius").get<float>();if(!std::isfinite(cpu.sunRadius) || cpu.sunRadius<=0 || cpu.sunRadius>.1f || !std::isfinite(glm::length(cpu.sunDirection)) || glm::length(cpu.sunDirection)<.99f || glm::length(cpu.sunDirection)>1.01f || glm::any(glm::lessThan(cpu.sunIrradiance,glm::vec3(0))) || !std::isfinite(glm::length(cpu.sunIrradiance)))throw std::invalid_argument("PT: invalid environment sun sidecar");}

    }else if(!baked.radiance.empty()) {
        cpu.environment=std::make_shared<Environment>(baked.width,baked.height,std::move(baked.radiance));cpu.sunDirection=baked.sunDirection;cpu.sunIrradiance=baked.sunIrradiance;cpu.sunRadius=baked.sunRadius;cpu.cloudShadow=baked.cloudShadow;
    }
    if(cpu.environment) {
        std::vector<float> pixels;pixels.reserve(cpu.environment->pixels().size()*3);for(auto value:cpu.environment->pixels()){pixels.push_back(value.r);pixels.push_back(value.g);pixels.push_back(value.b);}
        const auto skyPath=prefix+"-environment.hdr";
        if(!stbi_write_hdr(skyPath.c_str(),int(cpu.environment->width()),int(cpu.environment->height()),3,pixels.data()))throw std::runtime_error("PT: cannot save baked sky HDR");
        if(cpu.sunRadius>0){std::ofstream sidecar(skyPath+".json");nlohmann::json metadata={{"sun_direction",{cpu.sunDirection.x,cpu.sunDirection.y,cpu.sunDirection.z}},{"sun_irradiance",{cpu.sunIrradiance.x,cpu.sunIrradiance.y,cpu.sunIrradiance.z}},{"sun_radius",cpu.sunRadius}};if(cpu.cloudShadow)metadata["cloud_shadow"]=nlohmann::json::parse(render::serializeCloudShadow(*cpu.cloudShadow));sidecar<<metadata.dump(2)<<'\n';if(!sidecar)throw std::runtime_error("PT: cannot save solar environment metadata");}
    }
    if(compareOcean)captureOceanReference(*oceanReference,cpu,options,rhi::graphicsDevice(),prefix);
    if(compareDdgi){
        std::unique_ptr<CpuScene> full;
        if(lowWorld){full=std::make_unique<CpuScene>(*snapshot,dielectrics);full->environment=cpu.environment;full->sunDirection=cpu.sunDirection;full->sunIrradiance=cpu.sunIrradiance;full->sunRadius=cpu.sunRadius;full->cloudShadow=cpu.cloudShadow;}
        captureDdgiReference(*snapshot,full?*full:cpu,options,rhi::graphicsDevice(),prefix,name,ddgiFrames,ddgiPathCapture,ddgiPathSamples,ddgiPathDepth,full?&cpu:nullptr);
    }
    snapshot.reset();if(scene){scene->destroy();scene.reset();}
    const std::string reportName=packagePath.empty()?name:package.metadata.value("source",nlohmann::json::object()).value("name",name);
    auto report=[&](const Image &image,const std::string &path){writeReport(image,cpu,options,path,reportName);if(!packagePath.empty()||!oceanCapture.empty()){std::ifstream input(path+".json");nlohmann::json data;input>>data;input.close();if(!packagePath.empty())data["scene_package"]=package.metadata;if(!oceanCapture.empty()){data["frozen_oceans"]=oceanCapture;data["timings"]["procedural_capture_seconds"]=proceduralCaptureSeconds;}std::ofstream out(path+".json");out<<data.dump(2)<<'\n';if(!out)throw std::runtime_error("PT: cannot save scene package report");}};
    auto save=[&](const Image &image){const auto progressPrefix=prefix+"-"+std::to_string(image.samples)+"spp";writeImage(image,options.exposure,progressPrefix);report(image,progressPrefix);};
    auto result=gpu?renderGpu(cpu,options,rhi::graphicsDevice(),save):render(cpu,options,save);
    if(filter)denoise(result,denoiseOptions);
    const auto outputStarted=std::chrono::steady_clock::now();writeImage(result,options.exposure,prefix);const auto outputSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-outputStarted).count();report(result,prefix);
    {std::ifstream input(prefix+".json");nlohmann::json data;input>>data;input.close();data["timings"]["final_image_write_seconds"]=outputSeconds;data["timings"]["command_before_final_report_seconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-commandStarted).count();std::ofstream out(prefix+".json");out<<data.dump(2)<<'\n';if(!out)throw std::runtime_error("PT: cannot save final timing report");}
    std::cout<<result.execution<<" PT output: "<<prefix<<".png / .pfm / .json\n";return 0;
}
} // namespace pt
