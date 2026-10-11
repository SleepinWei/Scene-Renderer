#include "renderer/rhi/GpuAirFog.h"
#include "renderer/rhi/GpuClouds.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "renderer/rhi/ShadowRenderer.h"
#include "renderer/rhi/AtmosphereBake.h"
#include "PT/CpuPathTracer.h"
#include "PT/GpuPathTracer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <json/json.hpp>
#include <stb/stb_image_write.h>
namespace render {
namespace {void check(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
void ppm(const std::filesystem::path& path,const std::vector<uint8_t>& image){std::ofstream out(path,std::ios::binary);out<<"P6\n320 240\n255\n";for(size_t i=0;i<image.size();i+=4)out.write(reinterpret_cast<const char*>(image.data()+i),3);}
struct AerialReference {glm::dvec3 L{0},T{1};};
// Independent double-precision quadrature; no GPU LUT or shared shader helpers.
AerialReference aerialReference(const FrameData& f,const SunState& sun,glm::dvec3 origin,glm::dvec3 direction,double distance,double water=-1e30){
    AerialReference out;const auto& a=f.atmosphere;double begin=f.weather.aerialStartDistance,end=std::min(distance,double(f.weather.aerialMaxDistance));
    if(origin.y<water){if(direction.y<=0)return out;begin=std::max(begin,(water-origin.y)/direction.y);}else if(direction.y<0)end=std::min(end,(water-origin.y)/direction.y);
    if(end<=begin||f.weather.aerialDistanceScale==0)return out;
    const double bottom=a.radii.w,r=bottom+std::clamp((origin.y+direction.y*begin-f.seaLevelMeters)*.001,.001,double(a.radii.z-a.radii.w)-.001),mu=direction.y;
    const double horizon=-std::sqrt(std::max(0.,1-bottom*bottom/(r*r)));const bool ground=mu<horizon;
    const double boundary=ground?-r*mu-std::sqrt(std::max(0.,r*r*(mu*mu-1)+bottom*bottom)):-r*mu+std::sqrt(std::max(0.,r*r*(mu*mu-1)+double(a.radii.z)*a.radii.z));
    const double length=std::min((end-begin)*.001,std::max(0.,boundary));if(length<=0)return out;
    const glm::dvec3 solar=glm::normalize(glm::dvec3(sun.direction));const double nu=glm::dot(direction,solar),g=a.densities.w,pi=3.141592653589793;
    const double phaseR=3/(16*pi)*(1+nu*nu),phaseM=3/(8*pi)*(1-g*g)/(2+g*g)*(1+nu*nu)/std::pow(std::max(1+g*g-2*g*nu,1e-6),1.5);
    for(int i=0;i<256;++i){double t0=double(i)/256,t1=double(i+1)/256,start=t0*t0*length,finish=t1*t1*length,t=(start+finish)*.5;
        auto q=glm::dvec3(0,r,0)+direction*t;double ri=glm::length(q),h=std::max(ri-bottom,0.),muSun=std::clamp(glm::dot(q,solar)/ri,-1.,1.);
        const auto rayleigh=glm::dvec3(a.rayleigh)*std::exp(-h/a.densities.x),mie=glm::dvec3(a.mie)*std::exp(-h/a.densities.y);
        const auto extinction=rayleigh+glm::dvec3(a.extinction)*std::exp(-h/a.densities.y)+glm::dvec3(a.absorption)*std::max(0.,1-std::abs(h-a.densities.z)/a.absorption.w);
        auto localSun=sun;localSun.observerHeightKm=float(std::max(h,.001));localSun.direction={float(std::sqrt(std::max(0.,1-muSun*muSun))),float(muSun),0};
        const auto source=(rayleigh*phaseR+mie*phaseM)*glm::dvec3(solarTransmittance(a,localSun))*glm::dvec3(sun.irradiance);
        const double dt=(finish-start)*f.weather.aerialDistanceScale;const auto stepT=glm::exp(-extinction*dt);glm::dvec3 weight;
        for(int c=0;c<3;++c)weight[c]=extinction[c]>1e-10?(1-stepT[c])/extinction[c]:dt;
        out.L+=out.T*weight*source;out.T*=stepT;
    }return out;
}
}
void validateWeatherLighting(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory){
    check(std::abs(airOpticalDepth({0,0,0},{100,0,0},0,100,.01f)-1)<1e-6,"Horizontal fog optical depth differs from Beer law");
    check(airOpticalDepth({0,-2,0},{0,-1,0},0,100,.01f,0)==0,"Fog integrated submerged water");
    const float vertical=airOpticalDepth({0,-100,0},{0,100,0},0,100,.01f,0);
    check(std::abs(vertical-(1-std::exp(-1.f)))<1e-5,"Fog did not clip water before height integration");
    check(std::isfinite(airOpticalDepth({0,100000,0},{0,0,0},0,1,.01f)),"Downward fog integral overflowed");
    FrameData f;f.viewportWidth=320;f.viewportHeight=240;f.cameraPosition={0,90,300};f.farPlane=2000;f.timeSeconds=1;
    f.view=glm::lookAt(f.cameraPosition,glm::vec3(0,0,0),glm::vec3(0,1,0));glm::mat4 remap(1);remap[2][2]=.5;remap[3][2]=.5;
    f.viewProjection=remap*glm::perspective(glm::radians(55.f),320.f/240,.1f,f.farPlane)*f.view;
    f.sky=true;f.sunAngle=90;f.ambient=0;f.inverseSquareLocalLights=true;f.lights={{{0,0,0,0},{1,1,1,0},{0,-1,0,0}}};
    f.clouds.enabled=true;f.clouds.temporal=false;f.clouds.voxelResolution=64;f.clouds.volumeCenter={0,180,0};f.clouds.volumeSize={300,150,300};f.clouds.coverage=1;f.clouds.dynamicCoverage=true;f.clouds.density=.01f;f.clouds.shapeScale=100;f.clouds.weatherScale=1000;f.clouds.advection=glm::dvec2(0);f.clouds.steps=48;
    std::vector<MeshVertex> vertices={{{-400,0,-400},{0,1,0},{0,0}},{{400,0,-400},{0,1,0},{1,0}},{{400,0,400},{0,1,0},{1,1}},{{-400,0,400},{0,1,0},{0,1}}};std::vector<uint32_t> indices{0,2,1,0,3,2};
    auto mesh=std::make_shared<GpuMesh>(device,vertices,indices);MaterialDesc m;m.parameters.albedoAlpha={.5f,.5f,.5f,1};m.parameters.factors={0,.85f,1,0};auto mat=std::make_shared<GpuMaterial>(device,m);std::vector<DrawPacket> draws{{mesh,mat}};
    ForwardPbrRenderer renderer(device,directory,320,240,PbrPath::Scene);renderer.render(f,draws);auto unshadowed=renderer.readHDR();auto unshadowedImage=renderer.readOutput();
    f.weather.enabled=true;f.weather.cloudShadowStrength=1;f.weather.fog=false;f.weather.aerialPerspective=false;renderer.render(f,draws);auto shadowed=renderer.readHDR();auto shadowImage=renderer.readOutput();auto field=renderer.readCloudShadow();auto positions=renderer.readGBuffer(0);
    size_t darker=0;for(size_t i=0;i<shadowed.size();i+=4)if(positions[i+3]>0&&unshadowed[i]-shadowed[i]>.005f)++darker;
    check(darker>100,"Cloud field did not attenuate ground sunlight");
    const auto voxels=renderer.readCloudVoxels();const auto projection=renderer.shadowParameters();const int n=64,columns=8,atlasWidth=(n+2)*columns;
    auto potential=[&](glm::vec3 uv){if(glm::any(glm::lessThan(uv,glm::vec3(0)))||glm::any(glm::greaterThan(uv,glm::vec3(1))))return 0.f;auto p=uv*float(n)-.5f;auto low=glm::ivec3(glm::floor(p));auto weight=glm::fract(p);float result=0;
        for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x){const int slice=std::clamp(low.z+z,0,n-1),tx=slice%columns*(n+2)+low.x+x+1,ty=slice/columns*(n+2)+low.y+y+1;result+=float(voxels[(size_t(ty)*atlasWidth+tx)*4+2])/255*(x?weight.x:1-weight.x)*(y?weight.y:1-weight.y)*(z?weight.z:1-weight.z);}return result;};
    double shadowTauError=0;for(int z=8;z<128;z+=16)for(int x=8;x<128;x+=16){glm::vec2 xz=glm::vec2(projection.cloudPatch)+glm::vec2((x+.5f)/128,(z+.5f)/128)*glm::vec2(projection.cloudPatch.z,projection.cloudPatch.w);double tau=0;
        for(int i=0;i<64;++i){glm::vec3 point(xz.x,f.clouds.volumeCenter.y-f.clouds.volumeSize.y*.5f+(i+.5f)*f.clouds.volumeSize.y/64,xz.y);auto uv=(point-f.clouds.volumeCenter)/f.clouds.volumeSize+.5f;float density=std::clamp(potential(uv)*.425f*8,0.f,1.f);tau+=density*f.clouds.density*f.clouds.volumeSize.y/64;}
        shadowTauError=std::max(shadowTauError,std::abs(field[z*128+x]-std::exp(-tau)));}
    check(shadowTauError<.001,"Cloud ground transmission differs from independent frozen-density Beer integration");
    const auto range=std::minmax_element(field.begin(),field.end());check(*range.first<.9f&&*range.second>.99f,"Ground cloud field lost shadow/clear regions");
    const auto originalBakes=renderer.cloudShadowBakes();f.weather.cloudShadowStrength=.3f;renderer.render(f,draws);auto weakCloud=renderer.readHDR();
    double cloudStrengthError=0;for(size_t at=0;at<weakCloud.size();at+=4)if(positions[at+3]>0)for(int c=0;c<3;++c)cloudStrengthError=std::max(cloudStrengthError,double(std::abs(weakCloud[at+c]-(unshadowed[at+c]*.7f+shadowed[at+c]*.3f))));
    check(cloudStrengthError<.003&&renderer.cloudShadowBakes()==originalBakes,"Ground shadow strength altered raw transmission or failed radiance interpolation");
    f.weather.cloudShadowStrength=0;renderer.render(f,draws);check(renderer.readHDR()==unshadowed,"Zero ground cloud shadow strength did not restore clear solar illumination");
    f.weather.cloudShadowStrength=1;
    const auto count=renderer.cloudShadowBakes();renderer.render(f,draws);check(renderer.readOutput()==shadowImage&&renderer.cloudShadowBakes()==count,"Paused cloud shadows rebuilt or changed");
    f.timeSeconds+=.05f;renderer.render(f,draws);check(renderer.cloudShadowBakes()==count,"Cloud shadow interval did not limit updates");
    f.timeSeconds+=.06f;renderer.render(f,draws);check(renderer.cloudShadowBakes()==count+1,"Cloud shadow cache missed its due update");
    f.clouds.coverage=.005f;renderer.render(f,draws);f.clouds.coverage=0;renderer.render(f,draws);auto clearField=renderer.readCloudShadow();for(float v:clearField)check(v==1,"Clear coverage retained cloud shadow energy");
    f.clouds.coverage=1;f.weather.cloudShadows=false;renderer.render(f,draws);auto fogSource=renderer.readHDR();auto fogSourceImage=renderer.readOutput();
    f.weather.fog=true;f.weather.dryExtinction=.001f;f.weather.rainExtinction=0;f.weather.height=120;renderer.render(f,draws);auto fog=renderer.readHDR();auto fogImage=renderer.readOutput();
    double fogError=0;size_t compared=0;for(size_t i=0;i<fog.size();i+=4){for(int c=0;c<4;++c)check(std::isfinite(fog[i+c]),"Weather produced nonfinite HDR");if(positions[i+3]==0)continue;
        const glm::vec3 p(positions[i],positions[i+1],positions[i+2]);const float T=std::exp(-airOpticalDepth(f.cameraPosition,p,f.seaLevelMeters,f.weather.height,f.weather.dryExtinction));
        for(int c=0;c<3;++c)fogError=std::max(fogError,double(std::abs(fog[i+c]-(fogSource[i+c]*T+f.weather.fogColour[c]*(1-T)))));++compared;}
    check(compared>1000&&fogError<.003,"GPU fog differs from analytic air-segment integration");
    auto waterFrame=f;waterFrame.clouds.enabled=false;waterFrame.weather.cloudShadows=false;
    OceanSurfaceSettings ocean;ocean.id=99;ocean.spectrum.size=16;ocean.spectrum.length=128;ocean.spectrum.amplitude=0;ocean.meshSize=17;ocean.surfaceLength=800;ocean.seaLevel=10;ocean.detailWaves=false;ocean.cameraGrid=false;ocean.absorption=ocean.scattering=glm::vec3(0);ocean.bedCaustics=false;waterFrame.oceans={ocean};
    auto waterDraws=draws;waterDraws.push_back({mesh,mat,glm::translate(glm::mat4(1),glm::vec3(0,30,0))*glm::scale(glm::mat4(1),glm::vec3(.12f,1,.12f)),2});
    waterFrame.weather.fog=false;renderer.render(waterFrame,waterDraws);auto bedOff=renderer.readWaterCapture(99);auto airOff=renderer.readWaterCapture(99,false,true);
    waterFrame.weather.fog=true;renderer.render(waterFrame,waterDraws);auto bedOn=renderer.readWaterCapture(99);auto airOn=renderer.readWaterCapture(99,false,true);auto airPositions=renderer.readWaterCapture(99,true,true);
    check(bedOff==bedOn,"Water capture duplicated the camera-to-interface air segment on submerged geometry");
    double airError=0;size_t airPixels=0;for(size_t i=0;i<airOn.size();i+=4)if(airPositions[i+3]>0){const glm::vec3 p(airPositions[i],airPositions[i+1],airPositions[i+2]);const float T=std::exp(-airOpticalDepth(waterFrame.cameraPosition,p,ocean.seaLevel,waterFrame.weather.height,waterFrame.weather.dryExtinction,ocean.seaLevel));for(int c=0;c<3;++c)airError=std::max(airError,double(std::abs(airOn[i+c]-(airOff[i+c]*T+waterFrame.weather.fogColour[c]*(1-T)))));++airPixels;}
    check(airPixels>100&&airError<.003,"Water air-layer fog differs from the clipped air segment");
    waterFrame.cameraPosition={0,5,100};waterFrame.view=glm::lookAt(waterFrame.cameraPosition,glm::vec3(0,30,0),glm::vec3(0,1,0));waterFrame.viewProjection=remap*glm::perspective(glm::radians(55.f),320.f/240,.1f,waterFrame.farPlane)*waterFrame.view;
    renderer.render(waterFrame,waterDraws);for(float v:renderer.readHDR())check(std::isfinite(v),"Underwater weather composite is nonfinite");
    // Solar transmission remains independent of the camera-centred cloud panorama.
    auto baked=bakeAtmosphere(device,f);check(baked.cloudsIncluded&&baked.cloudShadow&&baked.radiance.size()==size_t(baked.width)*baked.height,"P1 bake omitted cloud sky or spatial solar field");
    auto clearFrame=f;clearFrame.clouds.enabled=false;auto sky=bakeAtmosphere(device,clearFrame);check(glm::length(baked.sunIrradiance-sky.sunIrradiance)<1e-6,"P1 double-attenuated the independent sun emitter");
    double skyDifference=0;for(size_t i=0;i<sky.radiance.size();++i)skyDifference+=glm::length(sky.radiance[i]-baked.radiance[i]);check(skyDifference>1,"P1 cloud panorama remained clear sky");
    RenderWorldSnapshot world;world.frame=f;world.frame.clouds.enabled=false;auto payload=std::make_shared<MeshPayload>();payload->vertices=vertices;payload->indices=indices;SnapshotDraw draw;draw.mesh=payload;draw.parameters=m.parameters;world.draws={draw};
    pt::CpuScene cpu(world);cpu.cloudShadow=baked.cloudShadow;pt::Options options;options.width=16;options.height=12;options.samples=8;options.maxDepth=1;options.minimumSamples=8;options.adaptive=false;options.threads=1;
    auto reference=pt::render(cpu,options);auto gpu=pt::renderGpu(cpu,options,device);double ptError=0,ptScale=0;for(size_t i=0;i<gpu.radiance.size();++i){ptError+=glm::length(gpu.radiance[i]-reference.radiance[i]);ptScale+=glm::length(reference.radiance[i]);}
    check(ptError/std::max(ptScale,1e-6)<.006,"CPU/GPU PT spatial cloud-sun transport differs");
    // A volume occluder must reduce scattered sunlight without changing extinction.
    auto vf=f;vf.clouds.enabled=false;vf.weather.fog=false;vf.weather.aerialPerspective=true;vf.weather.aerialDistanceScale=1;vf.weather.aerialShadows=false;vf.multipleScattering=0;
    vf.shadows=true;vf.shadowSettings.distance=2000;
    auto volumeDraws=draws;volumeDraws.push_back({mesh,mat,glm::translate(glm::mat4(1),glm::vec3(0,220,0))});
    renderer.render(vf,volumeDraws);const auto litSky=renderer.readHDR();const auto litAir=renderer.readAerialPerspective(),litT=renderer.readAerialPerspective(true);
    vf.weather.aerialShadows=true;renderer.render(vf,volumeDraws);const auto shadedSky=renderer.readHDR();const auto volumePositions=renderer.readGBuffer(0);const auto shadedAir=renderer.readAerialPerspective(),shadedT=renderer.readAerialPerspective(true);
    size_t shadowedFroxels=0,shadowedSkyPixels=0;double volumeShadowReduction=0;
    for(size_t at=0;at<litSky.size();at+=4)if(volumePositions[at+3]==0&&litSky[at]-shadedSky[at]>.00001f)++shadowedSkyPixels;
    for(size_t at=0;at<litAir.size();at+=4){
        check(std::isfinite(shadedAir[at]),"Volume shadows produced nonfinite radiance");
        if(litAir[at]-shadedAir[at]>.00001f)++shadowedFroxels;
        for(int c=0;c<3;++c){check(shadedAir[at+c]<=litAir[at+c]+.00001f,"Volume shadow added direct solar energy");volumeShadowReduction+=litAir[at+c]-shadedAir[at+c];}
    }
    check(shadowedSkyPixels>10,"Local sun occlusion did not reach atmospheric sky");
    check(litT==shadedT&&shadowedFroxels>100&&volumeShadowReduction>.01,"Volume shadows failed radiance/extinction separation");
    // Aerial perspective has its own switch and must leave sky pixels unchanged.
    auto af=f;af.clouds.enabled=false;af.weather.fog=false;af.weather.aerialPerspective=false;af.multipleScattering=0;af.weather.aerialShadows=false;af.weather.aerialDistanceScale=1;
    renderer.render(af,draws);const auto aerialOff=renderer.readHDR();auto aerialOffImage=renderer.readOutput();const auto aerialPositions=renderer.readGBuffer(0);
    af.weather.aerialPerspective=true;renderer.render(af,draws);const auto aerialOn=renderer.readHDR();auto aerialOnImage=renderer.readOutput();
    const auto aerialL=renderer.readAerialPerspective(),aerialT=renderer.readAerialPerspective(true);
    SunState testSun;testSun.direction={0,1,0};testSun.irradiance={1,1,1};testSun.multipleScattering=0;
    glm::dmat4 translation(1);translation[3]=glm::dvec4(af.cameraPosition,1);const auto inverse=glm::mat4(glm::inverse(glm::dmat4(af.viewProjection)*translation));
    double aerialError=0,aerialScale=0,aerialTransmissionError=0;
    for(int slice:{4,12,24,31})for(int y:{4,12,20})for(int x:{8,16,24}){
        const glm::vec2 uv((x+.5f)/32,(y+.5f)/32);auto point=inverse*glm::vec4(uv*glm::vec2(2,-2)+glm::vec2(-1,1),.999999f,1);auto direction=glm::normalize(glm::vec3(point)/point.w);
        auto reference=aerialReference(af,testSun,af.cameraPosition,direction,double(slice)/31*slice/31*af.weather.aerialMaxDistance);
        const size_t at=(size_t(slice*32+y)*32+x)*4;
        for(int c=0;c<3;++c){aerialError+=std::abs(aerialL[at+c]-reference.L[c]);aerialScale+=std::abs(reference.L[c]);aerialTransmissionError=std::max(aerialTransmissionError,std::abs(aerialT[at+c]-reference.T[c]));}
    }
    check(aerialError/std::max(aerialScale,1e-9)<.03&&aerialTransmissionError<.003,"Aerial perspective LUT differs from independent atmosphere quadrature");
    size_t aerialPixels=0,skyPixels=0;for(size_t at=0;at<aerialOn.size();at+=4){for(int c=0;c<3;++c)check(std::isfinite(aerialOn[at+c]),"Aerial perspective produced nonfinite HDR");
        if(aerialPositions[at+3]==0){for(int c=0;c<3;++c)check(aerialOn[at+c]==aerialOff[at+c],"Aerial perspective double integrated the sky");++skyPixels;}
        else if(glm::length(glm::vec3(aerialOn[at]-aerialOff[at],aerialOn[at+1]-aerialOff[at+1],aerialOn[at+2]-aerialOff[at+2]))>.0001f)++aerialPixels;}
    check(aerialPixels>500&&skyPixels>1000,"Aerial perspective fixture lost sky or distant geometry");
    renderer.render(af,draws);check(renderer.readHDR()==aerialOn,"Paused aerial perspective changed");
    af.weather.aerialDistanceScale=0;renderer.render(af,draws);check(renderer.readHDR()==aerialOff,"Zero aerial distance scale did not disable transport");
    af.weather.aerialDistanceScale=1;af.weather.aerialStartDistance=30000;renderer.render(af,draws);check(renderer.readHDR()==aerialOff,"Aerial start depth leaked scattering onto near geometry");
    af.weather.aerialStartDistance=100;af.weather.fog=true;renderer.render(af,draws);auto bothFog=renderer.readHDR();double combinedFogError=0;
    for(size_t at=0;at<bothFog.size();at+=4)if(aerialPositions[at+3]>0){auto p=glm::vec3(aerialPositions[at],aerialPositions[at+1],aerialPositions[at+2]);float T=std::exp(-airOpticalDepth(af.cameraPosition,p,af.seaLevelMeters,af.weather.height,af.weather.dryExtinction));for(int c=0;c<3;++c)combinedFogError=std::max(combinedFogError,double(std::abs(bothFog[at+c]-(aerialOn[at+c]*T+af.weather.fogColour[c]*(1-T)))));}
    check(combinedFogError<.003,"Aerial perspective and height fog composition duplicated an air segment");
    auto sunset=af;sunset.weather.fog=false;sunset.sunAngle=5;sunset.lights[0].directionOuter={0,-std::sin(glm::radians(5.f)),std::cos(glm::radians(5.f)),0};
    renderer.render(sunset,draws);const auto sunsetL=renderer.readAerialPerspective();double sunsetDifference=0;for(size_t at=0;at<sunsetL.size();at+=4)for(int c=0;c<3;++c){check(std::isfinite(sunsetL[at+c]),"Sunset aerial scattering is nonfinite");sunsetDifference+=std::abs(sunsetL[at+c]-aerialL[at+c]);}
    check(sunsetDifference>1,"Aerial perspective did not respond to sun direction");
    auto vacuum=af;vacuum.weather.fog=false;vacuum.weather.aerialPerspective=false;vacuum.atmosphere.rayleigh=vacuum.atmosphere.mie=vacuum.atmosphere.extinction=glm::vec4(0);vacuum.atmosphere.absorption=glm::vec4(0,0,0,15);
    renderer.render(vacuum,draws);const auto vacuumOff=renderer.readHDR();vacuum.weather.aerialPerspective=true;renderer.render(vacuum,draws);
    check(renderer.readHDR()==vacuumOff,"Zero atmospheric coefficients produced aerial fog");
    auto waf=waterFrame;waf.weather.fog=false;waf.weather.aerialPerspective=false;waf.multipleScattering=0;waf.weather.aerialShadows=false;waf.weather.aerialDistanceScale=1;waf.weather.aerialStartDistance=0;
    renderer.render(waf,waterDraws);auto aerialBedOff=renderer.readWaterCapture(99),aerialAirOff=renderer.readWaterCapture(99,false,true);
    waf.weather.aerialPerspective=true;renderer.render(waf,waterDraws);auto aerialBedOn=renderer.readWaterCapture(99),aerialAirOn=renderer.readWaterCapture(99,false,true),airWorld=renderer.readWaterCapture(99,true,true);
    check(aerialBedOn==aerialBedOff,"Aerial perspective integrated submerged capture geometry");
    double waterAerialError=0;size_t waterAerialPixels=0;
    for(size_t at=0;at<aerialAirOn.size();at+=4)if(airWorld[at+3]>0&&at/4%320%32==0){auto p=glm::vec3(airWorld[at],airWorld[at+1],airWorld[at+2]);auto delta=p-waf.cameraPosition;auto ref=aerialReference(waf,testSun,waf.cameraPosition,glm::normalize(delta),glm::length(delta),ocean.seaLevel);for(int c=0;c<3;++c)waterAerialError=std::max(waterAerialError,std::abs(aerialAirOn[at+c]-(aerialAirOff[at+c]*ref.T[c]+ref.L[c])));++waterAerialPixels;}
    check(waterAerialPixels>10&&waterAerialError<.003,"Water air atmosphere differs from clipped reference quadrature");
    nlohmann::json report={{"aerial_quadrature_relative_l1",aerialError/std::max(aerialScale,1e-9)},{"aerial_transmission_max_absolute_error",aerialTransmissionError},{"aerial_sunset_scattering_difference_l1",sunsetDifference},{"ground_cloud_strength_max_error",cloudStrengthError},{"aerial_shadowed_sky_pixels",shadowedSkyPixels},{"aerial_shadowed_froxels",shadowedFroxels},{"aerial_shadow_radiance_reduction_l1",volumeShadowReduction},{"aerial_geometry_pixels",aerialPixels},{"aerial_sky_unchanged_pixels",skyPixels},{"aerial_height_fog_max_absolute_error",combinedFogError},{"water_aerial_max_absolute_error",waterAerialError},{"water_aerial_reference_pixels",waterAerialPixels},{"backend",device->backend()==rhi::Backend::Metal?"Metal":"Vulkan"},{"darkened_ground_pixels",darker},{"cloud_shadow_min",*range.first},{"cloud_shadow_max",*range.second},{"shadow_beer_max_absolute_error",shadowTauError},{"fog_compared_pixels",compared},{"fog_max_absolute_error",fogError},{"water_air_compared_pixels",airPixels},{"water_air_max_absolute_error",airError},{"pt_relative_l1",ptError/std::max(ptScale,1e-6)},{"panorama_difference_l1",skyDifference},{"scope","P1 approximate: fixed cloud panorama and below-cloud solar field; fog/precipitation media excluded from PT"}};
    if(const char* output=std::getenv("SCENERENDERER_WEATHER_VALIDATION_DIR")){std::filesystem::create_directories(output);ppm(std::filesystem::path(output)/"aerial-off.ppm",aerialOffImage);ppm(std::filesystem::path(output)/"aerial-on.ppm",aerialOnImage);ppm(std::filesystem::path(output)/"cloud-shadow-off.ppm",unshadowedImage);ppm(std::filesystem::path(output)/"cloud-shadow-on.ppm",shadowImage);ppm(std::filesystem::path(output)/"fog-off.ppm",fogSourceImage);ppm(std::filesystem::path(output)/"fog-on.ppm",fogImage);std::vector<float> hdr;std::vector<uint8_t> panorama;for(auto v:baked.radiance){for(int c=0;c<3;++c){hdr.push_back(v[c]);panorama.push_back(uint8_t(std::lround(std::pow(std::max(v[c],0.f)/(1+std::max(v[c],0.f)),1/2.2f)*255)));}panorama.push_back(255);}
        auto skyPath=(std::filesystem::path(output)/"cloud-panorama.hdr").string();if(!stbi_write_hdr(skyPath.c_str(),baked.width,baked.height,3,hdr.data()))throw std::runtime_error("Cannot save P1 sky HDR");
        auto panoramaPath=(std::filesystem::path(output)/"cloud-panorama.png").string();if(!stbi_write_png(panoramaPath.c_str(),baked.width,baked.height,4,panorama.data(),baked.width*4))throw std::runtime_error("Cannot save P1 sky preview");
        std::ofstream(std::filesystem::path(output)/"cloud-solar-field.json")<<serializeCloudShadow(*baked.cloudShadow)<<'\n';
        std::ofstream(std::filesystem::path(output)/"validation.json")<<report.dump(2)<<'\n';}
    std::cout<<report.dump(2)<<"\nWeather lighting: cloud shadows, clear endpoint, paused/cache interval, analytic air fog, atmospheric aerial perspective and CPU/GPU P1 passed\n";
}
}
