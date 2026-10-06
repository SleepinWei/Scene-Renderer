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
    render::OceanSurfaceSettings water;water.spectrum.size=64;water.spectrum.length=32;water.spectrum.amplitude=.001f;water.meshSize=17;water.seaLevel=.4f;water.detailWaves=false;water.absorption={.2f,.1f,.05f};water.scattering=glm::vec3(0);source.frame.oceans={water};
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
    // Smooth sheets: colored NEE visibility and unchanged medium identity.
    auto sheetRoom=makeCausticsScene(32,24,false).snapshot;auto pane=sheetRoom.draws[0];pane.model=glm::translate(glm::mat4(1),glm::vec3(0,1.5f,0));pane.pathTracingKind=5;pane.pathTracingIor=1.5f;pane.parameters.albedoAlpha={.8f,.9f,1,.85f};sheetRoom.draws.push_back(pane);
    auto compareSheet=[&](const CpuScene &scene,const char *label){auto a=render(scene,options),b=renderGpu(scene,options,device);double error=0,energy=0;for(size_t i=0;i<a.radiance.size();++i){error+=glm::length(a.radiance[i]-b.radiance[i]);energy+=glm::length(a.radiance[i]);}if(a.nonFiniteSamples||b.nonFiniteSamples||energy<=0||error/energy>.025)throw std::runtime_error(std::string("PT: CPU/GPU thin sheet mismatch: ")+label);std::cout<<label<<" CPU/GPU relative L1 "<<error/energy<<'\n';};
    options.samples=512;options.maxDepth=16;CpuScene sheets(sheetRoom);if(!sheets.media().empty())throw std::runtime_error("PT: sheet became a volume");compareSheet(sheets,"Tinted thin sheet / area NEE");
    for(auto model:{1u,2u}){auto controlled=sheetRoom;for(auto &draw:controlled.draws)draw.pathTracingBsdfModel=model;CpuScene common(controlled);compareSheet(common,model==1?"Controlled Lambert / thin sheet":"Controlled additive GGX / thin sheet");}
    sheetRoom.draws.erase(sheetRoom.draws.begin()+1,sheetRoom.draws.begin()+3);sheetRoom.draws[0].parameters.albedoAlpha=glm::vec4(1);sheetRoom.draws[0].parameters.factors.y=.5f;sheetRoom.draws[1].parameters.albedoAlpha=glm::vec4(1);CpuScene whiteSheet(sheetRoom);whiteSheet.environment=waterScene.environment;compareSheet(whiteSheet,"Thin sheet / environment MIS");
    // Bounded pool water uses tangent normals and linear textured roughness, unlike FFT world normals.
    auto poolRoom=makeCausticsScene(32,24,false).snapshot;poolRoom.draws.erase(poolRoom.draws.begin()+2);
    auto pool=makeMediumValidationScene(32,24,true).snapshot.draws[0];pool.pathTracingIor=1.333f;pool.pathTracingAbsorption={.12f,.035f,.015f};pool.pathTracingScattering=glm::vec3(0);pool.pathTracingRoughness=.1f;pool.pathTracingRoughnessTexture=true;
    pool.model=glm::translate(glm::mat4(1),glm::vec3(0,.4f,0))*glm::scale(glm::mat4(1),glm::vec3(2,.3f,1.5f));
    auto maps=std::make_shared<render::MaterialPayload>();maps->images[1]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{1,1,{145,118,253,255}});maps->images[3]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{2,1,{255,40,0,255,255,85,0,255}});pool.material=maps;pool.parameters.emissiveNormal.w=1;poolRoom.draws.push_back(pool);
    CpuScene poolAir(poolRoom);poolAir.environment=waterScene.environment;compareSheet(poolAir,"Bounded pool / tangent normal / roughness atlas");
    for(auto &draw:poolRoom.draws)draw.pathTracingBsdfModel=2;CpuScene commonPool(poolRoom);commonPool.environment=waterScene.environment;compareSheet(commonPool,"Controlled correlated GGX / bounded pool");
    poolRoom.frame.cameraPosition={0,.4f,0};poolRoom.frame.viewProjection=depth*glm::perspective(glm::radians(50.f),32.f/24,.1f,100.f)*glm::lookAt(poolRoom.frame.cameraPosition,glm::vec3(0,2,4),glm::vec3(0,1,0));
    CpuScene poolInside(poolRoom);poolInside.environment=waterScene.environment;compareSheet(poolInside,"Inside bounded pool / mapped exit boundary");
    auto compareSurface=[&](const CpuScene &scene,const char *label){auto a=render(scene,options),b=renderGpu(scene,options,device);double error=0,energy=0;for(size_t i=0;i<a.radiance.size();++i){error+=glm::length(a.radiance[i]-b.radiance[i]);energy+=glm::length(a.radiance[i]);}if(a.nonFiniteSamples||b.nonFiniteSamples||energy<=0||error/energy>.025)throw std::runtime_error(std::string("PT: CPU/GPU shoreline/solar mismatch: ")+label);std::cout<<label<<" CPU/GPU relative L1 "<<error/energy<<'\n';};
    options.samples=512;options.maxDepth=16;
    // Sand detail is sampled independently of the landscape capture texture budget.
    auto shoreRoom=makeCausticsScene(32,24,false).snapshot;shoreRoom.draws.erase(shoreRoom.draws.begin()+1);
    auto &shore=shoreRoom.draws[0];shore.pathTracingKind=1;shore.extension.shoreHeight={0,10,1,1};shore.extension.shoreSurface={.3f,.7f,.95f,1};
    shore.pathTracingShoreline[0]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{2,2,{200,60,20,255,40,100,220,255,40,100,220,255,200,60,20,255}});
    shore.pathTracingShoreline[1]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{2,1,{160,110,250,255,105,155,250,255}});
    shore.pathTracingShoreline[2]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{2,1,{255,120,0,255,255,210,0,255}});
    shore.pathTracingShoreline[3]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{2,2,{255,255,255,255,100,100,100,255,255,255,255,255,100,100,100,255}});
    CpuScene shoreScene(shoreRoom);shoreScene.environment=waterScene.environment;compareSurface(shoreScene,"World-space sand / normals / roughness / wetness");
    CpuScene solarWater(frozen);solarWater.environment=std::make_shared<Environment>(8,4,std::vector<glm::vec3>(32,glm::vec3(.05f)));solarWater.sunDirection=glm::normalize(glm::vec3(.1f,1,.05f));solarWater.sunIrradiance=glm::vec3(2);solarWater.sunRadius=.03f;
    options.samples=1024;options.maxDepth=16;compareSurface(solarWater,"Refracted solar continuation / full mixture PDF");
    // Exercise spectral free flight, phase NEE and boundary transitions on both kernels.
    for(auto &draw:frozen.draws)if(draw.pathTracingKind==3){draw.pathTracingScattering={.12f,.25f,.4f};draw.pathTracingAnisotropy=.65f;}
    options.maxDepth=64;options.samples=1024;
    auto volumeCompare=[&](const CpuScene &scene,const char *label){auto a=render(scene,options),b=renderGpu(scene,options,device);double error=0,energy=0;for(size_t i=0;i<a.radiance.size();++i){error+=glm::length(a.radiance[i]-b.radiance[i]);energy+=glm::length(a.radiance[i]);}if(!a.volumeEvents||!b.volumeEvents||energy<=0||error/energy>.08)throw std::runtime_error(std::string("PT: CPU/GPU volume mismatch: ")+label);std::cout<<label<<" CPU/GPU relative L1 "<<error/energy<<", volume events "<<a.volumeEvents<<" / "<<b.volumeEvents<<'\n';};
    CpuScene scatteringWater(frozen);scatteringWater.environment=waterScene.environment;volumeCompare(scatteringWater,"Submerged FFT water scattering");
    CpuScene solarScatteringWater(frozen);solarScatteringWater.environment=solarWater.environment;solarScatteringWater.sunDirection=solarWater.sunDirection;solarScatteringWater.sunIrradiance=solarWater.sunIrradiance;solarScatteringWater.sunRadius=solarWater.sunRadius;volumeCompare(solarScatteringWater,"Refracted solar / HG phase mixture PDF");
    auto mediumFixture=makeMediumValidationScene(32,24);CpuScene jadeWater(mediumFixture.snapshot);jadeWater.environment=waterScene.environment;volumeCompare(jadeWater,"Nested water/jade random walk");
    for(auto &draw:mediumFixture.snapshot.draws)if(draw.pathTracingIor>0)draw.pathTracingRoughness=draw.pathTracingKind==3?.18f:.32f;
    CpuScene roughJadeWater(mediumFixture.snapshot);roughJadeWater.environment=waterScene.environment;volumeCompare(roughJadeWater,"Rough nested water/jade NEE");
    mediumFixture.snapshot.frame.cameraPosition={0,.2f,0};mediumFixture.snapshot.frame.viewProjection=depth*glm::perspective(glm::radians(50.f),32.f/24,.1f,100.f)*glm::lookAt(mediumFixture.snapshot.frame.cameraPosition,glm::vec3(0,.2f,4),glm::vec3(0,1,0));CpuScene insideJade(mediumFixture.snapshot);insideJade.environment=waterScene.environment;volumeCompare(insideJade,"Camera inside water/jade");
    Options unsupported=options;unsupported.guiding=true;bool learningRejected=false;try{renderGpu(jadeWater,unsupported,device);}catch(const std::invalid_argument &){learningRejected=true;}if(!learningRejected)throw std::runtime_error("PT: volume guiding was not rejected");
    // A small volume enables the thin-boundary ray epsilon; area-light visibility
    // must still trim both endpoints at their world-coordinate precision.
    auto originRoom=makeCausticsScene(32,24,false).snapshot;auto distantMedium=makeMediumValidationScene(32,24,true).snapshot.draws[0];distantMedium.model=glm::translate(glm::mat4(1),glm::vec3(20,0,0));originRoom.draws.push_back(distantMedium);
    CpuScene originScene(originRoom);options.samples=512;options.maxDepth=4;auto originImage=render(originScene,options);auto largeRoom=originRoom;glm::vec3 translation{10000,0,0};largeRoom.frame.cameraPosition+=translation;largeRoom.frame.viewProjection=largeRoom.frame.viewProjection*glm::translate(glm::mat4(1),-translation);for(auto &draw:largeRoom.draws)draw.model=glm::translate(glm::mat4(1),translation)*draw.model;CpuScene largeScene(largeRoom);auto largeCpu=render(largeScene,options),largeGpu=renderGpu(largeScene,options,device);double originEnergy=0,largeEnergy=0,largeGpuEnergy=0,largeError=0;for(size_t i=0;i<largeCpu.radiance.size();++i){originEnergy+=glm::length(originImage.radiance[i]);largeEnergy+=glm::length(largeCpu.radiance[i]);largeGpuEnergy+=glm::length(largeGpu.radiance[i]);largeError+=glm::length(largeCpu.radiance[i]-largeGpu.radiance[i]);}if(originEnergy<=0||std::abs(largeEnergy/originEnergy-1)>.05||largeError/largeEnergy>.08)throw std::runtime_error("PT: scaled area-light shadow segment self-occludes");std::cout<<"Area emitter +10km energy ratio "<<largeEnergy/originEnergy<<", GPU/CPU "<<largeGpuEnergy/largeEnergy<<", relative L1 "<<largeError/largeEnergy<<'\n';
    std::cout<<"Underwater CPU/GPU relative L1 "<<underError/underEnergy<<'\n';
    std::cout<<"Native FFT PT capture motion "<<motion<<", CPU/GPU water relative L1 "<<error/reference<<'\n';
    std::cout<<"CPU PT render-thread HDR sky bake and importance sampling passed\n";
}
} // namespace pt
