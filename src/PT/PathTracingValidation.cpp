#include "PT/CpuPathTracer.h"
#include "PT/GpuPathTracer.h"
#include "engine/RenderRuntime.h"
#include <cmath>
#include <iostream>
namespace pt {
void validatePathTracingBridge(std::shared_ptr<rhi::GraphicsDevice> device) {
    if(!device->computeLimits().supported || device->backend()==rhi::Backend::OpenGL)return;
    render::FrameData frame;frame.sky=true;frame.cameraPosition={0,2,0};frame.lights.push_back({{0,0,0,0},{3,2.5f,2,0},{0,-1,-.3f,0}});
    engine::RenderRuntime runtime(device,nullptr);
    auto baked=runtime.captureAtmosphere(frame);runtime.finish();
    if(baked.radiance.size()!=size_t(baked.width)*baked.height || baked.sunRadius<=0 || glm::length(baked.sunIrradiance)<=0)throw std::runtime_error("PT: render-thread sky bake is empty");
    double energy=0;for(auto pixel:baked.radiance){if(!std::isfinite(pixel.x) || !std::isfinite(pixel.y) || !std::isfinite(pixel.z) || glm::any(glm::lessThan(pixel,glm::vec3(0))))throw std::runtime_error("PT: baked HDR sky has invalid radiance");energy+=pixel.x+pixel.y+pixel.z;}
    if(energy<=0)throw std::runtime_error("PT: baked sky has no energy");
    Environment environment(baked.width,baked.height,std::move(baked.radiance));Random random(7);
    for(int i=0;i<1000;++i){auto sample=environment.sample(random);if(sample.pdf<=0 || !std::isfinite(sample.pdf))throw std::runtime_error("PT: baked environment sampling PDF is invalid");}
    validateGpuPathTracing(device);
    std::cout<<"CPU PT render-thread HDR sky bake and importance sampling passed\n";
}
} // namespace pt
