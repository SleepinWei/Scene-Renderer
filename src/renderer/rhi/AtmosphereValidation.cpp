#include "renderer/rhi/GpuAtmosphere.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "renderer/rhi/ShadowRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
namespace render {
namespace{void check(bool v,const char* reason){if(!v)throw std::runtime_error(reason);}}
void validateSunFixes(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory) {
    AtmosphereSettings parameters;GpuAtmosphere sky(device,directory);SunState sun;
    sky.update(parameters,sun);auto initial=sky.read(1);auto counts=sky.updateCounts();
    sky.update(parameters,sun);check(sky.updateCounts().sky==counts.sky,"Sky cache ignored unchanged state");
    sun.direction=glm::normalize(glm::vec3(1,1,-1));sky.update(parameters,sun);
    auto changed=sky.read(1);double difference=0;for(size_t i=0;i<initial.size();++i)difference=std::max(difference,double(std::abs(changed[i]-initial[i])));
    check(difference>.01,"Sky ignored the solar azimuth");check(sky.updateCounts().transmittance==counts.transmittance && sky.updateCounts().multiple==counts.multiple,"Solar motion rebuilt static atmosphere LUTs");
    sun.observerHeightKm=1;sky.update(parameters,sun);auto high=sky.read(1);difference=0;for(size_t i=0;i<high.size();++i)difference=std::max(difference,double(std::abs(high[i]-changed[i])));check(difference>.01,"Sky ignored observer height");
    sun.multipleScattering=0;sky.update(parameters,sun);auto single=sky.read(1);sun.multipleScattering=1;sky.update(parameters,sun);auto multiple=sky.read(1);double energy=0;
    for(size_t i=0;i<multiple.size();i+=4)for(int c=0;c<3;++c){check(std::isfinite(multiple[i+c]),"Multiple scattering produced nonfinite radiance");check(multiple[i+c]>=single[i+c]-1e-5,"Multiple scattering removed radiance");energy+=multiple[i+c]-single[i+c];}
    check(energy>1,"Multiple scattering was not connected to SkyView");
    SunState zenith;zenith.direction={0,1,0};zenith.observerHeightKm=.001f;auto reference=solarTransmittance(parameters,zenith);auto trans=sky.read(0);
    for(int c=0;c<3;++c)check(std::abs(trans[c]-reference[c])<.005,"GPU zenith transmittance differs from double CPU integration");
    zenith.direction={0,-1,0};check(glm::length(solarTransmittance(parameters,zenith))==0,"Sun below the planet still illuminated surfaces");
    auto invalid=parameters;invalid.densities.w=1;bool rejected=false;try{sky.update(invalid,sun);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"g=1 was accepted by RHI atmosphere");
    parameters.densities.w=.99f;sun.observerHeightKm=.001f;sun.direction={0,0,-1};sky.update(parameters,sun);for(float v:sky.read(1))check(std::isfinite(v),"Sun-aligned extreme Mie parameter produced NaN");

    FrameData f;f.sky=true;f.ambient=0;f.cameraPosition={0,2,0};f.nearPlane=.1f;f.farPlane=20;
    f.atmosphere.rayleigh=f.atmosphere.mie=f.atmosphere.extinction=f.atmosphere.absorption=glm::vec4(0);f.atmosphere.absorption.w=15;
    const auto solar=glm::normalize(glm::vec3(0,.5f,-.8660254f));f.lights={{{0,0,0,0},{1,.8f,.6f,0},glm::vec4(-solar,0)}};
    auto pointCamera=[&](glm::vec3 direction){f.view=glm::lookAt(f.cameraPosition,f.cameraPosition+direction,glm::vec3(0,1,0));glm::mat4 depth(1);depth[2][2]=.5f;depth[3][2]=.5f;f.viewProjection=depth*glm::perspective(glm::radians(10.f),1.f,f.nearPlane,f.farPlane)*f.view;};
    pointCamera(solar);ForwardPbrRenderer renderer(device,directory,256,256,PbrPath::Scene);
    auto render=[&](){renderer.render(f,{});auto pixels=renderer.readHDR();for(float v:pixels)check(std::isfinite(v),"Solar HDR overflowed or produced NaN");return pixels;};
    auto disk=render();f.directionalEnabled=false;check(render()==disk,"Disabling direct lighting removed or changed the visible sky sun");f.directionalEnabled=true;
    auto invalidFrame=f;invalidFrame.atmosphere.radii.z=invalidFrame.atmosphere.radii.w;bool badRadius=false;try{renderer.render(invalidFrame,{});}catch(const std::invalid_argument&){badRadius=true;}check(badRadius,"Invalid radii reached observer height clamp");
    double sum=0,max=0;size_t lit=0;for(size_t i=0;i<disk.size();i+=4){sum+=disk[i];max=std::max(max,double(disk[i]));lit+=disk[i]>1;}
    double expected=1/(3.141592653589793*std::pow(std::sin(.005),2));check(std::abs(max/expected-1)<.002,"Solar disk radiance is not irradiance / projected solid angle");
    check(lit>100 && lit<500,"Solar disk has the wrong angular footprint");
    const size_t center=4*(128*256+128);check(std::abs(disk[center+1]/disk[center]-.8)<.002 && std::abs(disk[center+2]/disk[center]-.6)<.002,"Solar disk ignored RGB irradiance");
    // An opaque foreground quad must hide the analytic disk, in both scene paths.
    const glm::vec3 centerPoint=f.cameraPosition+solar*2.f,tangent{.5f,0,0},bitangent=glm::cross(solar,glm::vec3(1,0,0))*.5f;
    std::vector<MeshVertex> blockerVertices;
    for(auto corner:{centerPoint-tangent-bitangent,centerPoint+tangent-bitangent,centerPoint+tangent+bitangent,centerPoint-tangent+bitangent})blockerVertices.push_back({corner,-solar,{0,0}});
    auto blocker=std::make_shared<GpuMesh>(device,blockerVertices,std::vector<uint32_t>{0,1,2,0,2,3});
    MaterialDesc black;black.parameters.albedoAlpha={0,0,0,1};black.parameters.factors={0,1,1,0};auto opaque=std::make_shared<GpuMaterial>(device,black);
    for(bool forward:{false,true}) {
        f.forwardShading=forward;renderer.render(f,{{blocker,opaque,glm::mat4(1)}});auto pixels=renderer.readHDR();
        check(pixels[center]<1,"Foreground geometry failed to occlude the solar disk");
    }
    f.forwardShading=false;
    f.atmosphere.radii.y=.01f;auto large=render();double largeSum=0,largeMax=0;size_t largeLit=0;for(size_t i=0;i<large.size();i+=4){largeSum+=large[i];largeMax=std::max(largeMax,double(large[i]));largeLit+=large[i]>1;}
    check(double(largeLit)/lit>3.2 && double(largeLit)/lit<4.8,"Doubling solar radius did not quadruple its area");check(largeMax/max>.24 && largeMax/max<.26,"Solar radius changed radiance incorrectly");check(std::abs(largeSum/sum-1)<.15,"Changing solar radius changed total solar energy");
    f.lights[0].directionOuter={-1,-.2f,0,0};auto away=render();double awayEnergy=0;for(size_t i=0;i<away.size();i+=4)awayEnergy+=away[i];check(awayEnergy<1e-6,"Visible sun did not follow DirectionLight");
    f.atmosphere.radii.y=.005f;f.lights[0].directionOuter={0,0,1,0};pointCamera({0,0,-1});auto sunset=render();double sunsetEnergy=0;for(size_t i=0;i<sunset.size();i+=4)sunsetEnergy+=sunset[i];check(sunsetEnergy/sum>.35 && sunsetEnergy/sum<.8,"Solar disk was not partially occulted at the horizon");
    float below=glm::radians(-5.f);glm::vec3 night{0,std::sin(below),-std::cos(below)};f.lights[0].directionOuter=glm::vec4(-night,0);pointCamera(night);auto hidden=render();double hiddenEnergy=0;for(size_t i=0;i<hidden.size();i+=4)hiddenEnergy+=hidden[i];check(hiddenEnergy<1e-6,"Solar disk remained visible below the horizon");
    f.lights[0].directionOuter=glm::vec4(-solar,0);pointCamera(solar);f.atmosphere.radii.y=.0005f;auto hdr=render();for(float v:hdr)check(std::isfinite(v),"Small HDR solar disk overflowed float16");
    f.atmosphere.radii.y=.005f;f.lights[0].colorInner={0,0,0,0};auto off=render();double offEnergy=0;for(size_t i=0;i<off.size();i+=4)offEnergy+=off[i];check(offEnergy==0,"Zero solar irradiance left a stale sun disk");
    std::cout<<"RHI solar regression: disk pixels "<<lit<<" -> "<<largeLit<<", energy ratio "<<largeSum/sum<<", horizon energy ratio "<<sunsetEnergy/sum<<", radiance "<<max<<", multiple-scattering added energy "<<energy<<"; direction/observer/cache/CPU reference/extreme Mie/HDR tests passed\n";
}

void validateAtmosphereRhi(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& directory){
    if(!d->computeLimits().maxStorageImages){std::cout<<"RHI atmosphere/water requires storage image compute; deferred on this backend\n";return;}
    validateSunFixes(d,directory);
    GpuAtmosphere atmosphere(d,directory);atmosphere.update({},20);auto sky=atmosphere.read(1);
    for(uint32_t i=0;i<4;++i){const auto values=atmosphere.read(i);for(size_t j=0;j<values.size();j+=4)for(size_t c=0;c<3;++c){check(std::isfinite(values[j+c])&&values[j+c]>=-.0001f,"Atmosphere LUT nonfinite/negative");if(i==0)check(values[j+c]<=1.0001f,"Atmosphere transmittance outside 0..1");}}
    atmosphere.update({},20);check(atmosphere.read(1)==sky,"Atmosphere unchanged parameters were recomputed differently");atmosphere.update({},55);auto noon=atmosphere.read(1);bool changed=false;for(size_t i=0;i<sky.size();++i)if(std::abs(sky[i]-noon[i])>.01f)changed=true;check(changed,"Atmosphere ignored sun angle");
    FrameData f;f.sky=true;f.sunAngle=20;f.cameraPosition={0,2,3};f.view=glm::lookAt(f.cameraPosition,glm::vec3(0),glm::vec3(0,1,0));f.nearPlane=.1f;f.farPlane=20;
    glm::mat4 correction(1);correction[2][2]=.5f;correction[3][2]=.5f;f.viewProjection=correction*glm::perspective(glm::radians(65.f),1.f,f.nearPlane,f.farPlane)*f.view;f.lights={{{0,0,0,0},{20,20,20,0},{0,-1,-.5f,0}}};
    std::vector<MeshVertex> vertices{{{-4,-.5f,-4},{0,1,0},{0,0}},{{4,-.5f,-4},{0,1,0},{1,0}},{{4,-.5f,4},{0,1,0},{1,1}},{{-4,-.5f,4},{0,1,0},{0,1}}};
    auto mesh=std::make_shared<GpuMesh>(d,vertices,std::vector<uint32_t>{0,2,1,0,3,2});MaterialDesc m;m.parameters.albedoAlpha={.2f,.65f,.2f,1};m.parameters.factors={0,.8f,1,0};auto material=std::make_shared<GpuMaterial>(d,m);
    ForwardPbrRenderer renderer(d,directory,64,64,PbrPath::Scene);std::vector<DrawPacket> draws{{mesh,material,glm::mat4(1)}};renderer.render(f,draws);const auto dry=renderer.readHDR();
    {ShadowRenderer source(d,directory,32,atmosphere.irradiance());f.rsm=true;f.rsmSettings.sunBounce=false;f.rsmSettings.skyBounce=true;source.render(f,draws);auto flux=source.readRsmSource(0);double skyEnergy=0;for(size_t i=0;i<flux.size();i+=4)skyEnergy+=flux[i];check(skyEnergy>1e-6,"Sky RSM source did not capture irradiance");f.rsmSettings.skyBounce=false;source.render(f,draws);flux=source.readRsmSource(0);double zero=0;for(size_t i=0;i<flux.size();i+=4)zero+=flux[i];check(zero<skyEnergy*.001,"Sky bounce toggle left indirect source energy");f.rsm=false;f.rsmSettings={};}
    OceanSurfaceSettings ocean;ocean.spectrum.size=16;ocean.spectrum.length=8;ocean.spectrum.amplitude=0;ocean.meshSize=33;ocean.seaLevel=0;f.oceans={ocean};renderer.render(f,draws);auto water=renderer.readHDR();size_t surface=0;
    for(size_t i=0;i<water.size();i+=4){for(int c=0;c<4;++c)check(std::isfinite(water[i+c]),"Water surface output nonfinite");if(std::abs(water[i]-dry[i])>.03f)++surface;}check(surface>100,"Water compute -> vertex sampling -> HDR surface missing");
    f.oceans[0].waterMask=std::make_shared<const ImageRGBA8>(ImageRGBA8{1,1,{0,0,0,255}});
    renderer.render(f,draws);auto masked=renderer.readHDR();
    for(size_t i=0;i<dry.size();++i)check(std::abs(masked[i]-dry[i])<.005f,"Black water mask did not restore dry scene");
    f.oceans[0].waterMask=std::make_shared<const ImageRGBA8>(ImageRGBA8{1,1,{255,255,255,255}});
    renderer.render(f,draws);auto unmasked=renderer.readHDR();
    for(size_t i=0;i<water.size();++i)check(std::abs(unmasked[i]-water[i])<.005f,"White water mask changed unmasked water");
    OceanSurfaceSettings tiled=ocean;tiled.surfaceLength=16;
    OceanSurface tileSurface(d,directory,tiled);
    check(!tileSurface.compatible(ocean),"Ocean display domain change retained incompatible grid");
    f.timeSeconds=.2f;renderer.render(f,draws);auto stationary=renderer.readHDR();for(size_t i=0;i<water.size();++i)check(std::abs(stationary[i]-water[i])<.005f,"Flat water changed across previous displacement copy");
    f.oceans[0].refraction=false;renderer.render(f,draws);auto opaque=renderer.readHDR();bool refraction=false;for(size_t i=0;i<water.size();++i)if(std::abs(opaque[i]-water[i])>.01f)refraction=true;check(refraction,"Water refraction toggle ignored opaque scene snapshot");
    renderer.resize(48,32);renderer.render(f,draws);check(renderer.readOutput().size()==48*32*4,"Water surface resize failed");
    std::cout<<"RHI atmosphere transmittance/sky/multiple scattering/irradiance, sun update, water vertex FFT sampling/refraction/foam/HDR and previous-frame texture copy passed; water pixels "<<surface<<"\n";
    validateWaterSurface(d,directory);
}
}
