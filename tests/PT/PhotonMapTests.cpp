#include "PT/CpuPathTracer.h"
#include "PT/PhotonMap.h"
#include "PT/ValidationScenes.h"
#include "PT/SceneIdentity.h"
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
    glm::dvec3 uniformMean(0);for(int x=-1;x<=1;++x)for(int z=-1;z<=1;++z){pt::Surface floor;floor.position={x*.6f,0,z*.6f};floor.normal=floor.geometricNormal={0,1,0};floor.bsdfModel=1;floor.albedo=glm::vec3(1);auto gather=map.gather(floor,{0,1,0},2,.2f);uniformMean+=gather.flux/(double(pi)*.2*.2*map.paths);check(map.gather(floor,{0,1,0},1,.2f).count==0,"SPPM gather exceeded depth budget");}
    check(glm::all(glm::lessThan(glm::abs(glm::vec3(uniformMean/9.0)/expected-glm::vec3(1)),glm::vec3(.07f))),"Uniform SPPM disk disagrees with analytical solar Fresnel/Beer irradiance");
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
void progressivePhotons(){
    // Small pinhole field inside a flat pool: independent analytical irradiance
    // fixes the scale, including camera absorption and BSDF (no photon reference).
    auto validation=pt::makePoolCausticsScene(1,1,false);auto &frame=validation.snapshot.frame;
    frame.cameraPosition={.3f,.6f,.3f};glm::mat4 conversion(1);conversion[2][2]=.5f;conversion[3][2]=.5f;
    frame.viewProjection=conversion*glm::perspective(glm::radians(1.f),1.f,.01f,100.f)*glm::lookAt(frame.cameraPosition,glm::vec3(.3f,0,.3f),glm::vec3(0,0,-1));
    pt::CpuScene scene(validation.snapshot);scene.sunDirection=glm::normalize(glm::vec3(-.25f,1,.3f));scene.sunIrradiance=glm::vec3(4);scene.sunRadius=.01f;
    pt::Options o;o.width=o.height=1;o.samples=64;o.maxDepth=2;o.threads=2;o.adaptive=false;o.sppm=true;o.photonPaths=6000;o.photonRadius=.3f;
    auto image=pt::render(scene,o);const float c=scene.sunDirection.y,cw=std::sqrt(1-(1-c*c)/(1.333f*1.333f));
    auto irradiance=glm::vec3(4*c*(1-pt::dielectricFresnel(c,1,1.333f)))*glm::exp(-glm::vec3(.08f,.025f,.012f)*(1.15f/cw));
    glm::vec3 expected(0);auto incoming=-glm::refract(-scene.sunDirection,{0,1,0},1/1.333f);
    for(uint32_t iteration=0;iteration<o.samples;++iteration){auto rng=pt::Random::forPixel(o.seed,0,iteration,o.sobol);glm::vec3 origin,d;const float u=rng.uniform(),v=rng.uniform();scene.cameraRay(u,v,origin,d);pt::VisiblePoint point;uint64_t rays=0;scene.trace(origin,d,rng,o.maxDepth,rays,nullptr,true,true,nullptr,true,nullptr,&point);check(point.valid,"SPPM lost an underwater camera endpoint");expected+=point.beta*pt::evaluateBsdf(point.surface,point.view,incoming)*irradiance;}
    expected/=float(o.samples);std::cout<<"SPPM flat pool analytical ratio: "<<image.radiance[0].x/expected.x<<'\n';check(glm::all(glm::lessThan(glm::abs(image.radiance[0]/expected-glm::vec3(1)),glm::vec3(.08f))),"SPPM progressive radius/flux/path normalization changed analytical flat pool energy");
    check(image.progressiveRadiusMean<o.photonRadius*.7f&&image.photonMerges>100&&image.emittedPhotonPaths==uint64_t(o.samples)*o.photonPaths,"SPPM did not shrink independently sampled photon batches");
    check(near(image.radiance[0],image.caustics[0]),"SPPM direct/indirect split double counted flat pool caustics");
    // At one iteration the radius/flux factors cancel: changing alpha cannot
    // change the rendered energy. This catches a mismatched area normalization.
    auto one=o;one.samples=1;one.sppmAlpha=.2f;auto lowAlpha=pt::render(scene,one);one.sppmAlpha=.9f;auto highAlpha=pt::render(scene,one);
    check(near(lowAlpha.radiance[0],highAlpha.radiance[0],1e-5f),"SPPM first-batch energy depends on alpha");
    auto first=pt::PhotonMap(scene,o,0,false),second=pt::PhotonMap(scene,o,1,false);check(first.photons().size()!=second.photons().size()||std::memcmp(first.photons().data(),second.photons().data(),first.photons().size()*sizeof(pt::Photon))!=0,"SPPM reused identical photons each iteration");
    o.width=8;o.height=8;o.samples=8;o.photonPaths=2000;o.threads=1;auto serial=pt::render(scene,o);o.threads=3;auto parallel=pt::render(scene,o);
    check(serial.radiance==parallel.radiance&&serial.caustics==parallel.caustics&&serial.photonMerges==parallel.photonMerges&&serial.rays==parallel.rays,"SPPM depends on worker scheduling");
    auto changed=o;changed.sppmAlpha=.8f;check(!pt::sameRenderOptions(o,changed),"SPPM alpha change retained an incompatible film");changed=o;changed.sppm=false;check(!pt::sameRenderOptions(o,changed),"SPPM mode change retained an incompatible film");
    for(int flag=0;flag<5;++flag){auto invalid=o;if(flag==0)invalid.adaptive=true;if(flag==1)invalid.photonMapping=true;if(flag==2)invalid.bdpt=true;if(flag==3)invalid.sppmAlpha=1;if(flag==4)invalid.sppmAlpha=std::numeric_limits<float>::quiet_NaN();bool rejected=false;try{pt::render(scene,invalid);}catch(const std::invalid_argument &){rejected=true;}check(rejected,"SPPM accepted incompatible options");}
    auto huge=o;huge.width=huge.height=4096;bool rejected=false;try{pt::render(scene,huge);}catch(const std::length_error &){rejected=true;}check(rejected,"SPPM allocated unbounded pixel state");
    auto glassFixture=pt::makeCausticsScene(8,8);pt::CpuScene glass(glassFixture.snapshot,glassFixture.dielectrics);auto glassOptions=o;glassOptions.maxDepth=5;glassOptions.photonPaths=6000;auto glassImage=pt::render(glass,glassOptions);double caustic=0;for(size_t i=0;i<glassImage.caustics.size();++i){caustic+=glassImage.caustics[i].x;check(glm::all(glm::lessThanEqual(glassImage.caustics[i],glassImage.radiance[i]+glm::vec3(1e-5f))),"SPPM caustics exceed full area-light image");}check(caustic>0,"SPPM area emitter glass caustics are empty");
    // No return path for indirect light: HDR NEE at the endpoint has weight 1.
    auto world=snapshot();world.draws.push_back(triangle({-100,-100,0},{100,-100,0},{0,100,0},{.8f,.8f,.8f}));pt::CpuScene sky(world);sky.environment=std::make_shared<pt::Environment>(8,4,std::vector<glm::vec3>(32,glm::vec3(1)));
    o.samples=512;o.photonPaths=1;o.maxDepth=2;o.sppm=false;auto reference=pt::render(sky,o);o.sppm=true;auto progressive=pt::render(sky,o);double a=0,b=0;for(size_t i=0;i<reference.radiance.size();++i){a+=reference.radiance[i].x;b+=progressive.radiance[i].x;}check(std::abs(b/a-1)<.025&&progressive.photonMerges==0,"SPPM HDR NEE loses energy or double counts continuation");
}
}
int main(){try{photonMappingAndAnyHit();progressivePhotons();std::cout<<"Photon mapping, SPPM and shadow traversal tests passed\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
