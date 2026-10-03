#include "renderer/rhi/GpuAtmosphere.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "renderer/rhi/ShadowRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
namespace render {
namespace{void check(bool v,const char* reason){if(!v)throw std::runtime_error(reason);}}
void validateAtmosphereRhi(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& directory){
    if(!d->computeLimits().maxStorageImages){std::cout<<"RHI atmosphere/water requires storage image compute; deferred on this backend\n";return;}
    GpuAtmosphere atmosphere(d,directory);atmosphere.update({},20);auto sky=atmosphere.read(1);
    for(uint32_t i=0;i<4;++i){const auto values=atmosphere.read(i);for(size_t j=0;j<values.size();j+=4)for(size_t c=0;c<3;++c){check(std::isfinite(values[j+c])&&values[j+c]>=-.0001f,"Atmosphere LUT nonfinite/negative");if(i==0)check(values[j+c]<=1.0001f,"Atmosphere transmittance outside 0..1");}}
    atmosphere.update({},20);check(atmosphere.read(1)==sky,"Atmosphere unchanged parameters were recomputed differently");atmosphere.update({},55);auto noon=atmosphere.read(1);bool changed=false;for(size_t i=0;i<sky.size();++i)if(std::abs(sky[i]-noon[i])>.01f)changed=true;check(changed,"Atmosphere ignored sun angle");
    FrameData f;f.sky=true;f.sunAngle=20;f.cameraPosition={0,2,3};f.view=glm::lookAt(f.cameraPosition,glm::vec3(0),glm::vec3(0,1,0));f.nearPlane=.1f;f.farPlane=20;
    glm::mat4 correction(1);correction[2][2]=.5f;correction[3][2]=.5f;f.viewProjection=correction*glm::perspective(glm::radians(65.f),1.f,f.nearPlane,f.farPlane)*f.view;f.lights={{{0,0,0,0},{3,3,3,0},{0,-1,-.5f,0}}};
    std::vector<MeshVertex> vertices{{{-4,-.5f,-4},{0,1,0},{0,0}},{{4,-.5f,-4},{0,1,0},{1,0}},{{4,-.5f,4},{0,1,0},{1,1}},{{-4,-.5f,4},{0,1,0},{0,1}}};
    auto mesh=std::make_shared<GpuMesh>(d,vertices,std::vector<uint32_t>{0,2,1,0,3,2});MaterialDesc m;m.parameters.albedoAlpha={.2f,.65f,.2f,1};m.parameters.factors={0,.8f,1,0};auto material=std::make_shared<GpuMaterial>(d,m);
    ForwardPbrRenderer renderer(d,directory,64,64,PbrPath::Scene);std::vector<DrawPacket> draws{{mesh,material,glm::mat4(1)}};renderer.render(f,draws);const auto dry=renderer.readHDR();
    {ShadowRenderer source(d,directory,32,atmosphere.irradiance());f.rsm=true;f.rsmSettings.sunBounce=false;f.rsmSettings.skyBounce=true;source.render(f,draws);auto flux=source.readRsmSource(0);double skyEnergy=0;for(size_t i=0;i<flux.size();i+=4)skyEnergy+=flux[i];check(skyEnergy>1e-6,"Sky RSM source did not capture irradiance");f.rsmSettings.skyBounce=false;source.render(f,draws);flux=source.readRsmSource(0);double zero=0;for(size_t i=0;i<flux.size();i+=4)zero+=flux[i];check(zero<skyEnergy*.001,"Sky bounce toggle left indirect source energy");f.rsm=false;f.rsmSettings={};}
    OceanSurfaceSettings ocean;ocean.spectrum.size=16;ocean.spectrum.length=8;ocean.spectrum.amplitude=0;ocean.meshSize=33;ocean.seaLevel=0;f.oceans={ocean};renderer.render(f,draws);auto water=renderer.readHDR();size_t surface=0;
    for(size_t i=0;i<water.size();i+=4){for(int c=0;c<4;++c)check(std::isfinite(water[i+c]),"Water surface output nonfinite");if(std::abs(water[i]-dry[i])>.03f)++surface;}check(surface>100,"Water compute -> vertex sampling -> HDR surface missing");
    f.timeSeconds=.2f;renderer.render(f,draws);auto stationary=renderer.readHDR();for(size_t i=0;i<water.size();++i)check(std::abs(stationary[i]-water[i])<.005f,"Flat water changed across previous displacement copy");
    f.oceans[0].refraction=false;renderer.render(f,draws);auto opaque=renderer.readHDR();bool refraction=false;for(size_t i=0;i<water.size();++i)if(std::abs(opaque[i]-water[i])>.01f)refraction=true;check(refraction,"Water refraction toggle ignored opaque scene snapshot");
    renderer.resize(48,32);renderer.render(f,draws);check(renderer.readOutput().size()==48*32*4,"Water surface resize failed");
    std::cout<<"RHI atmosphere transmittance/sky/multiple scattering/irradiance, sun update, water vertex FFT sampling/refraction/foam/HDR and previous-frame texture copy passed; water pixels "<<surface<<"\n";
}
}
