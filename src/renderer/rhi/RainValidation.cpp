#include "renderer/rhi/GpuRain.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "renderer/rhi/MaterialWetness.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <json/json.hpp>
namespace render {
namespace {
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
void ppm(const std::filesystem::path& path,const std::vector<uint8_t>& image,uint32_t w,uint32_t h){
    std::ofstream file(path,std::ios::binary);file<<"P6\n"<<w<<' '<<h<<"\n255\n";
    for(size_t i=0;i<image.size();i+=4)file.write(reinterpret_cast<const char*>(image.data()+i),3);
    if(!file)throw std::runtime_error("Cannot save rain validation capture");
}
}
void validateRain(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory){
    FrameData frame;frame.viewportWidth=320;frame.viewportHeight=240;frame.cameraPosition={0,3,8};frame.timeSeconds=20;
    frame.view=glm::lookAt(frame.cameraPosition,glm::vec3(0,0,0),glm::vec3(0,1,0));glm::mat4 remap(1);remap[2][2]=.5;remap[3][2]=.5;
    frame.viewProjection=remap*glm::perspective(glm::radians(55.f),320.f/240,.1f,50.f)*frame.view;
    frame.ambient=.2f;frame.lights={{{0,0,0,0},{2,2,2,0},{0,-1,0,0}}};
    std::vector<MeshVertex> vertices={{{-30,0,-30},{0,1,0},{0,0}},{{30,0,-30},{0,1,0},{1,0}},{{30,0,30},{0,1,0},{1,1}},{{-30,0,30},{0,1,0},{0,1}}};
    auto mesh=std::make_shared<GpuMesh>(device,vertices,std::vector<uint32_t>{0,2,1,0,3,2});
    MaterialDesc material;material.parameters.albedoAlpha={.5f,.5f,.5f,1};material.parameters.factors={0,.85f,1,0};
    auto gpuMaterial=std::make_shared<GpuMaterial>(device,material);std::vector<DrawPacket> scene{{mesh,gpuMaterial}};
    ForwardPbrRenderer renderer(device,directory,320,240,PbrPath::Scene);renderer.render(frame,scene);auto dry=renderer.readOutput();auto dryHDR=renderer.readHDR();
    material.extension.features={int(MaterialFeature::RainWetness),65535,0,0};gpuMaterial->updateExtension(material.extension);
    auto invalid=material.extension;invalid.features.y=65536;bool rejected=false;
    try{gpuMaterial->updateExtension(invalid);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected&&gpuMaterial->extension().features.y==65535,"Invalid wetness partially changed material");
    renderer.render(frame,scene);auto wet=renderer.readOutput();auto wetHDR=renderer.readHDR();
    check(wet!=dry,"Wet terrain material did not change");
    size_t darker=0;for(size_t i=0;i<dryHDR.size();i+=4)if(wetHDR[i]<dryHDR[i]*.7f&&dryHDR[i]>.01f)++darker;
    check(darker>1000,"Rain wetness did not reduce diffuse terrain albedo");
    auto spatial=material.extension;spatial.features.x|=int(MaterialFeature::SpatialRainWetness);
    for(size_t i=0;i<256;++i)setRainWetCell(spatial,i,i%16>=8?255:0);
    gpuMaterial->updateExtension(spatial);renderer.render(frame,scene);auto spatialImage=renderer.readOutput();auto spatialHDR=renderer.readHDR();
    size_t spatialDry=0,spatialWet=0;for(size_t i=0;i<spatialHDR.size();i+=4)if(dryHDR[i]>.01f){
        if(std::abs(spatialHDR[i]-dryHDR[i])<.001f)++spatialDry;
        if(std::abs(spatialHDR[i]-wetHDR[i])<.001f)++spatialWet;
    }
    check(spatialDry>1000&&spatialWet>1000,"Spatial rain field did not preserve dry/wet regions");
    renderer.render(frame,scene);check(renderer.readOutput()==spatialImage,"Paused spatial wetness is nondeterministic");
    // Both regions must reach the independent underwater geometry capture.
    auto waterFrame=frame;OceanSurfaceSettings ocean;ocean.id=1;ocean.spectrum.size=16;ocean.spectrum.length=16;ocean.spectrum.amplitude=0;
    ocean.meshSize=17;ocean.surfaceLength=64;ocean.seaLevel=1;ocean.detailWaves=false;ocean.cameraGrid=false;
    ocean.absorption=ocean.scattering=glm::vec3(0);ocean.specular=glm::vec3(0);ocean.fresnel=0;waterFrame.oceans={ocean};
    gpuMaterial->updateExtension({});renderer.render(waterFrame,scene);auto dryCapture=renderer.readWaterCapture(1);
    gpuMaterial->updateExtension(material.extension);renderer.render(waterFrame,scene);auto wetCapture=renderer.readWaterCapture(1);
    gpuMaterial->updateExtension(spatial);renderer.render(waterFrame,scene);auto spatialCapture=renderer.readWaterCapture(1);
    size_t captureDry=0,captureWet=0;for(size_t i=0;i<spatialCapture.size();i+=4)if(dryCapture[i]>.01f){
        if(std::abs(spatialCapture[i]-dryCapture[i])<.001f)++captureDry;
        if(std::abs(spatialCapture[i]-wetCapture[i])<.001f)++captureWet;
    }
    check(captureDry>1000&&captureWet>1000,"Water capture lost spatial rain wetness");
    gpuMaterial->updateExtension(material.extension);
    frame.rainMetersPerSecond=50.f/3600000;frame.rainWind={12,4};renderer.render(frame,scene);auto rain=renderer.readOutput();auto rainHDR=renderer.readHDR();
    check(rain!=wet,"Rain pass had no visible streaks");renderer.render(frame,scene);check(renderer.readOutput()==rain,"Paused visual rain is nondeterministic");
    auto hidden=frame;hidden.rainVisible=false;renderer.render(hidden,scene);check(renderer.readOutput()==wet,"Disabling visual rain changed terrain");
    for(float value:rainHDR)check(std::isfinite(value),"Rain produced nonfinite HDR");
    // The fragment shader must reject every drop behind a full-screen near depth.
    Resources resources(device);const auto usage=rhi::TextureUsage::ColorAttachment|rhi::TextureUsage::Sampled|rhi::TextureUsage::CopySource;
    auto hdr=resources.texture({320,240,rhi::Format::RGBA16Float,usage,"Rain occlusion test"});auto hv=resources.view(hdr);
    auto depth=resources.texture({320,240,rhi::Format::Depth32Float,rhi::TextureUsage::DepthAttachment|rhi::TextureUsage::Sampled,"Rain blocking depth"});auto dv=resources.view(depth);
    GpuRain particles(device,directory);auto commands=device->createCommandList();rhi::RenderPassDesc clear;clear.color=hv;clear.depth=dv;clear.clearColor={.1f,.2f,.3f,1};clear.clearDepth=0;commands.beginRenderPass(clear);commands.endRenderPass();
    particles.record(resources,commands,frame,hv,dv);device->submit(commands);auto occluded=device->readTextureFloat(hdr);
    for(size_t i=0;i<occluded.size();i+=4)check(std::abs(occluded[i]-.1f)<.001f&&std::abs(occluded[i+1]-.2f)<.001f,"Rain leaked through foreground depth");
    nlohmann::json report={{"width",320},{"height",240},{"wet_diffuse_pixels",darker},{"paused_equal",true},{"visual_disable_equal",true},{"full_depth_occlusion",true},{"finite_hdr",true},{"spatial_dry_pixels",spatialDry},{"spatial_wet_pixels",spatialWet},{"capture_dry_pixels",captureDry},{"capture_wet_pixels",captureWet},{"scope","main-view analytic visual rain; uniform and 16x16 spatial frozen rain wetness"}};
    if(const char* output=std::getenv("SCENERENDERER_RAIN_CAPTURE_DIR")){
        std::filesystem::path root(output);std::filesystem::create_directories(root);ppm(root/"dry.ppm",dry,320,240);ppm(root/"wet.ppm",wet,320,240);ppm(root/"rain.ppm",rain,320,240);ppm(root/"spatial.ppm",spatialImage,320,240);
        std::ofstream(root/"validation.json")<<report.dump(2)<<'\n';
    }
    std::cout<<"Rain depth occlusion, deterministic pause, wet material and visual disable passed: "<<report.dump()<<'\n';
}
}
