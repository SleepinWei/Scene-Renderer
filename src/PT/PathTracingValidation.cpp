#include "PT/CpuPathTracer.h"
#include "PT/GpuPathTracer.h"
#include "PT/ProceduralCapture.h"
#include "PT/ValidationScenes.h"
#include "engine/RenderRuntime.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
namespace pt {
void validatePathTracingBridge(std::shared_ptr<rhi::GraphicsDevice> device) {
    if(!device->computeLimits().supported || device->backend()==rhi::Backend::OpenGL)return;
    render::FrameData frame;frame.sky=true;frame.cameraPosition={0,2,0};frame.lights.push_back({{0,0,0,0},{3,2.5f,2,0},{0,-1,-.3f,0}});
    engine::RenderRuntime runtime(device,nullptr);
    auto baked=runtime.captureAtmosphere(frame);
    auto source=makeCausticsScene(32,24,false).snapshot;source.frame.timeSeconds=1;
    render::OceanSurfaceSettings water;water.spectrum.size=64;water.spectrum.length=8;water.spectrum.amplitude=.001f;water.meshSize=17;water.seaLevel=.4f;water.detailWaves=false;water.absorption={.2f,.1f,.05f};source.frame.oceans={water};
    CaptureOptions capture;capture.grass=false;capture.textureExtent=64;
    auto frozen=runtime.capturePathTracingScene(source,capture);source.frame.timeSeconds=2;auto later=runtime.capturePathTracingScene(source,capture);
    float motion=0;const auto &a=frozen.draws.back().mesh->vertices,&b=later.draws.back().mesh->vertices;for(size_t i=0;i<a.size();++i)motion=std::max(motion,glm::length(a[i].position-b[i].position));
    if(motion<1e-6f||frozen.draws.back().pathTracingIor!=1.333f||!frozen.frame.oceans.empty())throw std::runtime_error("PT: native FFT ocean capture did not freeze motion/optics");
    source.frame.oceans[0].spectrum.size=7;bool rejected=false;try{runtime.capturePathTracingScene(source,capture);}catch(const std::invalid_argument &){rejected=true;}if(!rejected)throw std::runtime_error("PT: capture error did not reach logic thread");
    runtime.finish();
    if(baked.radiance.size()!=size_t(baked.width)*baked.height || baked.sunRadius<=0 || glm::length(baked.sunIrradiance)<=0)throw std::runtime_error("PT: render-thread sky bake is empty");
    double energy=0;for(auto pixel:baked.radiance){if(!std::isfinite(pixel.x) || !std::isfinite(pixel.y) || !std::isfinite(pixel.z) || glm::any(glm::lessThan(pixel,glm::vec3(0))))throw std::runtime_error("PT: baked HDR sky has invalid radiance");energy+=pixel.x+pixel.y+pixel.z;}
    if(energy<=0)throw std::runtime_error("PT: baked sky has no energy");
    Environment environment(baked.width,baked.height,std::move(baked.radiance));Random random(7);
    for(int i=0;i<1000;++i){auto sample=environment.sample(random);if(sample.pdf<=0 || !std::isfinite(sample.pdf))throw std::runtime_error("PT: baked environment sampling PDF is invalid");}
    validateGpuPathTracing(device);
    frozen.draws.erase(frozen.draws.begin()+2); // Replace studio emitter with constant HDR for energy comparison.
    CpuScene waterScene(frozen);waterScene.environment=std::make_shared<Environment>(8,4,std::vector<glm::vec3>(32,glm::vec3(1)));
    Options options;options.width=32;options.height=24;options.samples=512;options.maxDepth=6;options.adaptive=false;auto cpu=render(waterScene,options),gpu=renderGpu(waterScene,options,device);double error=0,reference=0;for(size_t i=0;i<cpu.radiance.size();++i){error+=glm::length(cpu.radiance[i]-gpu.radiance[i]);reference+=glm::length(cpu.radiance[i]);}if(error/std::max(reference,1e-9)>.08)throw std::runtime_error("PT: CPU/GPU FFT water/absorption energy differs");
    frozen.frame.cameraPosition={0,.2f,0};glm::mat4 depth(1);depth[2][2]=.5f;depth[3][2]=.5f;frozen.frame.viewProjection=depth*glm::perspective(glm::radians(50.f),32.f/24,.1f,100.f)*glm::lookAt(frozen.frame.cameraPosition,glm::vec3(0,-1,-.01f),glm::vec3(0,0,-1));
    CpuScene submerged(frozen);submerged.environment=waterScene.environment;auto underCpu=render(submerged,options),underGpu=renderGpu(submerged,options,device);double underError=0,underEnergy=0;for(size_t i=0;i<underCpu.radiance.size();++i){underError+=glm::length(underCpu.radiance[i]-underGpu.radiance[i]);underEnergy+=glm::length(underCpu.radiance[i]);}if(underError/std::max(underEnergy,1e-9)>.08)throw std::runtime_error("PT: CPU/GPU underwater primary medium differs");
    std::cout<<"Underwater CPU/GPU relative L1 "<<underError/underEnergy<<'\n';
    std::cout<<"Native FFT PT capture motion "<<motion<<", CPU/GPU water relative L1 "<<error/reference<<'\n';
    std::cout<<"CPU PT render-thread HDR sky bake and importance sampling passed\n";
}
} // namespace pt
