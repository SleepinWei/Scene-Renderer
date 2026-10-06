#include "PT/CpuPathTracer.h"
#include "PT/PhotonMap.h"
#include "PT/ValidationScenes.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>
namespace {
constexpr float pi=3.14159265358979323846f;
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
bool near(glm::vec3 a,glm::vec3 b,float tolerance=1e-4f){return glm::length(a-b)<tolerance;}
render::SnapshotDraw triangle(glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 color={1,1,1}) {
    render::SnapshotDraw draw;auto mesh=std::make_shared<render::MeshPayload>();const auto n=glm::normalize(glm::cross(b-a,c-a));
    mesh->vertices={{a,n,{.25f,.25f}},{b,n,{.25f,.25f}},{c,n,{.25f,.25f}}};mesh->indices={0,1,2};draw.mesh=mesh;draw.parameters.factors={0,.8f,1,0};draw.parameters.albedoAlpha=glm::vec4(color,1);return draw;
}
render::RenderWorldSnapshot snapshot(){render::RenderWorldSnapshot s;s.frame.cameraPosition={0,0,2};glm::mat4 conversion(1);conversion[2][2]=.5f;conversion[3][2]=.5f;s.frame.viewProjection=conversion*glm::perspective(glm::radians(50.f),1.f,.1f,100.f)*glm::lookAt(s.frame.cameraPosition,glm::vec3(0),glm::vec3(0,1,0));return s;}
void photonMappingAndAnyHit(){
    auto validation=pt::makePoolCausticsScene(24,18,false);pt::CpuScene scene(validation.snapshot);scene.sunDirection=glm::normalize(glm::vec3(-.25f,1,.3f));scene.sunIrradiance=glm::vec3(4);scene.sunRadius=.01f;
    pt::Options options;options.width=24;options.height=18;options.samples=16;options.maxDepth=2;options.threads=2;options.adaptive=false;options.photonMapping=true;options.photonPaths=400000;options.photonRadius=.25f;
    pt::PhotonMap map(scene,options);check(map.causticCount>1000,"Flat water photon emission did not reach the pool floor");
    // Independent flat-interface irradiance: projected sun flux, Fresnel loss,
    // Beer absorption, and Lambert 1/pi. No photon density code in the reference.
    float cosine=scene.sunDirection.y,cosineWater=std::sqrt(1-(1-cosine*cosine)/(1.333f*1.333f));
    glm::vec3 expected=glm::vec3(4*cosine*(1-pt::dielectricFresnel(cosine,1,1.333f))/pi)*glm::exp(-glm::vec3(.08f,.025f,.012f)*(1.15f/cosineWater));
    glm::vec3 mean(0);for(int x=-1;x<=1;++x)for(int z=-1;z<=1;++z){pt::Surface floor;floor.position={x*.6f,0,z*.6f};floor.normal=floor.geometricNormal={0,1,0};floor.bsdfModel=1;floor.albedo=glm::vec3(1);auto estimate=map.estimate(floor,{0,1,0},2);mean+=estimate.radiance;check(near(estimate.radiance,estimate.caustics),"Flat pool density contains non-caustic light at depth two");check(near(map.estimate(floor,{0,1,0},1).radiance,glm::vec3(0)),"Photon gathering exceeded the camera+light depth budget");}mean/=9.f;
    std::cout<<"Flat pool photon irradiance ratio: "<<mean.x/expected.x<<", "<<mean.y/expected.y<<", "<<mean.z/expected.z<<'\n';check(glm::all(glm::lessThan(glm::abs(mean/expected-glm::vec3(1)),glm::vec3(.07f))),"Photon launch/PDF/eta/disk normalization differs from analytical flat water");
    auto deterministic=options;deterministic.photonPaths=4000;deterministic.threads=1;pt::PhotonMap serial(scene,deterministic);deterministic.threads=3;pt::PhotonMap parallel(scene,deterministic);check(serial.photons().size()==parallel.photons().size()&&serial.rays==parallel.rays&&std::memcmp(serial.photons().data(),parallel.photons().data(),serial.photons().size()*sizeof(pt::Photon))==0,"Parallel photon generation changed deterministic light paths or accumulation order");
    auto noWater=pt::makePoolCausticsScene(24,18,false,false);pt::CpuScene dry(noWater.snapshot);dry.sunDirection=scene.sunDirection;dry.sunIrradiance=scene.sunIrradiance;dry.sunRadius=scene.sunRadius;options.photonPaths=20000;pt::PhotonMap dryMap(dry,options);check(dryMap.causticCount==0,"No-water photon map falsely classified caustics");
    options.maxDepth=4;auto pool=pt::render(scene,options);check(pool.nonFiniteSamples==0&&pool.causticPhotons>0,"Pool photon image has no finite caustics");double energy=0;for(size_t i=0;i<pool.caustics.size();++i){energy+=pool.caustics[i].x;check(glm::all(glm::lessThanEqual(pool.caustics[i],pool.radiance[i]+glm::vec3(1e-5f))),"Photon caustics exceed the complete image");}check(energy>1,"Pool caustics AOV is empty");
    options.photonMapping=false;options.maxDepth=4;auto fast=pt::render(scene,options);options.shadowAnyHit=false;auto slow=pt::render(scene,options);check(fast.radiance==slow.radiance&&fast.rays==slow.rays,"Any-hit shadows changed PT transport");
    pt::Random rng(701);for(int i=0;i<256;++i){glm::vec3 o{rng.uniform()*12-6,rng.uniform()*6-1,rng.uniform()*12-6},d=glm::normalize(glm::vec3(rng.uniform()-.5f,rng.uniform()-.5f,rng.uniform()-.5f));float maximum=rng.uniform()*10;pt::Surface hit;check(scene.intersectsAny(o,d,1e-4f,maximum)==scene.intersect(o,d,1e-4f,maximum,hit,true),"Any-hit disagrees with brute force for bounded shadow rays");}
    auto areaSource=pt::makeCausticsScene(8,8);pt::CpuScene area(areaSource.snapshot,areaSource.dielectrics);auto areaOptions=options;areaOptions.photonMapping=true;areaOptions.photonPaths=30000;areaOptions.maxDepth=5;pt::PhotonMap areaMap(area,areaOptions);check(areaMap.causticCount>100,"Area-light importance photon paths produced no glass caustics");
    auto skyWorld=snapshot();skyWorld.draws.push_back(triangle({-100,-100,0},{100,-100,0},{0,100,0},{.8f,.8f,.8f}));pt::CpuScene sky(skyWorld);sky.environment=std::make_shared<pt::Environment>(8,4,std::vector<glm::vec3>(32,glm::vec3(1)));auto skyOptions=options;skyOptions.width=8;skyOptions.height=8;skyOptions.samples=1024;skyOptions.maxDepth=2;skyOptions.photonPaths=2000;skyOptions.photonMapping=false;auto skyPt=pt::render(sky,skyOptions);skyOptions.photonMapping=true;auto skyPhoton=pt::render(sky,skyOptions);double original=0,replaced=0;for(size_t i=0;i<skyPt.radiance.size();++i){original+=skyPt.radiance[i].x;replaced+=skyPhoton.radiance[i].x;}check(std::abs(replaced/original-1)<.025,"Photon merge direct HDR lighting loses energy or double counts BSDF hits");
    auto glowingWorld=snapshot();auto lower=triangle({-10,-10,0},{10,-10,0},{0,10,0});auto upper=triangle({-10,-10,2},{0,10,2},{10,-10,2});lower.parameters.emissiveNormal=upper.parameters.emissiveNormal={1,1,1,0};glowingWorld.draws={lower,upper};pt::CpuScene glowing(glowingWorld);auto glowingOptions=options;glowingOptions.photonMapping=true;glowingOptions.photonPaths=4000;glowingOptions.maxDepth=3;pt::PhotonMap glowingMap(glowing,glowingOptions);check(glowingMap.photons().size()>100,"Emissive surfaces lost reflected indirect photons");
    auto volume=pt::makeMediumValidationScene(8,8,true);pt::CpuScene scattering(volume.snapshot);bool rejected=false;try{pt::PhotonMap invalid(scattering,options);}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Photon map silently dropped volume scattering");
    auto cutoutWorld=snapshot();auto hole=triangle({-2,-2,0},{2,-2,0},{0,2,0});hole.parameters.albedoAlpha.a=0;hole.parameters.factors.w=.5f;cutoutWorld.draws.push_back(hole);pt::CpuScene cutout(cutoutWorld);check(!cutout.intersectsAny({0,0,1},{0,0,-1},1e-4f,2),"Constant alpha-cutout without a texture blocked any-hit shadows");
    auto mixedWorld=snapshot();auto tinted=triangle({-100,-100,0},{100,-100,0},{0,100,0});tinted.pathTracingKind=5;tinted.pathTracingIor=1.5f;tinted.parameters.albedoAlpha={.8f,.9f,1,1};mixedWorld.draws.push_back(tinted);auto blocker=triangle({-1,-1,-1},{1,-1,-1},{0,1,-1});mixedWorld.draws.push_back(blocker);pt::CpuScene mixed(mixedWorld);pt::Surface transparent;bool opaque=false;check(mixed.intersectShadow({0,0,1},{0,0,-1},1e-4f,3,transparent,opaque)&&opaque,"Opaque blocker behind thin glass did not terminate mixed shadows");check(mixed.intersectShadow({5,0,1},{0,0,-1},1e-4f,3,transparent,opaque)&&!opaque&&transparent.thinDielectric,"Unblocked thin glass was lost by mixed shadow traversal");
    mixedWorld.frame.lights.push_back({{0,0,-3,1},{2,3,4,0},{0,0,1,0}});auto receiver=triangle({-100,-100,2},{100,-100,2},{0,100,2});mixedWorld.draws.push_back(receiver);pt::CpuScene mixedLit(mixedWorld);glm::vec3 mixedA(0),mixedB(0);for(uint32_t i=0;i<128;++i){auto r1=pt::Random::forPixel(1,0,i,true),r2=r1;uint64_t ra=0,rb=0;mixedA+=mixedLit.trace({5,0,1},{0,0,1},r1,2,ra,nullptr,true,true,nullptr,true);mixedB+=mixedLit.trace({5,0,1},{0,0,1},r2,2,rb,nullptr,true,true,nullptr,false);}check(mixedA.r>0&&mixedA==mixedB,"Mixed alpha/thin/opaque shadow traversal changed transport");
    auto sheet=pt::makeThinSolarValidationScene(8,8);pt::CpuScene thin(sheet.snapshot);check(!thin.opaqueShadows(),"Thin-sheet scene enabled opaque any-hit shadows");
    options.photonMapping=true;options.adaptive=true;rejected=false;try{pt::validateOptions(options);}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Photon bias was hidden by adaptive variance stopping");
}
}
int main(){try{photonMappingAndAnyHit();std::cout<<"Photon mapping and shadow traversal tests passed\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
