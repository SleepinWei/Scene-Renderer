#include "PT/CpuPathTracer.h"
#include "PT/ValidationScenes.h"
#include "PT/SceneIdentity.h"
#include "../../src/PT/FiberScattering.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstring>
#include <future>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <chrono>
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
void thinDielectric() {
    check(std::abs(pt::thinDielectricReflectance(1,1,1.5f)-1.f/13)<1e-6,"Parallel-sheet normal-incidence reflection differs from independent plate sum");
    check(pt::thinDielectricReflectance(.001f,1,1.5f)>.99f,"Thin sheet lost grazing reflection");
    check(pt::thinDielectricReflectance(.2f,1.5f,1.1f)==1,"Sheet in higher-IOR exterior failed TIR");
    pt::Surface sheet;sheet.thinDielectric=true;sheet.ior=1.5f;sheet.normal=sheet.geometricNormal={0,0,1};sheet.albedo={.4f,.7f,1};pt::Random random(115);
    for(bool front:{true,false}){sheet.frontFace=front;int reflected=0;glm::vec3 energy(0);
        for(int i=0;i<32768;++i){auto sample=pt::sampleBsdf(sheet,{0,0,1},random);check(sample.delta&&sample.pdf>0,"Sheet event is not discrete");if(sample.transmission)check(near(sample.direction,{0,0,-1}),"Parallel-sheet transmission bent or eta-scaled the ray");else ++reflected;energy+=sample.value*(std::abs(sample.direction.z)/sample.pdf);}
        check(std::abs(float(reflected)/32768-1.f/13)<.005,"Sheet branch frequencies changed");check(near(energy/32768.f,glm::vec3(1.f/13)+sheet.albedo*(12.f/13),.009f),"Tinted thin-sheet energy exceeds expected R+T*tint");}
    auto room=snapshot();room.draws.push_back(triangle({-1000,-1000,0},{1000,-1000,0},{0,1000,0}));room.frame.lights.push_back({{0,0,4,1},{20,20,20,0},{0,0,-1,0}});
    auto pane=triangle({-1000,-1000,3},{1000,-1000,3},{0,1000,3});pane.pathTracingKind=5;pane.pathTracingIor=1.5f;pane.parameters.albedoAlpha={.8f,.9f,1,1};pt::CpuScene baseline(room);room.draws.push_back(pane);pt::CpuScene glass(room);
    check(glass.media().empty()&&glass.initialMedia({0,0,2})[0]==0,"Thin sheet entered the volume stack");pt::Surface hit;check(glass.intersect({0,0,2},{0,0,1},0,10,hit)&&hit.thinDielectric&&hit.mediumId==0,"Thin surface identity lost");check(glass.exportData().materials.back().extra.w==1,"Packed thin-sheet ABI flag lost");
    uint64_t rays=0;pt::Random a(7),b(7);auto expected=glm::pow(glm::vec3(.8f,.9f,1),glm::vec3(2.2f))*(12.f/13);auto ratio=glass.trace({0,0,2},{0,0,-1},a,1,rays)/baseline.trace({0,0,2},{0,0,-1},b,1,rays);check(near(ratio,expected,1e-5f),"Thin-sheet shadow Fresnel/tint differs from path transmission");
    room.frame.lights.clear();room.draws.back().parameters.albedoAlpha=glm::vec4(1);room.draws[0].parameters.factors.y=.5f;pt::CpuScene furnace(room);furnace.environment=std::make_shared<pt::Environment>(8,4,std::vector<glm::vec3>(32,glm::vec3(1)));glm::vec3 total(0);
    for(uint32_t i=0;i<32768;++i){auto r=pt::Random::forPixel(1,0,i,true);total+=furnace.trace({0,0,2},{0,0,-1},r,32,rays);}std::cout<<"Thin white furnace "<<total.x/32768<<", "<<total.y/32768<<", "<<total.z/32768<<'\n';check(near(total/32768.f,glm::vec3(1),.035f),"Straight-through thin sheet double-counts environment NEE/MIS");
    bool rejected=false;try{glass.validateBidirectional();}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Unsupported thin-sheet BDPT accepted");
}
void samplers() {
    for(uint32_t dimension:{0u,1u,10u,511u,32767u}){std::vector<int> bins(1024,0);for(uint32_t i=0;i<1024;++i){float value=pt::sobolSample(738,i,dimension);check(value>=0 && value<1,"Sobol sample out of range");++bins[uint32_t(value*1024)];}for(auto count:bins)check(count==1,"Sobol lost one-dimensional stratification");}
    std::vector<int> cells(1024,0);for(uint32_t i=0;i<1024;++i){uint32_t x=uint32_t(pt::sobolSample(79,i,0)*32),y=uint32_t(pt::sobolSample(79,i,1)*32);++cells[y*32+x];}for(auto count:cells)check(count==1,"Sobol 2D net missed a stratum");
    auto a=pt::Random::forPixel(31,19,5,true),b=pt::Random::forPixel(31,19,5,true);a.dimension(259);b.dimension(259);check(a.uniform()==b.uniform(),"Sobol random access differs across clones");
    // A digital shift alone locks distant bounce coordinates together: this
    // integral becomes 0 or .5 instead of .25, regardless of sample count.
    for(uint32_t seed:{79u,738u,31u})for(uint32_t d:{258u,514u,24578u}){
        double product=0;uint32_t quadrant=0;constexpr uint32_t count=16384;
        for(uint32_t i=0;i<count;++i){float x=pt::sobolSample(seed,i,2),y=pt::sobolSample(seed,i,d);product+=x*y;quadrant+=x<.5f&&y<.5f;}
        check(std::abs(product/count-.25)<.008,"Sobol cross-bounce product integral is correlated");
        check(std::abs(double(quadrant)/count-.25)<.012,"Sobol cross-bounce quadrant integral is correlated");
    }
    for(uint32_t seed:{79u,738u,31u}){
        uint32_t both=0;constexpr uint32_t count=16384;
        for(uint32_t i=0;i<count;++i)both+=pt::sobolSample(seed,i,88)<.25f&&pt::sobolSample(seed,i,192)<.5f;
        check(std::abs(double(both)/count-.125)<.012,"Solar proposal selector is correlated with late alpha coverage");
    }
}
void sceneReuse() {
    auto world=snapshot();world.draws={triangle({-1,-1,0},{1,-1,0},{0,1,0})};
    auto cache=std::make_shared<pt::GeometryCache>();pt::CpuScene original(world,{},cache);
    check(original.accelerationStats().blasBuilds==1&&cache->retainedBytes()>0,"Cold BLAS cache did not retain geometry");
    auto changed=world;changed.draws[0].model=glm::translate(glm::mat4(1),glm::vec3(.2f,0,0));changed.draws[0].parameters.albedoAlpha={.1f,.8f,.2f,1};
    changed.frame.lights.push_back({{0,0,2,1},{20,20,20,0},{0,0,-1,0}});
    pt::CpuScene reused(changed,{},cache),fresh(changed);
    check(reused.accelerationStats().blasBuilds==0&&reused.accelerationStats().blasCacheHits==1,"Transform/material/light edit rebuilt mesh BLAS");
    pt::Options opts;opts.width=8;opts.height=8;opts.samples=4;opts.maxDepth=3;opts.adaptive=false;opts.threads=1;
    check(pt::render(reused,opts).radiance==pt::render(fresh,opts).radiance,"Cached BLAS changed edited-scene transport");
    auto replacement=std::make_shared<render::MeshPayload>(*world.draws[0].mesh);replacement->revision=1;replacement->vertices[2].position.y=2;changed.draws[0].mesh=replacement;
    pt::CpuScene rebuilt(changed,{},cache);check(rebuilt.accelerationStats().blasBuilds==1,"New geometry reused a stale BLAS");
    auto tiny=std::make_shared<pt::GeometryCache>(1);pt::CpuScene uncached(world,{},tiny);check(tiny->retainedBytes()==0,"Oversized geometry exceeded cache budget");
    auto bounded=std::make_shared<pt::GeometryCache>(cache->retainedBytes()/2);pt::CpuScene lease(world,{},bounded);pt::CpuScene eviction(changed,{},bounded);
    check(bounded->retainedBytes()<=cache->retainedBytes()/2,"BLAS retention exceeded budget");pt::Surface hit;check(lease.intersect({0,0,2},{0,0,-1},0,10,hit),"Cache eviction invalidated a live CPU scene");
    auto water=pt::makeWaterSolarValidationScene(8,8).snapshot;pt::CpuScene moving(water,{},cache);
    water.frame.cameraPosition.y=-1;glm::mat4 depth(1);depth[2][2]=.5f;depth[3][2]=.5f;
    water.frame.viewProjection=depth*glm::perspective(glm::radians(60.f),1.f,.1f,100.f)*glm::lookAt(water.frame.cameraPosition,water.frame.cameraPosition+glm::vec3(0,0,-1),glm::vec3(0,1,0));
    moving.updateCamera(water.frame);pt::CpuScene cameraReference(water);
    check(moving.cameraData().media==cameraReference.cameraData().media&&moving.cameraData().winding==cameraReference.cameraData().winding&&moving.cameraData().media[0].x!=0,"Camera update lost underwater medium state");
    for(auto uv:{glm::vec2(.5f),glm::vec2(.1f,.8f)}){glm::vec3 oa,da,ob,db;moving.cameraRay(uv.x,uv.y,oa,da);cameraReference.cameraRay(uv.x,uv.y,ob,db);check(near(oa,ob)&&near(da,db)&&std::abs(moving.cameraPdf(da)-cameraReference.cameraPdf(db))<1e-6f,"Updated camera rays/PDF differ from a fresh camera");glm::vec2 pa,pb;float fa,fb;check(moving.project(oa+da*2.f,pa,fa)==cameraReference.project(ob+db*2.f,pb,fb)&&glm::length(pa-pb)<1e-6f&&fa==fb,"Updated camera projection differs");}
    auto bad=water.frame;bad.viewProjection=glm::mat4(0);bool rejected=false;try{moving.updateCamera(bad);}catch(const std::invalid_argument&){rejected=true;}check(rejected&&moving.cameraData().inverseProjection==cameraReference.cameraData().inverseProjection,"Invalid camera damaged retained scene");
    auto identity=pt::sceneIdentity(world);auto cameraOnly=world;cameraOnly.frame.viewProjection=water.frame.viewProjection;cameraOnly.frame.cameraPosition=water.frame.cameraPosition;cameraOnly.sequence=100;cameraOnly.exposure=2;cameraOnly.frame.ssao=true;
    check(identity==pt::sceneIdentity(cameraOnly),"Display/camera-only edits invalidated transport identity");
    check(!(identity==pt::sceneIdentity(changed)),"Transport identity omitted geometry/material/transform changes");
    cameraOnly=world;cameraOnly.draws[0].pathTracingIor=1.5f;check(!(identity==pt::sceneIdentity(cameraOnly)),"Optical edit did not invalidate PT");
    cameraOnly=world;cameraOnly.frame.oceans.emplace_back();auto oceanId=pt::sceneIdentity(cameraOnly);cameraOnly.frame.oceans[0].spectrum.windSpeed+=1;check(!(oceanId==pt::sceneIdentity(cameraOnly)),"FFT edit omitted from capture identity");
    cameraOnly.frame.oceans[0].spectrum.seaState.enabled=true;auto seaId=pt::sceneIdentity(cameraOnly);
    cameraOnly.frame.oceans[0].spectrum.seaState.swellPeriod+=1;check(!(seaId==pt::sceneIdentity(cameraOnly)),"Swell period omitted from PT capture identity");
    seaId=pt::sceneIdentity(cameraOnly);cameraOnly.frame.oceans[0].spectrum.seaState.depth=8;check(!(seaId==pt::sceneIdentity(cameraOnly)),"Wave depth omitted from PT capture identity");
    seaId=pt::sceneIdentity(cameraOnly);cameraOnly.frame.oceans[0].shortWaveRipples=true;check(!(seaId==pt::sceneIdentity(cameraOnly)),"Ripple mode omitted from PT capture identity");
    seaId=pt::sceneIdentity(cameraOnly);cameraOnly.frame.oceans[0].surfaceModel.physical=true;check(!(seaId==pt::sceneIdentity(cameraOnly)),"Physical water model omitted from capture identity");
    seaId=pt::sceneIdentity(cameraOnly);cameraOnly.frame.oceans[0].surfaceModel.roughness=.12f;check(!(seaId==pt::sceneIdentity(cameraOnly)),"Shared water roughness omitted from capture identity");
    seaId=pt::sceneIdentity(cameraOnly);cameraOnly.frame.cameraPosition.x+=1;check(!(seaId==pt::sceneIdentity(cameraOnly)),"Focused ocean camera position omitted from capture identity");
    cameraOnly=world;cameraOnly.frame.sky=true;auto skyId=pt::sceneIdentity(cameraOnly);cameraOnly.frame.cameraPosition.y=10;check(!(skyId==pt::sceneIdentity(cameraOnly)),"Sky observer altitude did not invalidate baked lighting");
    cameraOnly=world;cameraOnly.frame.clouds.enabled=true;auto cloudId=pt::sceneIdentity(cameraOnly);cameraOnly.frame.clouds.coverage+=.1f;check(!(cloudId==pt::sceneIdentity(cameraOnly)),"Cloud coverage did not invalidate P1 sky/solar capture");
    auto field=std::make_shared<render::CloudShadowField>();field->width=2;field->patch={-1,-1,2,2};field->projection={0,0,0,100};field->transmission={1,.5f,.25f,0};
    auto frozen=render::deserializeCloudShadow(render::serializeCloudShadow(*field));check(frozen->transmission==field->transmission&&std::abs(frozen->sample({0,0,0})-.4375f)<1e-6,"Cloud solar field round trip or bilinear interpolation differs");
    check(frozen->sample({2,0,0})==1&&frozen->sample({0,100,0})==1,"Cloud solar field extended beyond its receiver domain");
    field->transmission[0]=-1;bool invalidField=false;try{render::serializeCloudShadow(*field);}catch(const std::invalid_argument&){invalidField=true;}check(invalidField,"Invalid cloud transmission was serialized");
    auto options=opts;options.exposure=3;check(pt::sameRenderOptions(opts,options),"Exposure invalidated raw GPU film");options.maxDepth++;check(!pt::sameRenderOptions(opts,options),"Depth edit did not invalidate raw film");
    options=opts;options.gpuTraversal=pt::GpuTraversal::Software;check(!pt::sameRenderOptions(opts,options),"Traversal change reused a film from another intersection path");
}
void packedExportSharing() {
    auto world=snapshot();auto draw=triangle({-.4f,-.4f,0},{.4f,-.4f,0},{0,.4f,0});
    auto image=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{2,2,std::vector<uint8_t>(16,255)});
    auto material=std::make_shared<render::MaterialPayload>();material->images[0]=image;draw.material=material;
    for(int i=0;i<128;++i){auto instance=draw;instance.objectId=i+1;instance.model=glm::translate(glm::mat4(1),glm::vec3((i%16-7.5f)*.7f,(i/16-3.5f)*.7f,0));world.draws.push_back(instance);}
    auto cache=std::make_shared<pt::GeometryCache>(0);
    auto cold=std::make_unique<pt::CpuScene>(world,std::vector<pt::DielectricMaterial>{},cache);
    auto original=cold->exportData();
    check(!original.geometryReused&&!original.texturesReused&&cold->accelerationStats().alphaImageScans==1&&cold->accelerationStats().alphaPixelsScanned==4,"Shared alpha image was scanned per instance / cold export reused unbuilt data");
    auto changed=world;changed.draws[0].parameters.albedoAlpha.x=.1f;
    pt::CpuScene hot(changed,{},cache,cold.get());auto hotData=hot.exportData();
    check(hotData.geometry==original.geometry&&hotData.textures==original.textures&&hotData.geometryReused&&hotData.texturesReused,"Scalar material edit repacked immutable geometry / textures");
    changed.draws[0].model[3].x+=.05f;pt::CpuScene moved(changed,{},cache,&hot);auto movedData=moved.exportData();
    check(movedData.geometry==original.geometry&&movedData.textures==original.textures,"Transform edit copied static packed arrays");
    auto added=changed;added.draws.push_back(draw);pt::CpuScene topology(added,{},cache,&moved);auto topologyData=topology.exportData();
    check(topologyData.geometry==original.geometry&&topologyData.instances.size()==129,"Adding an instance of existing geometry repacked BLAS");
    auto transparent=std::make_shared<render::ImageRGBA8>(*image);transparent->pixels.back()=0;
    auto replacement=std::make_shared<render::MaterialPayload>(*material);replacement->images[0]=transparent;changed.draws[0].material=replacement;
    pt::CpuScene textured(changed,{},cache,&moved);auto textureData=textured.exportData();
    check(textureData.geometry==original.geometry&&textureData.textures!=original.textures&&!textureData.texturesReused&&textured.accelerationStats().alphaImageScans==2&&!textured.opaqueShadows(),"New alpha image reused stale texels / opaque summary");
    check(textureData.textures->texels[3]==0x00ffffffu&&original.textures->texels[3]==0xffffffffu,"Packed texture replacement modified an old export");
    auto altered=std::make_shared<render::MeshPayload>(*draw.mesh);altered->vertices[2].position.y+=.1f;
    changed.draws[0].mesh=altered;pt::CpuScene geometry(changed,{},cache,&textured);auto geometryData=geometry.exportData();
    check(geometryData.geometry!=textureData.geometry&&!geometryData.geometryReused&&geometryData.textures==textureData.textures,"New geometry reused stale packed BLAS / invalidated unchanged texels");
    auto reordered=changed;std::swap(reordered.draws[0],reordered.draws[1]);pt::CpuScene reorderedScene(reordered,{},cache,&geometry);auto reorderedData=reorderedScene.exportData();
    check(reorderedData.geometry!=geometryData.geometry&&reorderedData.textures!=geometryData.textures,"Changed geometry / image order reused old root or texture indices");
    pt::CpuScene fresh(changed);geometry.environment=fresh.environment=std::make_shared<pt::Environment>(2,1,std::vector<glm::vec3>(2,glm::vec3(.5f)));auto freshData=fresh.exportData();
    check(freshData.geometry->vertices.size()==geometryData.geometry->vertices.size()&&freshData.textures->texels==geometryData.textures->texels&&freshData.textures->images==geometryData.textures->images,"Shared export differs from fresh packing");
    pt::Options opts;opts.width=16;opts.height=12;opts.samples=4;opts.adaptive=false;opts.threads=1;
    check(pt::render(geometry,opts).radiance==pt::render(fresh,opts).radiance,"Alpha scan dedup / packed reuse changed CPU transport");
    pt::CpuScene concurrent(world);auto first=std::async(std::launch::async,[&]{return concurrent.exportData();});auto second=std::async(std::launch::async,[&]{return concurrent.exportData();});auto a=first.get(),b=second.get();
    check(a.geometry==b.geometry&&a.textures==b.textures&&a.geometryReused!=b.geometryReused&&a.texturesReused!=b.texturesReused,"Concurrent exports raced lazy publication");
    cold.reset();image.reset();material.reset();
    check(original.geometry->vertices.size()==3&&original.textures->texels[3]==0xffffffffu&&cache->retainedBytes()==0,"Packed export lifetime depends on evicted CPU geometry / destroyed scene");
    std::cout<<"Packed exports: immutable leases, alpha dedup, image/geometry/order invalidation, concurrent publication and old-export lifetime passed\n";
}
void incrementalTlas() {
    auto world=snapshot();auto source=triangle({-.6f,-.6f,0},{.6f,-.6f,0},{0,.6f,0});
    for(int i=0;i<64;++i){auto draw=source;draw.objectId=i+1;draw.model=glm::translate(glm::mat4(1),glm::vec3((i%8-3.5f)*2,(i/8-3.5f)*2,0));world.draws.push_back(draw);}
    auto emitter=triangle({-6,8,3},{6,8,3},{0,-6,3});emitter.objectId=1000;emitter.parameters.emissiveNormal={2,3,4,1};world.draws.push_back(emitter);
    auto cache=std::make_shared<pt::GeometryCache>();auto old=std::make_shared<pt::CpuScene>(world,std::vector<pt::DielectricMaterial>{},cache);
    check(old->accelerationStats().tlasBuilds==1&&old->accelerationStats().emitterBuilds==1,"Cold scene did not build TLAS/emitter distribution");
    auto material=world;material.draws[0].parameters.albedoAlpha={.2f,.8f,.4f,1};
    pt::CpuScene hot(material,{},cache,old.get());auto a=hot.accelerationStats();
    check(a.tlasReuses==1&&a.tlasBuilds==0&&a.tlasRefits==0&&a.emitterReuses==1&&a.thinNormalReuses==1&&a.cameraMediaReuses==1&&a.transformReuses==65&&a.instanceBoundsReuses==65,"Material-only scene recomputed unchanged acceleration/derived data");
    auto noRetention=std::make_shared<pt::GeometryCache>(0);pt::CpuScene active(world,{},noRetention),leased(material,{},noRetention,&active);
    check(noRetention->retainedBytes()==0&&leased.accelerationStats().blasBuilds==0&&leased.accelerationStats().blasSceneReuses==2&&leased.accelerationStats().tlasReuses==1,"Evicted/oversized BLAS was rebuilt despite a valid previous-scene lease");
    auto light=world;light.frame.lights.push_back({{0,0,2,1},{10,10,10,0},{0,0,-1,0}});pt::CpuScene relit(light,{},cache,old.get());check(relit.accelerationStats().tlasReuses==1&&relit.accelerationStats().emitterReuses==1,"Analytic light edit rebuilt acceleration/emitter geometry");
    auto emission=world;emission.draws.back().parameters.emissiveNormal.x*=2;pt::CpuScene changedEmitter(emission,{},cache,old.get());
    check(changedEmitter.accelerationStats().tlasReuses==1&&changedEmitter.accelerationStats().emitterBuilds==1,"Emitter RGB edit reused stale sampling weights");
    auto moved=material;moved.draws[0].model=glm::translate(glm::mat4(1),glm::vec3(-6.8f,-7,0));
    pt::CpuScene refitted(moved,{},cache,old.get()),fresh(moved);a=refitted.accelerationStats();
    const auto before=old->exportData(),after=refitted.exportData();
    check(before.geometry->blasNodes.size()==a.blasNodes&&after.tlasNodes.size()==a.tlasNodes&&before.instances[0].info.x==0,"GPU export does not use independent BLAS/TLAS address spaces");
    check(before.geometry->blasNodes.size()==after.geometry->blasNodes.size()&&std::memcmp(before.geometry->blasNodes.data(),after.geometry->blasNodes.data(),before.geometry->blasNodes.size()*sizeof(pt::PackedNode))==0,"Instance motion changed immutable GPU BLAS");
    for(const auto &instance:after.instances)check(instance.info.x<after.geometry->blasNodes.size(),"Exported instance BLAS root is outside the BLAS buffer");
    check(a.tlasRefits==1&&a.tlasBuilds==0&&a.emitterBuilds==1&&a.cameraMediaReuses==0&&a.tlasRefitAge==1&&a.transformReuses==64&&a.instanceBoundsReuses==64,"Small transform change did not refit TLAS / invalidate world-space derived data");
    auto compareHits=[&](const pt::CpuScene &candidate,const pt::CpuScene &reference,const render::RenderWorldSnapshot &input){
        for(const auto &draw:input.draws){const auto target=glm::vec3(draw.model*glm::vec4(0,0,0,1)),origin=target+glm::vec3(0,0,2);pt::Surface x,y,z;
            bool hx=candidate.intersect(origin,{0,0,-1},0,20,x),hy=reference.intersect(origin,{0,0,-1},0,20,y),hz=candidate.intersect(origin,{0,0,-1},0,20,z,true);
            check(hx==hy&&hx==hz,"Refit hit/miss differs from SAH rebuild / brute force");if(hx)check(x.primitive==y.primitive&&x.primitive==z.primitive&&x.distance==y.distance&&near(x.albedo,y.albedo),"Refit primitive/distance/material differs from SAH rebuild / brute force");
        }
    };compareHits(refitted,fresh,moved);
    pt::Options opts;opts.width=16;opts.height=12;opts.samples=8;opts.maxDepth=4;opts.threads=1;opts.adaptive=false;
    check(pt::render(refitted,opts).radiance==pt::render(fresh,opts).radiance,"Refit changed raw CPU path transport");
    auto scrambled=world;std::vector<uint32_t> permutation(64);std::iota(permutation.begin(),permutation.end(),0u);pt::Random shuffle(71);
    for(int i=63;i>0;--i)std::swap(permutation[i],permutation[uint32_t(shuffle.uniform()*(i+1))]);
    for(int i=0;i<64;++i)scrambled.draws[i].model=world.draws[permutation[i]].model;
    pt::CpuScene degraded(scrambled,{},cache,old.get()),scrambledFresh(scrambled);a=degraded.accelerationStats();
    std::cout<<"TLAS quality fallback: builds "<<a.tlasBuilds<<", refit rebuilds "<<a.refitRebuilds<<", cost "<<a.tlasCost<<" / "<<a.tlasBuildCost<<'\n';
    check(a.refitRebuilds==1&&a.tlasBuilds==1&&a.tlasRefits==0&&a.tlasRefitAge==0,"Poor TLAS refit did not fall back to SAH");compareHits(degraded,scrambledFresh,scrambled);
    auto overlap=moved;overlap.draws[0].model=overlap.draws[1].model;pt::CpuScene overlapping(overlap,{},cache,old.get()),overlapFresh(overlap);compareHits(overlapping,overlapFresh,overlap);
    auto added=world;auto extra=source;extra.objectId=999;added.draws.push_back(extra);pt::CpuScene topology(added,{},cache,old.get());check(topology.accelerationStats().tlasBuilds==1&&topology.accelerationStats().tlasRefits==0,"Topology addition incorrectly refitted TLAS");
    auto reordered=world;std::swap(reordered.draws[0],reordered.draws[1]);pt::CpuScene orderChange(reordered,{},cache,old.get());check(orderChange.accelerationStats().tlasBuilds==1,"Changed instance order incorrectly reused TLAS identities");
    auto changed=world;auto mesh=std::make_shared<render::MeshPayload>(*source.mesh);mesh->vertices[2].position.y+=.2f;changed.draws[0].mesh=mesh;pt::CpuScene geometry(changed,{},cache,old.get());check(geometry.accelerationStats().tlasBuilds==1,"Changed source geometry incorrectly reused TLAS");
    auto aging=world;for(uint32_t i=1;i<=33;++i){aging.draws[0].model=glm::translate(glm::mat4(1),glm::vec3(-7+i*.001f,-7,0));auto next=std::make_shared<pt::CpuScene>(aging,std::vector<pt::DielectricMaterial>{},cache,old.get());a=next->accelerationStats();if(i<=32)check(a.tlasRefits==1&&a.tlasRefitAge==i,"Refit age was not retained across updates");else check(a.refitRebuilds==1&&a.tlasBuilds==1&&a.tlasRefitAge==0,"Refit age limit did not force SAH rebuild");old=std::move(next);}
    auto media=pt::makeMediumValidationScene(8,8,true).snapshot;media.frame.cameraPosition.z=0;media.frame.viewProjection=media.frame.viewProjection*glm::translate(glm::mat4(1),glm::vec3(0,0,4));const auto originalBoundary=media;pt::CpuScene volume(media,{},cache);check(volume.cameraData().media[0].x!=0,"Optical invalidation fixture must start inside its volume");media.draws[0].pathTracingIor=0;media.draws[0].pathTracingAbsorption=glm::vec3(0);media.draws[0].pathTracingScattering=glm::vec3(0);media.draws[0].pathTracingKind=0;pt::CpuScene noBoundary(media,{},cache,&volume),freshBoundary(media);
    check(noBoundary.accelerationStats().cameraMediaReuses==0&&noBoundary.cameraData().media==freshBoundary.cameraData().media,"Optical boundary edit reused stale camera media");
    auto cutout=originalBoundary;auto alpha=std::make_shared<render::MaterialPayload>();alpha->images[0]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{1,1,{255,255,255,0}});cutout.draws[0].material=alpha;cutout.draws[0].parameters.factors.w=.5f;
    pt::CpuScene invisibleBoundary(cutout,{},cache,&volume),freshInvisible(cutout);
    check(invisibleBoundary.accelerationStats().tlasReuses==1&&invisibleBoundary.accelerationStats().cameraMediaReuses==0&&invisibleBoundary.cameraData().media==freshInvisible.cameraData().media&&invisibleBoundary.cameraData().media[0].x==0,"Alpha-cutout edit reused stale camera boundary membership");
    auto empty=world;empty.draws.clear();pt::CpuScene removed(empty,{},cache,old.get());check(removed.accelerationStats().tlasBuilds==1&&removed.accelerationStats().tlasNodes==0&&removed.accelerationStats().tlasCost==0,"Removing all instances retained stale TLAS nodes");
    std::cout<<"CPU incremental TLAS: reuse/refit/topology/quality/age fallback, derived data and raw-film parity passed\n";
}
void largeInstanceUpdates() {
    auto world=snapshot();auto draw=triangle({-.4f,-.4f,0},{.4f,-.4f,0},{0,.4f,0});
    for(uint32_t i=0;i<20000;++i){auto instance=draw;instance.objectId=i+1;instance.model=glm::translate(glm::mat4(1),glm::vec3(float(i%200)-100,float(i/200)-50,0));world.draws.push_back(instance);}
    auto cache=std::make_shared<pt::GeometryCache>();pt::CpuScene cold(world,{},cache);
    world.draws[100].parameters.albedoAlpha={.1f,.7f,.3f,1};pt::CpuScene material(world,{},cache,&cold);auto a=material.accelerationStats();
    check(a.tlasReuses==1&&a.tlasBuilds==0&&a.blasBuilds==0&&a.transformReuses==20000&&a.instanceBoundsReuses==20000,"Large material update repeated per-instance transform/bounds preparation");
    world.draws[100].model[3].x+=.01f;pt::CpuScene moved(world,{},cache,&material),fresh(world);a=moved.accelerationStats();
    check(a.tlasRefits==1&&a.blasBuilds==0&&a.transformReuses==19999&&a.instanceBoundsReuses==19999,"Large sparse transform update did not retain unchanged instances");
    for(uint32_t i:{0u,100u,7000u,19999u}){auto origin=glm::vec3(world.draws[i].model[3])+glm::vec3(0,0,2);pt::Surface x,y;check(moved.intersect(origin,{0,0,-1},0,10,x)&&fresh.intersect(origin,{0,0,-1},0,10,y)&&x.primitive==y.primitive&&x.distance==y.distance&&near(x.albedo,y.albedo),"Large refit changed sampled instance identity/material");}
    std::cout<<"20k instance update stage times (single CPU wall-clock observation; not a benchmark): TLAS build "<<cold.accelerationStats().tlasSeconds<<", reuse "<<material.accelerationStats().tlasSeconds<<", refit "<<a.tlasSeconds<<" s; retained transforms "<<a.transformReuses<<'\n';
}
void instances() {
    auto world=snapshot(),expanded=snapshot();auto source=triangle({-1,-1,0},{1,-1,0},{0,1,0});
    auto uvMesh=std::make_shared<render::MeshPayload>(*source.mesh);uvMesh->vertices[0].uv={0,0};uvMesh->vertices[1].uv={1,0};uvMesh->vertices[2].uv={.5f,1};source.mesh=uvMesh;
    auto maps=std::make_shared<render::MaterialPayload>();maps->images[0]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{2,2,{220,70,40,255,40,190,50,0,50,60,240,255,220,210,60,255}});maps->images[1]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{1,1,{151,105,249,255}});source.material=maps;source.parameters.emissiveNormal.w=.6f;
    std::vector<glm::vec3> targets;
    for(int i=0;i<64;++i){auto draw=source;draw.model=glm::translate(glm::mat4(1),glm::vec3((i%8-4)*3.5f,(i/8-4)*3.5f,0))*glm::rotate(glm::mat4(1),.15f*(i%4),glm::vec3(0,1,0))*glm::scale(glm::mat4(1),glm::vec3(i%2?-1.2f:.7f,.5f+.2f*(i%5),1.3f));draw.parameters.albedoAlpha={.5f+.006f*i,.7f,.8f,1};draw.parameters.factors.w=i%3==0?.5f:0;draw.pathTracingBsdfModel=i%3;
        world.draws.push_back(draw);targets.push_back(glm::vec3(draw.model*glm::vec4(0,0,0,1)));
        auto flat=draw;auto mesh=std::make_shared<render::MeshPayload>(*draw.mesh);auto normal=glm::transpose(glm::inverse(glm::mat3(draw.model)));for(auto &v:mesh->vertices){v.position=glm::vec3(draw.model*glm::vec4(v.position,1));v.normal=glm::normalize(normal*v.normal);}if(glm::determinant(glm::mat3(draw.model))<0)std::swap(mesh->indices[1],mesh->indices[2]);flat.mesh=mesh;flat.model=glm::mat4(1);expanded.draws.push_back(flat);
    }
    pt::CpuScene shared(world),flat(expanded);auto stats=shared.accelerationStats();check(stats.uniqueMeshes==1&&stats.instances==64&&stats.uniqueTriangles==1&&stats.expandedTriangles==64,"BLAS geometry duplicated across instances");auto packed=shared.exportData();check(packed.geometry->vertices.size()==3&&packed.geometry->triangles.size()==1&&packed.instances.size()==64,"GPU export expanded shared geometry");check(shared.memoryBytes()<flat.memoryBytes(),"Instance sharing did not reduce CPU acceleration memory");
    pt::Random random(497);int hits=0;
    for(int i=0;i<4096;++i){auto target=targets[i%targets.size()]+glm::vec3(random.uniform()*2-1,random.uniform()*2-1,0);auto origin=target+glm::vec3(.4f,-.2f,3);auto direction=glm::normalize(target-origin)*(i%2?2.f:1.f);pt::Surface a,b,c;bool x=shared.intersect(origin,direction,.0001f,100,a),y=flat.intersect(origin,direction,.0001f,100,b),z=shared.intersect(origin,direction,.0001f,100,c,true);check(x==y&&x==z,"Instanced TLAS hit/miss differs from flattened/world brute force");check(shared.intersectsAny(origin,direction,.0001f,100)==z,"Any-hit differs for mirrored/cutout/nonuniform instances");if(!x)continue;++hits;
        check(std::abs(a.distance-b.distance)<2e-5f&&std::abs(a.distance-c.distance)<2e-5f,"Non-normalized local ray lost world t");check(near(a.albedo,b.albedo,.0001f)&&near(a.normal,b.normal,.0001f)&&a.frontFace==b.frontFace&&a.bsdfModel==b.bsdfModel,"Mirrored/nonuniform instance changed UV, normal, winding or material");}
    check(hits>500,"Instance probe did not exercise enough alpha/normal hits");
    auto lightWorld=snapshot();auto emitter=source;emitter.parameters.emissiveNormal={4,4,4,0};emitter.parameters.factors.w=0;emitter.material.reset();emitter.model=glm::scale(glm::mat4(1),glm::vec3(-2,3,1));lightWorld.draws={emitter};pt::CpuScene light(lightWorld);auto sample=light.sampleEmitter(random);check(std::abs(sample.pdfArea-1.f/12)<1e-6f,"Emitter area PDF omitted affine area scale");check(near(sample.surface.geometricNormal,{0,0,1}),"Mirrored emitter outward normal reversed");
    auto cubes=pt::makeMediumValidationScene(8,8,true).snapshot;auto cube=cubes.draws[0];cube.model=glm::mat4(1);auto other=cube;other.model=glm::translate(glm::mat4(1),glm::vec3(4,0,0))*glm::scale(glm::mat4(1),glm::vec3(-.5f,2,1));other.pathTracingAbsorption={.9f,.2f,.1f};cubes.draws={cube,other};pt::CpuScene media(cubes);check(media.accelerationStats().uniqueMeshes==1&&media.initialMedia({0,0,0})[0]==1&&media.initialMedia({4,0,0})[0]==2,"Shared dielectric geometry lost per-instance medium identity");check(near(media.initialAbsorption({4,0,0}),other.pathTracingAbsorption),"Instance medium coefficients leaked across shared BLAS");
    for(int kind=0;kind<3;++kind){auto invalid=snapshot();auto draw=source;if(kind==0)draw.model[0][0]=0;if(kind==1)draw.model[0][3]=.1f;if(kind==2)draw.model[3][0]=NAN;invalid.draws={draw};bool rejected=false;try{pt::CpuScene bad(invalid);}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Invalid affine instance transform accepted");}
}
void comparisonClosures() {
    pt::Surface s;s.normal=s.geometricNormal={0,0,1};s.albedo={.3f,.6f,.8f};s.bsdfModel=1;s.metallic=.7f;s.roughness=.65f;
    auto v=glm::normalize(glm::vec3(.8f,0,1));check(near(pt::evaluateBsdf(s,v,{0,0,1}),s.albedo/pi),"Controlled Lambert depends on metallic/Fresnel");
    pt::Random rng(891);glm::vec3 integral(0);for(int i=0;i<8192;++i){auto a=pt::sampleBsdf(s,v,rng);integral+=a.value*a.direction.z/a.pdf;}
    check(near(integral/8192.f,s.albedo,.001f),"Lambert sampler does not conserve diffuse reflectance");
    // Independent equal-solid-angle quadrature at oblique incidence checks the
    // additive closure, visible-normal sampling and correlated Smith masking.
    s.bsdfModel=2;glm::vec3 quadrature(0),estimate(0);constexpr int nz=256,np=512;
    for(int z=0;z<nz;++z)for(int p=0;p<np;++p){float c=(z+.5f)/nz,phi=2*pi*(p+.5f)/np,r=std::sqrt(1-c*c);quadrature+=pt::evaluateBsdf(s,v,{r*std::cos(phi),r*std::sin(phi),c})*c*(2*pi/(nz*np));}
    for(int i=0;i<65536;++i){auto a=pt::sampleBsdf(s,v,rng);if(a.pdf>0)estimate+=a.value*a.direction.z/a.pdf;}
    check(near(estimate/65536.f,quadrature,.009f),"Controlled GGX estimator differs from independent integral");
    for(auto model:{1u,2u}){auto room=snapshot();auto draw=triangle({-1,-1,0},{1,-1,0},{0,1,0});draw.pathTracingBsdfModel=model;draw.pathTracingKind=5;draw.pathTracingIor=1.5f;room.draws.push_back(draw);pt::CpuScene scene(room);auto flags=scene.exportData().materials[0].extra.w;check((flags&1)==1&&(flags>>1)==model,"Packed BSDF model corrupts thin-sheet bit");}
}
void cornerTangentsAndLeaves() {
    auto draw=triangle({-1,-1,0},{1,-1,0},{0,1,0});auto source=std::make_shared<render::MeshPayload>(*draw.mesh);
    source->vertices[0].normal=glm::normalize(glm::vec3(.1f,0,1));source->vertices[1].normal=glm::normalize(glm::vec3(-.1f,.2f,1));source->vertices[2].normal=glm::normalize(glm::vec3(0,-.2f,1));
    source->vertices[0].uv={0,0};source->vertices[1].uv={1,0};source->vertices[2].uv={.5f,1};
    for(const auto &v:source->vertices){auto t=glm::normalize(glm::vec3(1,0,0)-v.normal*v.normal.x);source->pathTracingTangents.emplace_back(t,1);}
    auto maps=std::make_shared<render::MaterialPayload>();maps->images[1]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{1,1,{151,105,249,255}});draw.material=maps;draw.parameters.emissiveNormal.w=.6f;
    const glm::vec3 weights{.25f,.35f,.4f};
    for(bool mirrored:{false,true})for(bool mirroredUV:{false,true})for(bool front:{false,true}){
        auto mesh=std::make_shared<render::MeshPayload>(*source);if(mirroredUV)for(size_t i=0;i<3;++i){mesh->vertices[i].uv.x=1-mesh->vertices[i].uv.x;mesh->pathTracingTangents[i]=-mesh->pathTracingTangents[i];}
        draw.mesh=mesh;draw.model=glm::rotate(glm::mat4(1),.3f,glm::vec3(0,1,0))*glm::scale(glm::mat4(1),glm::vec3(mirrored?-2.f:2.f,.4f,1.3f));auto world=snapshot();world.draws={draw};pt::CpuScene scene(world);
        auto normalMatrix=glm::transpose(glm::inverse(glm::mat3(draw.model)));glm::vec3 point(0),n(0),t(0);for(size_t i=0;i<3;++i){point+=mesh->vertices[i].position*weights[i];n+=mesh->vertices[i].normal*weights[i];t+=glm::vec3(mesh->pathTracingTangents[i])*weights[i];}
        point=glm::vec3(draw.model*glm::vec4(point,1));auto b=glm::cross(n,t)*(mirroredUV?-1.f:1.f);
        auto map=glm::vec3(151,105,249)/255.f*2.f-1.f;map.x*=.6f;map.y*=.6f;map.z=glm::mix(1.f,map.z,.6f);auto expected=glm::normalize(normalMatrix*(t*map.x+b*map.y+n*map.z))*(front?1.f:-1.f);auto gn=glm::normalize(normalMatrix*glm::vec3(0,0,1));pt::Surface hit;
        check(scene.intersect(point+gn*(front?3.f:-3.f),gn*(front?-1.f:1.f),.0001f,10,hit)&&near(hit.normal,expected,.0002f),"Exported corner tangent interpolation, mirror, nonuniform scale or backface changed normal mapping");
    }
    pt::Surface leaf;leaf.bsdfModel=3;leaf.normal=leaf.geometricNormal={0,0,1};leaf.albedo={.2f,.3f,.4f};leaf.diffuseTransmission={.6f,.2f,.1f};pt::Random random(989);glm::vec3 energy(0);int transmitted=0;
    for(int i=0;i<32768;++i){auto sample=pt::sampleBsdf(leaf,{0,0,1},random);check(!sample.delta&&sample.pdf>0&&sample.transmission==(sample.direction.z<0),"Thin diffuse event flags or hemisphere changed");energy+=sample.value*(std::abs(sample.direction.z)/sample.pdf);transmitted+=sample.transmission;}
    check(near(energy/32768.f,leaf.albedo+leaf.diffuseTransmission,.009f),"Thin diffuse white furnace does not match independently specified R+T");float probability=glm::dot(leaf.diffuseTransmission,glm::vec3(.2126f,.7152f,.0722f))/glm::dot(leaf.albedo+leaf.diffuseTransmission,glm::vec3(.2126f,.7152f,.0722f));check(std::abs(float(transmitted)/32768-probability)<.01f,"Thin diffuse sampling probability differs from RGB lobe power");
    auto world=snapshot();auto pane=triangle({-100,-100,0},{100,-100,0},{0,100,0});pane.pathTracingBsdfModel=3;pane.pathTracingDiffuseTransmission={.6f,.2f,.1f};world.draws={pane};world.frame.inverseSquareLocalLights=true;world.frame.lights.push_back({{0,0,-2,1},{4,4,4,0},{0,0,1,0}});pt::CpuScene scene(world);uint64_t rays=0;auto value=scene.trace({0,0,2},{0,0,-1},random,1,rays);check(near(value,pane.pathTracingDiffuseTransmission/pi,.00001f),"Thin leaf backside NEE missed, self-shadowed or introduced dielectric attenuation");check(scene.media().empty()&&scene.initialMedia({0,0,1})[0]==0,"Thin diffuse leaf entered medium stack");
}
void grazingClosures() {
    const glm::vec3 gn{0,0,1};
    for(float z:{.001f,.02f,.2f,.8f})for(float tilt:{0.f,.3f,.8f,1.3f,1.55f})for(float azimuth:{0.f,1.f,3.f}){
        auto view=glm::vec3(std::sqrt(1-z*z)*std::cos(azimuth),std::sqrt(1-z*z)*std::sin(azimuth),z);
        auto n=glm::vec3(std::sin(tilt),0,std::cos(tilt)),safe=pt::correctedReflectionNormal(gn,view,n);
        const float threshold=std::min(.9f*z,.01f);
        check(std::abs(glm::length(safe)-1)<1e-5f&&safe.z>=n.z-1e-5f&&glm::dot(gn,glm::reflect(-view,safe))>=threshold-2e-6f,"Glossy normal correction is nonunit, rotates away from geometry or reflects below the surface");
        if(glm::dot(gn,glm::reflect(-view,n))>=threshold)check(near(n,safe,1e-6f),"Already valid reflection normal changed");
    }
    pt::Surface s;s.geometricNormal=gn;s.normal={.8f,0,.6f};s.bsdfModel=1;s.albedo={.2f,.3f,.4f};
    const auto view=glm::normalize(glm::vec3(-.95f,0,.2f));
    check(glm::dot(s.normal,view)<0&&near(pt::evaluateBsdf(s,view,gn)*pt::surfaceCosine(s,gn),s.albedo*.6f/pi,1e-6f)&&pt::bsdfPdf(s,view,gn)>0,"Lambert normal was rejected because it points away from the view");
    s.diffuseNormal=s.normal;s.smoothNormal=gn;s.normal=pt::correctedReflectionNormal(gn,view,s.normal);
    s.diffuseTransmission={.6f,.2f,.1f};s.roughness=.5f;
    for(auto model:{2u,3u}){
        s.bsdfModel=model;glm::vec3 integral(0),estimate(0);pt::Random rng(3921);
        constexpr int nz=384,np=512;
        for(int z=0;z<nz;++z)for(int p=0;p<np;++p){float c=-1+2.f*(z+.5f)/nz,phi=2*pi*(p+.5f)/np,r=std::sqrt(1-c*c);glm::vec3 l{r*std::cos(phi),r*std::sin(phi),c};integral+=pt::evaluateBsdf(s,view,l)*pt::surfaceCosine(s,l)*(4*pi/(nz*np));}
        constexpr int samples=131072;
        for(int i=0;i<samples;++i){auto a=pt::sampleBsdf(s,view,rng);if(a.pdf<=0)continue;check(std::isfinite(a.value.x)&&std::abs(a.pdf-pt::bsdfPdf(s,view,a.direction))<1e-5f,"Grazing sample is nonfinite or differs from mixture PDF");estimate+=a.value*pt::surfaceCosine(s,a.direction)/a.pdf;}
        check(near(estimate/float(samples),integral,.006f),"Different diffuse/glossy/translucent normals break sample/PDF/integral agreement");
        check(glm::all(glm::lessThanEqual(integral,s.albedo+s.diffuseTransmission+.05f)),"Grazing bump correction gains energy");
    }
    for(auto model:{1u,2u}){
        auto world=snapshot();auto draw=triangle({-1,-1,0},{1,-1,0},{0,1,0});draw.pathTracingBsdfModel=model;
        if(model==1){auto maps=std::make_shared<render::MaterialPayload>();maps->images[1]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{1,1,{240,128,170,255}});draw.material=maps;}
        world.draws={draw};pt::CpuScene scene(world);bool rejected=false;
        try{scene.validateBidirectional();}catch(const std::invalid_argument &e){rejected=std::string(e.what()).find("bump-corrected")!=std::string::npos;}
        check(rejected,"BDPT accepted an unvalidated bump-corrected closure adjoint");
    }
}
void waterSolarReflection() {
    auto world=pt::makeWaterSolarValidationScene(8,8).snapshot;pt::CpuScene scene(world);
    scene.sunDirection=glm::normalize(glm::vec3(0,.2f,1));scene.sunIrradiance=glm::vec3(1);scene.sunRadius=.01f;
    check(scene.proceduralCount(3)==1&&scene.proceduralCount(999)==0&&scene.scatteringCount()==0,"Cached procedural/medium counts differ from retained instances");
    // Independent finite-disc integral after one planar Fresnel reflection.
    double expected=0;constexpr int nz=64,np=128;auto tangent=glm::vec3(1,0,0),bitangent=glm::cross(scene.sunDirection,tangent);float edge=std::cos(scene.sunRadius);
    for(int z=0;z<nz;++z)for(int p=0;p<np;++p){float c=glm::mix(edge,1.f,(z+.5f)/nz),r=std::sqrt(1-c*c),phi=2*pi*(p+.5f)/np;auto l=scene.sunDirection*c+(tangent*std::cos(phi)+bitangent*std::sin(phi))*r;
        expected+=.8/pi*l.z*pt::dielectricFresnel(l.y,1,1.333f)*(2*pi*(1-edge))/(pi*std::sin(scene.sunRadius)*std::sin(scene.sunRadius)*nz*np);}
    double mean[2]{},square[2]{};constexpr int samples=65536;
    for(int mode=0;mode<2;++mode){pt::Random random(12345);uint64_t rays=0;
        for(int i=0;i<samples;++i){auto value=scene.trace({0,1,2},{0,0,-1},random,2,rays,nullptr,mode==1);check(std::isfinite(value.x)&&value.x>=0,"Water reflection proposal generated nonfinite energy");mean[mode]+=value.x;square[mode]+=double(value.x)*value.x;}
        mean[mode]/=samples;square[mode]=square[mode]/samples-mean[mode]*mean[mode];}
    std::cout<<"Water solar reflection analytic/proposal/baseline "<<expected<<" / "<<mean[1]<<" / "<<mean[0]<<", variance "<<square[1]<<" / "<<square[0]<<'\n';
    check(std::abs(mean[1]-expected)<.006,"Solar reflection mixture disagrees with independent finite-disc Fresnel integral");
    check(square[1]<square[0]*.02,"Water solar proposal did not reduce rare reflection variance");
    world.draws[0].pathTracingKind=0;pt::CpuScene imported(world);imported.sunDirection=scene.sunDirection;imported.sunIrradiance=scene.sunIrradiance;imported.sunRadius=scene.sunRadius;
    check(imported.proceduralCount(3)==0&&imported.dielectricCount()==1,"Imported pool fixture incorrectly acquired FFT semantics");
    pt::Random random(12345);double importedMean=0;uint64_t rays=0;
    for(int i=0;i<samples;++i)importedMean+=imported.trace({0,1,2},{0,0,-1},random,2,rays,nullptr,true).x;
    check(std::abs(importedMean/samples-expected)<.006,"Imported dielectric pool missed reflection proposal or changed its expectation");
}
void thinSolarReflection() {
    auto world=pt::makeThinSolarValidationScene(8,8).snapshot;pt::CpuScene scene(world);
    scene.sunDirection=glm::normalize(glm::vec3(0,.2f,1));scene.sunIrradiance=glm::vec3(1);scene.sunRadius=.01f;
    check(scene.thinSolarNormals().size()==2&&scene.media().empty(),"Thin orientation catalog duplicated parallel panes or created a medium");
    double expected=0;constexpr int nz=64,np=128;auto tangent=glm::vec3(1,0,0),bitangent=glm::cross(scene.sunDirection,tangent);float edge=std::cos(scene.sunRadius);
    for(int z=0;z<nz;++z)for(int p=0;p<np;++p){float c=glm::mix(edge,1.f,(z+.5f)/nz),r=std::sqrt(1-c*c),phi=2*pi*(p+.5f)/np;auto l=scene.sunDirection*c+(tangent*std::cos(phi)+bitangent*std::sin(phi))*r;
        float reflection=pt::thinDielectricReflectance(l.y,1,1.5f),transmission=1-pt::thinDielectricReflectance(l.z,1,1.5f);
        expected+=.8/pi*l.z*reflection*transmission*transmission*std::pow(.8,4.4)*(2*pi*(1-edge))/(pi*std::sin(scene.sunRadius)*std::sin(scene.sunRadius)*nz*np);
    }
    constexpr int samples=65536;double mean[2]{},variance[2]{};
    for(int mode=0;mode<2;++mode){pt::Random random(12345);uint64_t rays=0;for(int i=0;i<samples;++i){auto value=scene.trace({0,1,2},{0,0,-1},random,4,rays,nullptr,mode==1);check(std::isfinite(value.x)&&value.x>=0,"Thin solar proposal produced invalid energy");mean[mode]+=value.x;variance[mode]+=double(value.x)*value.x;}mean[mode]/=samples;variance[mode]=variance[mode]/samples-mean[mode]*mean[mode];}
    std::cout<<"Thin solar reflection/transmission analytic/proposal/baseline "<<expected<<" / "<<mean[1]<<" / "<<mean[0]<<", variance "<<variance[1]<<" / "<<variance[0]<<'\n';
    check(std::abs(mean[1]-expected)<.004,"Thin reflection/two-transmission proposal differs from independent disc integral");
    check(variance[1]<variance[0]*.02,"Thin solar proposal did not reduce reflection-chain variance");
    pt::CpuScene overlap(pt::makeThinSolarValidationScene(8,8,true).snapshot);overlap.sunDirection=scene.sunDirection;overlap.sunIrradiance=scene.sunIrradiance;overlap.sunRadius=scene.sunRadius;
    check(overlap.thinSolarNormals().size()==3,"Transformed/mirrored sheet orientation was lost");
    pt::Random random(12345);uint64_t rays=0;double sum=0;for(int i=0;i<samples;++i)sum+=overlap.trace({0,1,2},{0,0,-1},random,4,rays).x;
    check(std::abs(sum/samples-expected)<.004,"Overlapping solar cones used selected-component rather than full mixture density");
    // Exercise environment NEE/MIS with the reflection proposal active but a dark sun.
    auto furnaceWorld=snapshot();auto wall=triangle({-1000,-1000,0},{1000,-1000,0},{0,1000,0});wall.pathTracingBsdfModel=1;
    auto pane=triangle({-1000,-1000,3},{1000,-1000,3},{0,1000,3});pane.pathTracingKind=5;pane.pathTracingIor=1.5f;
    furnaceWorld.draws={wall,pane};pt::CpuScene furnace(furnaceWorld);furnace.environment=std::make_shared<pt::Environment>(8,4,std::vector<glm::vec3>(32,glm::vec3(1)));furnace.sunRadius=.01f;furnace.sunDirection=glm::normalize(glm::vec3(0,.2f,-1));
    glm::vec3 energy(0);for(uint32_t i=0;i<32768;++i){auto r=pt::Random::forPixel(1,0,i,true);energy+=furnace.trace({0,0,2},{0,0,-1},r,32,rays);}
    check(near(energy/32768.f,glm::vec3(1),.035f),"Thin solar proposal broke straight sheet environment NEE/MIS");
    auto capped=world;for(int i=0;i<12;++i){auto hint=world.draws[0];hint.model=glm::translate(glm::mat4(1),glm::vec3(20000+i*2000,0,0))*glm::rotate(glm::mat4(1),.1f*(i+1),glm::vec3(1,0,0));capped.draws.push_back(hint);}pt::CpuScene limited(capped);
    check(limited.thinSolarNormals().size()==8,"Sheet orientation catalog exceeded the GPU ABI limit");
}
void roughDielectric() {
    for(bool front:{true,false})for(float roughness:{.35f,.65f}){
        pt::Surface s;s.normal=s.geometricNormal={0,0,1};s.ior=1.54f;s.frontFace=front;s.transmissionRoughness=roughness;
        glm::vec3 view=glm::normalize(glm::vec3(.7f,0,1));pt::Random rng(813);double accepted=0,pdfIntegral=0,sampleEnergy=0,integralEnergy=0;
        constexpr int samples=150000;for(int i=0;i<samples;++i){auto sample=pt::sampleBsdf(s,view,rng,pt::TransportMode::Importance);if(sample.pdf<=0)continue;++accepted;
            check(!sample.delta&&sample.transmission==(sample.direction.z<0),"Rough boundary flags or medium transition hemisphere differ");
            check(std::abs(sample.pdf-pt::bsdfPdf(s,view,sample.direction))<1e-5f,"Rough dielectric sample/pdf disagree");
            auto weight=sample.value*std::abs(sample.direction.z)/sample.pdf;check(std::isfinite(weight.x)&&weight.x>=0&&weight.x<=1.0001f,"Single-scatter dielectric gains energy");sampleEnergy+=weight.x;
            pt::Surface reverse=s;if(sample.transmission){reverse.frontFace=!front;reverse.normal=reverse.geometricNormal=-s.normal;}
            auto reciprocal=pt::evaluateBsdf(reverse,sample.direction,view,pt::TransportMode::Radiance);
            if(glm::length(reciprocal-sample.value)>=.003f*std::max(1.f,sample.value.x))std::cerr<<"Reciprocity r "<<roughness<<", front "<<front<<", trans "<<sample.transmission<<", direction "<<sample.direction.x<<", "<<sample.direction.y<<", "<<sample.direction.z<<", value "<<sample.value.x<<", reverse "<<reciprocal.x<<'\n';
            check(glm::length(reciprocal-sample.value)<.003f*std::max(1.f,sample.value.x),"Rough transmission radiance/importance reciprocity failed");
        }
        // Independent equal-solid-angle quadrature, not samples from the tested PDF.
        constexpr int nz=768,np=1024;for(int z=0;z<nz;++z)for(int p=0;p<np;++p){float cosine=-1+2.f*(z+.5f)/nz,phi=2*pi*(p+.5f)/np,sine=std::sqrt(1-cosine*cosine);glm::vec3 l{sine*std::cos(phi),sine*std::sin(phi),cosine};pdfIntegral+=pt::bsdfPdf(s,view,l)*4*pi/(nz*np);integralEnergy+=pt::evaluateBsdf(s,view,l,pt::TransportMode::Importance).x*std::abs(cosine)*4*pi/(nz*np);}
        std::cout<<"Rough dielectric front "<<front<<", r "<<roughness<<", accepted/pdf "<<accepted/samples<<" / "<<pdfIntegral<<", energy sample/integral "<<sampleEnergy/samples<<" / "<<integralEnergy<<'\n';
        check(std::abs(pdfIntegral-accepted/samples)<.025,"Rough dielectric PDF integral misses reflection/transmission mass");
        check(std::abs(integralEnergy-sampleEnergy/samples)<.025,"Rough dielectric estimator disagrees with independent integration");
    }
    pt::Surface matched;matched.normal=matched.geometricNormal={0,0,1};matched.ior=matched.exteriorIor=1.54f;matched.transmissionRoughness=.5f;pt::Random random(18);auto pass=pt::sampleBsdf(matched,{0,0,1},random);check(pass.delta&&pass.transmission&&near(pass.direction,{0,0,-1}),"Matched IOR did not become transparent delta boundary");
    auto source=pt::makeCausticsScene(8,8);source.snapshot.draws.back().pathTracingIor=1.54f;source.snapshot.draws.back().pathTracingRoughness=.4f;pt::CpuScene scene(source.snapshot);check(scene.exportData().materials.back().absorption.w==.4f,"Dielectric roughness ABI mismatch");bool rejected=false;try{scene.validateBidirectional();}catch(const std::invalid_argument &){rejected=true;}check(rejected,"BDPT accepted unimplemented rough strategy densities");
    for(float invalid:{-1.f,1.1f,NAN}){source.snapshot.draws.back().pathTracingRoughness=invalid;rejected=false;try{pt::CpuScene bad(source.snapshot);}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Invalid dielectric roughness accepted");}
    // From the center of an absorbing cube, the first exit is one unit away.
    // Its transmitted environment connection lies in air, not the cube medium.
    auto box=pt::makeMediumValidationScene(8,8,true).snapshot;box.draws[0].pathTracingScattering=glm::vec3(0);box.draws[0].pathTracingAbsorption=glm::vec3(.4f);box.draws[0].pathTracingRoughness=.65f;
    pt::CpuScene exit(box);exit.environment=std::make_shared<pt::Environment>(8,4,std::vector<glm::vec3>(32,glm::vec3(1)));
    pt::Surface face;check(exit.intersect({0,0,0},{0,0,1},0,2,face),"Rough exit fixture missed boundary");face.exteriorIor=1;
    double expected=0,observed=0;constexpr int nz=512,np=512;
    for(int z=0;z<nz;++z)for(int p=0;p<np;++p){float cosine=(z+.5f)/nz,phi=2*pi*(p+.5f)/np,sine=std::sqrt(1-cosine*cosine);glm::vec3 l{sine*std::cos(phi),sine*std::sin(phi),cosine};expected+=pt::evaluateBsdf(face,{0,0,-1},l).x*cosine*2*pi/(nz*np)*std::exp(-.4f);}
    uint64_t rays=0;for(int i=0;i<60000;++i){pt::Random rng(i+101);observed+=exit.trace({0,0,0},{0,0,1},rng,1,rays).x/60000;}
    std::cout<<"Rough exit medium NEE sample/reference "<<observed<<" / "<<expected<<'\n';check(std::abs(observed-expected)<.015,"Transmitted NEE attenuated an air shadow segment with interior extinction");
}
void geometry() {
    auto s=snapshot();s.draws.push_back(triangle({-1,-1,0},{1,-1,0},{0,1,0},{.5f,.2f,.1f}));pt::CpuScene scene(s);pt::Surface hit;
    check(scene.meshCount()==1 && scene.triangles()==1,"Single-mesh objects must be imported");
    check(scene.intersect({0,0,2},{0,0,-1},0,10,hit),"Planar triangle AABB missed");check(std::abs(hit.distance-2)<1e-6f,"Triangle distance mismatch");
    check(near(hit.albedo,glm::pow(glm::vec3(.5f,.2f,.1f),glm::vec3(2.2f))),"Albedo factor / encoded-to-linear conversion mismatch");
    check(!scene.intersect({0,0,0},{1,0,0},0,10,hit),"Parallel ray accepted as a NaN hit");
    s.draws[0].model=glm::translate(glm::mat4(1),glm::vec3(2,0,0))*glm::scale(glm::mat4(1),glm::vec3(2,.5f,1));pt::CpuScene transformed(s);
    check(transformed.intersect({2,0,2},{0,0,-1},0,10,hit) && near(hit.normal,{0,0,1}),"Transform / inverse transpose normal mismatch");
    auto image=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{2,1,{255,0,0,255,0,255,0,0}});auto material=std::make_shared<render::MaterialPayload>();material->images[0]=image;
    s.draws[0].model=glm::mat4(1);s.draws[0].material=material;s.draws[0].parameters.albedoAlpha=glm::vec4(1);s.draws[0].parameters.factors.w=.5f;pt::CpuScene opaque(s);
    check(opaque.intersect({0,0,2},{0,0,-1},0,10,hit) && near(hit.albedo,{1,0,0}),"Texture orientation / alpha opaque texel mismatch");
    auto mesh=std::make_shared<render::MeshPayload>(*s.draws[0].mesh);for(auto &v:mesh->vertices)v.uv.x=.75f;s.draws[0].mesh=mesh;pt::CpuScene masked(s);check(!masked.intersect({0,0,2},{0,0,-1},0,10,hit),"Alpha mask did not skip geometry");
    auto mapped=std::make_shared<render::MaterialPayload>();mapped->images[1]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{1,1,{255,128,255,255}});mapped->images[2]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{1,1,{0,0,128,255}});mapped->images[3]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{1,1,{0,64,0,255}});
    auto uvMesh=std::make_shared<render::MeshPayload>(*s.draws[0].mesh);uvMesh->vertices[0].uv={0,0};uvMesh->vertices[1].uv={1,0};uvMesh->vertices[2].uv={.5f,1};s.draws[0].mesh=uvMesh;s.draws[0].material=mapped;s.draws[0].parameters.factors={1,1,1,0};s.draws[0].parameters.emissiveNormal.w=1;pt::CpuScene normal(s);
    check(normal.intersect({0,0,2},{0,0,-1},0,10,hit) && hit.normal.x>.6f && hit.normal.z>.6f,"UV-derived normal-map tangent basis failed");check(std::abs(hit.metallic-128/255.f)<1e-5f && std::abs(hit.roughness-64/255.f)<1e-5f,"Packed metallic B / roughness G conversion failed");
    auto invalidImage=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{2,2,{0,0,0,255}});auto invalidMaterial=std::make_shared<render::MaterialPayload>();invalidMaterial->images[0]=invalidImage;s.draws[0].material=invalidMaterial;bool rejectedImage=false;try{pt::CpuScene invalid(s);}catch(const std::invalid_argument &){rejectedImage=true;}check(rejectedImage,"Truncated CPU material image was accepted");
    s.draws.clear();pt::Random r(9);
    for(int i=0;i<300;++i){glm::vec3 a{r.uniform()*8-4,r.uniform()*8-4,r.uniform()*8-4};s.draws.push_back(triangle(a,a+glm::vec3(.4f,0,.1f),a+glm::vec3(0,.5f,0)));}
    pt::CpuScene bvh(s);
    for(int i=0;i<10000;++i){glm::vec3 origin{r.uniform()*12-6,r.uniform()*12-6,r.uniform()*12-6};glm::vec3 direction=glm::normalize(glm::vec3(r.uniform()-.5f,r.uniform()-.5f,r.uniform()-.5f));pt::Surface fast,reference;bool a=bvh.intersect(origin,direction,1e-4f,100,fast),b=bvh.intersect(origin,direction,1e-4f,100,reference,true);check(a==b && (!a || std::abs(fast.distance-reference.distance)<1e-4f),"SAH BVH differs from brute-force triangles");}
    pt::CpuScene empty(snapshot());check(empty.nodeCount()==0 && !empty.intersect({0,0,0},{0,0,1},0,10,hit),"Empty scene acceleration failed");
}
void environment() {
    pt::Environment uniform(32,16,std::vector<glm::vec3>(512,glm::vec3(2)));pt::Random rng(123);glm::vec3 mean(0);
    for(int i=0;i<50000;++i){auto sample=uniform.sample(rng);check(std::abs(sample.pdf-1/(4*pi))<1e-5f,"Uniform HDR solid-angle PDF mismatch");check(std::abs(sample.pdf-uniform.pdf(sample.direction))<1e-5f,"Sample and evaluate environment PDF disagree");mean+=sample.direction;}
    check(glm::length(mean/50000.f)<.02f,"Environment sampling is not uniform in solid angle");
    std::vector<glm::vec3> pixels(32*16,glm::vec3(.01f));for(int y=4;y<8;++y)for(int x=8;x<12;++x)pixels[y*32+x]=glm::vec3(10);pt::Environment bright(32,16,pixels);double normalization=0;
    for(int y=0;y<16;++y)for(int x=0;x<32;++x){float theta=(y+.5f)/16*pi,phi=(x+.5f)/32*2*pi;glm::vec3 d{std::sin(theta)*std::sin(phi),std::cos(theta),-std::sin(theta)*std::cos(phi)};double solid=2*pi/32*(std::cos(pi*y/16)-std::cos(pi*(y+1)/16));normalization+=bright.pdf(d)*solid;}
    check(std::abs(normalization-1)<1e-5,"Nonuniform environment PDF does not integrate to one");
    int chosen=0;for(int i=0;i<10000;++i){auto sample=bright.sample(rng);check(std::abs(sample.pdf-bright.pdf(sample.direction))<1e-3f,"HDR importance PDF mismatch");if(sample.radiance.x>1)++chosen;}check(chosen>9000,"HDR importance sampler missed the bright patch");
    check(near(uniform.evaluate({0,1,0}),{2,2,2}),"Environment pole sampling failed");
}
void bsdf() {
    pt::Surface s;s.albedo={.8f,.4f,.1f};s.roughness=.6f;s.normal=s.geometricNormal={0,0,1};pt::Random rng(817);
    for(float metallic:{0.f,1.f}) {s.metallic=metallic;glm::vec3 estimate(0),integral(0);
        for(int i=0;i<100000;++i){auto sample=pt::sampleBsdf(s,{0,0,1},rng);if(sample.pdf>0){check(std::abs(sample.pdf-pt::bsdfPdf(s,{0,0,1},sample.direction))<1e-5f,"BSDF sample PDF mismatch");estimate+=sample.value*(std::max(0.f,sample.direction.z)/sample.pdf);}const float z=rng.uniform(),phi=2*pi*rng.uniform(),r=std::sqrt(1-z*z);glm::vec3 d{r*std::cos(phi),r*std::sin(phi),z};integral+=pt::evaluateBsdf(s,{0,0,1},d)*(z*2*pi);}
        estimate/=100000.f;integral/=100000.f;check(near(estimate,integral,.015f),"GGX/diffuse sample estimator does not match independent hemispherical integration");check(glm::all(glm::lessThanEqual(estimate,glm::vec3(1.02f))),"White furnace produces energy gain");
    }
    s.metallic=1;s.roughness=.55f;glm::vec3 view=glm::normalize(glm::vec3(1,0,.12f)),estimate(0),integral(0);
    for(int i=0;i<150000;++i){auto sample=pt::sampleBsdf(s,view,rng);if(sample.pdf>0){check(std::abs(sample.pdf-pt::bsdfPdf(s,view,sample.direction))<1e-5f,"Grazing VNDF PDF mismatch");estimate+=sample.value*(std::max(0.f,sample.direction.z)/sample.pdf);}float z=rng.uniform(),phi=2*pi*rng.uniform(),q=std::sqrt(1-z*z);integral+=pt::evaluateBsdf(s,view,{q*std::cos(phi),q*std::sin(phi),z})*(z*2*pi);}
    check(near(estimate/150000.f,integral/150000.f,.015f),"Grazing VNDF estimator loses energy");
    check(near(pt::evaluateBsdf(s,{0,0,1},{0,0,-1}),{0,0,0}),"BSDF leaks below the geometric surface");
}
void anisotropicBsdf(){
    pt::Surface s;s.normal=s.geometricNormal={0,0,1};s.specularTangent={1,0,0,1};s.albedo={.8f,.6f,.3f};s.roughness=.55f;
    pt::Random rng(89173);
    for(uint32_t model:{0u,2u})for(float metallic:{0.f,1.f})for(float anisotropy:{-.8f,.8f})for(auto view:std::vector<glm::vec3>{{0,0,1},glm::normalize(glm::vec3(1,.2f,.15f))}){
        s.bsdfModel=model;s.metallic=metallic;s.specularAnisotropy=anisotropy;s.specularRotation=.37f;
        glm::vec3 estimate(0),integral(0);double pdfIntegral=0;int accepted=0;constexpr int samples=65536;
        for(int i=0;i<samples;++i){auto sample=pt::sampleBsdf(s,view,rng);if(sample.pdf>0){++accepted;check(std::isfinite(sample.pdf)&&near(sample.value,pt::evaluateBsdf(s,view,sample.direction),1e-5f),"Anisotropic sample evaluation is inconsistent");check(std::abs(sample.pdf-pt::bsdfPdf(s,view,sample.direction))<1e-5f,"Anisotropic VNDF PDF mismatch");estimate+=sample.value*(sample.direction.z/sample.pdf);}
            const float z=rng.uniform(),phi=2*pi*rng.uniform(),r=std::sqrt(1-z*z);glm::vec3 l{r*std::cos(phi),r*std::sin(phi),z};integral+=pt::evaluateBsdf(s,view,l)*(z*2*pi);pdfIntegral+=pt::bsdfPdf(s,view,l)*2*pi;
        }
        check(near(estimate/float(samples),integral/float(samples),.03f),"Anisotropic sampling disagrees with independent hemispherical integration");
        check(std::abs(pdfIntegral/samples-double(accepted)/samples)<.02,"Anisotropic PDF mass disagrees with accepted sampling frequency");
        check(glm::all(glm::lessThanEqual(estimate/float(samples),glm::vec3(1.015f))),"Anisotropic white furnace gains energy");
        const auto l=glm::normalize(glm::vec3(.3f,.4f,1));check(near(pt::evaluateBsdf(s,view,l),pt::evaluateBsdf(s,l,view),1e-5f),"Anisotropic reflection violates reciprocity");
    }
    s.bsdfModel=0;s.metallic=1;s.specularRotation=0;s.specularAnisotropy=.8f;const glm::vec3 view{0,0,1},l=glm::normalize(glm::vec3(.4f,.2f,1));auto positive=pt::evaluateBsdf(s,view,l);s.specularAnisotropy=-.8f;s.specularRotation=pi/2;
    check(near(positive,pt::evaluateBsdf(s,view,l),1e-5f),"Signed anisotropy axis swap differs from 90 degree rotation");
    s.specularAnisotropy=0;s.specularRotation=0;auto isotropic=pt::evaluateBsdf(s,view,l);s.specularRotation=1.23f;s.specularTangent={0,1,0,-1};check(isotropic==pt::evaluateBsdf(s,view,l),"Zero anisotropy depends on tangent or rotation");
    auto world=snapshot();auto draw=triangle({-2,-2,0},{2,-2,0},{0,2,0});auto oriented=std::make_shared<render::MeshPayload>(*draw.mesh);oriented->vertices[0].uv={0,0};oriented->vertices[1].uv={1,0};oriented->vertices[2].uv={.5f,1};draw.mesh=oriented;draw.pathTracingSpecularAnisotropy=.8f;draw.pathTracingSpecularRotation=.3f;draw.model=glm::scale(glm::mat4(1),glm::vec3(-2,1,.5f));world.draws.push_back(draw);pt::CpuScene scene(world);pt::Surface hit;
    check(scene.intersect({0,0,2},{0,0,-1},0,10,hit)&&std::abs(hit.specularTangent.x+1)<1e-6f&&hit.specularTangent.w==-1,"Mirrored UV anisotropic frame was not preserved");
    auto packed=scene.exportData();check(packed.materials[0].specularShape==glm::vec4(.8f,.3f,0,0),"Packed specular parameters were lost");
    auto common=world;common.draws[0].extension.specularShape={-.6f,.7f,2,0};
    pt::CpuScene commonScene(common);const auto commonPacked=commonScene.exportData();
    check(commonPacked.materials[0].specularShape==glm::vec4(-.6f,.7f,0,0),"Common GGX parameters did not reach PT");
    check(!(pt::sceneIdentity(common)==pt::sceneIdentity(world)),"Common GGX parameters did not invalidate PT accumulation");
    const auto identity=pt::sceneIdentity(world);auto changed=world;changed.draws[0].pathTracingSpecularRotation+=.1f;check(!(identity==pt::sceneIdentity(changed)),"Specular rotation did not invalidate transport identity");changed=world;changed.draws[0].pathTracingSpecularAnisotropy=-.8f;check(!(identity==pt::sceneIdentity(changed)),"Specular axes did not invalidate transport identity");

    world.draws[0].pathTracingSpecularAnisotropy=1;bool rejected=false;try{pt::CpuScene invalid(world);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Unsupported anisotropy magnitude accepted");
    world.draws[0].pathTracingSpecularAnisotropy=.8f;rejected=false;try{pt::CpuScene invalid(world,{{draw.objectId,1.5f}});}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Legacy dielectric annotation silently ignored specular anisotropy");
    auto room=snapshot();auto receiver=triangle({-20,-20,0},{20,-20,0},{0,20,0},{.7f,.6f,.4f});receiver.parameters.factors={.6f,.5f,1,0};receiver.pathTracingSpecularAnisotropy=.8f;receiver.pathTracingSpecularRotation=.4f;room.draws.push_back(receiver);
    auto emitter=triangle({-.3f,-.3f,1},{0,.3f,1},{.3f,-.3f,1});emitter.parameters.albedoAlpha={0,0,0,1};emitter.parameters.emissiveNormal={6,6,6,0};room.draws.push_back(emitter);pt::CpuScene lit(room);pt::Options options;options.width=options.height=8;options.samples=2048;options.maxDepth=2;options.threads=2;options.adaptive=false;
    auto reference=pt::render(lit,options);options.bdpt=true;auto bidirectional=pt::render(lit,options);double a=0,b=0;for(size_t i=0;i<reference.radiance.size();++i){a+=reference.radiance[i].x;b+=bidirectional.radiance[i].x;}
    check(a>0&&std::abs(b/a-1)<.05,"Anisotropic BDPT reverse-density / connection energy differs from PT");

}
void transport() {
    auto s=snapshot();s.draws.push_back(triangle({-20,-20,0},{20,-20,0},{0,20,0},{.7f,.25f,.1f}));pt::CpuScene scene(s);
    scene.environment=std::make_shared<pt::Environment>(16,8,std::vector<glm::vec3>(128,glm::vec3(1)));
    pt::Random r(58);glm::vec3 result(0),reference(0);uint64_t rays=0;
    pt::Surface surface;check(scene.intersect({0,0,2},{0,0,-1},1e-4f,10,surface),"Transport fixture missed");
    for(int i=0;i<50000;++i){result+=scene.trace({0,0,2},{0,0,-1},r,1,rays);const float z=r.uniform(),phi=2*pi*r.uniform(),q=std::sqrt(1-z*z);reference+=pt::evaluateBsdf(surface,{0,0,1},{q*std::cos(phi),q*std::sin(phi),z})*(z*2*pi);}
    check(near(result/50000.f,reference/50000.f,.007f),"Environment NEE + BSDF MIS double counts or loses energy");
    s.frame.lights.push_back({{0,0,0,0},{2,2,2,0},{0,0,-1,0}});pt::CpuScene visible(s);glm::vec3 lit=visible.trace({0,0,2},{0,0,-1},r,1,rays);
    s.draws.push_back(triangle({-.5f,-.5f,1},{.5f,-.5f,1},{0,.5f,1}));pt::CpuScene blocked(s);auto dark=blocked.trace({0,0,.5f},{0,0,-1},r,1,rays);check(lit.r>.05f && dark.r<1e-6f,"Direct-light visibility failed");
}
void rendering() {
    auto s=snapshot();s.draws.push_back(triangle({-20,-20,0},{20,-20,0},{0,20,0},{.7f,.25f,.1f}));
    s.frame.lights.push_back({{0,0,0,0},{3,3,3,0},{0,0,-1,0}});pt::CpuScene scene(s);scene.environment=std::make_shared<pt::Environment>(16,8,std::vector<glm::vec3>(128,glm::vec3(.3f)));
    pt::Options options;options.width=32;options.height=24;options.samples=20;options.maxDepth=4;options.threads=1;
    int callbacks=0;auto first=pt::render(scene,options,[&](const auto &image){++callbacks;check(image.samples==4 || image.samples==16 || image.samples==20,"Unexpected progressive sample count");});options.threads=3;auto second=pt::render(scene,options);
    check(callbacks==3 && first.samples==20 && first.radiance==second.radiance,"Renderer depends on worker scheduling or misses tiles");
    options.adaptive=false;options.samples=65;auto checkpointReference=pt::render(scene,options);
    for(uint32_t interval:{1u,32u}){options.checkpointSamples=interval;auto frequent=pt::render(scene,options);check(frequent.radiance==checkpointReference.radiance&&frequent.sampleCounts==checkpointReference.sampleCounts&&frequent.rays==checkpointReference.rays,"Fixed-spp checkpoint frequency changed transport samples");}
    options.checkpointSamples=0;bool invalidCheckpoint=false;try{pt::render(scene,options);}catch(const std::invalid_argument &){invalidCheckpoint=true;}check(invalidCheckpoint,"Zero checkpoint interval accepted");options.checkpointSamples=256;options.samples=20;
    check(first.nonFiniteSamples==0 && first.radiance[0].r>first.radiance[0].g*2,"Material colors lost during transport");
    pt::writeImage(first,1,"build/path-tracing/tests/fixture");pt::writeReport(first,scene,options,"build/path-tracing/tests/fixture","fixture");check(std::filesystem::file_size("build/path-tracing/tests/fixture.pfm")==size_t(32*24*12+14),"PFM float layout/header mismatch");
    bool rejected=false;options.samples=0;try{pt::render(scene,options);}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Zero samples were accepted");
    options.samples=256;options.width=16;options.height=16;options.minimumSamples=32;options.adaptive=true;options.threads=1;
    pt::CpuScene empty(snapshot());empty.environment=std::make_shared<pt::Environment>(8,4,std::vector<glm::vec3>(32,glm::vec3(.5f)));auto adaptive=pt::render(empty,options);options.threads=3;auto again=pt::render(empty,options);
    check(adaptive.convergedPixels==256 && adaptive.totalSamples<uint64_t(256)*256,"Adaptive pixels did not stop on constant radiance");check(adaptive.sampleCounts==again.sampleCounts && adaptive.radiance==again.radiance,"Adaptive stopping depends on thread count");for(auto value:adaptive.radiance)check(near(value,glm::vec3(.5f),1e-6f),"Adaptive mean changed constant environment");
    options.adaptive=false;auto fixed=pt::render(empty,options);check(fixed.totalSamples==uint64_t(256)*256 && fixed.radiance==adaptive.radiance,"Fixed spp reference lost samples");
    // An emissive triangle must illuminate a receiving surface through explicit light sampling.
    s.frame.lights.clear();auto emitter=triangle({-.5f,-.5f,1},{0,.5f,1},{.5f,-.5f,1});emitter.parameters.emissiveNormal={10,10,10,0};s.draws.push_back(emitter);pt::CpuScene area(s);pt::Random r(33);glm::vec3 energy(0);uint64_t rays=0;
    for(int i=0;i<1000;++i)energy+=area.trace({.8f,0,.5f},{0,0,-1},r,1,rays);check(energy.r/1000>.02f,"Emissive triangle NEE produced no illumination");
}
void fiberMaterials(){
    pt::Surface hair;hair.bsdfModel=5;hair.normal=hair.geometricNormal={0,0,1};hair.specularTangent={1,0,0,1};hair.albedo=glm::vec3(1);hair.fiber.w=0;
    pt::Random rng(918);auto view=glm::normalize(glm::vec3(.3f,.2f,1));glm::vec3 energy(0);
    for(int i=0;i<20000;++i){auto sample=pt::sampleBsdf(hair,view,rng);check(sample.pdf>0&&std::isfinite(sample.pdf),"Hair sample PDF invalid");
        check(glm::length(sample.direction)> .999f&&glm::length(sample.direction)<1.001f,"Hair direction not unit");
        check(near(sample.value,pt::evaluateBsdf(hair,view,sample.direction),1e-5f)&&std::abs(sample.pdf-pt::bsdfPdf(hair,view,sample.direction))<1e-5f,"Hair sampling/evaluation mismatch");
        energy+=sample.value*(pt::surfaceCosine(hair,sample.direction)/sample.pdf);}
    check(near(energy/20000.f,glm::vec3(1),.002f),"Unabsorbing hair furnace loses/gains energy");
    double integratedPdf=0;glm::vec3 integral(0);const int longitudinal=160,azimuth=320;
    for(int i=0;i<longitudinal;++i)for(int j=0;j<azimuth;++j){float si=2*(i+.5f)/longitudinal-1,phi=2*pi*(j+.5f)/azimuth,ci=std::sqrt(1-si*si);glm::vec3 l{si,ci*std::cos(phi),ci*std::sin(phi)};
        integratedPdf+=pt::bsdfPdf(hair,view,l)*4*pi/(longitudinal*azimuth);integral+=pt::evaluateBsdf(hair,view,l)*(pt::surfaceCosine(hair,l)*4*pi/(longitudinal*azimuth));}
    check(std::abs(integratedPdf-1)<.01&&near(integral,glm::vec3(1),.015f),"Independent hair sphere quadrature not normalized");
    // Narrow/broad lobes, ribbon edges and tilted near-longitudinal views must
    // remain finite, including the limits used by imported thin geometry.
    for(float beta:{.1f,.8f})for(float offset:{-.999f,.999f})for(float tilt:{-.2f,.2f})for(float x:{-.999f,0.f,.999f}){
        auto edge=hair;edge.fiber={beta,beta,1.55f,tilt};edge.hairOffset=offset;
        auto v=glm::vec3(x,0,std::sqrt(1-x*x));glm::vec3 sum(0);
        for(int i=0;i<1000;++i){auto sample=pt::sampleBsdf(edge,v,rng);auto value=sample.value*(pt::surfaceCosine(edge,sample.direction)/sample.pdf);
            check(sample.pdf>0&&std::isfinite(sample.pdf)&&std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z),"Hair edge/grazing sample nonfinite");sum+=value;}
        check(near(sum/1000.f,glm::vec3(1),.003f),"Hair edge/grazing white furnace changed energy");
    }
    hair.albedo={.6f,.2f,.04f};energy=glm::vec3(0);
    for(int i=0;i<10000;++i){auto sample=pt::sampleBsdf(hair,view,rng);energy+=sample.value*(pt::surfaceCosine(hair,sample.direction)/sample.pdf);}
    energy/=10000.f;check(energy.x>energy.y&&energy.y>energy.z&&energy.x<1,"Hair absorption does not tint/bound scattering");
    for(float nv:{.01f,.2f,1.f})for(float w:{0.f,.8f}){
        pt::Surface cloth;cloth.bsdfModel=4;cloth.normal=cloth.geometricNormal={0,0,1};cloth.albedo=glm::vec3(1);cloth.sheen={w,.5f,.3f,0};cloth.roughness=.8f;
        auto v=glm::normalize(glm::vec3(std::sqrt(1-nv*nv),0,nv));glm::vec3 sum(0);double pdf=0;
        for(int i=0;i<80;++i)for(int j=0;j<160;++j){float z=2*(i+.5f)/80-1,phi=2*pi*(j+.5f)/160,r=std::sqrt(1-z*z);glm::vec3 l{r*std::cos(phi),r*std::sin(phi),z};
            sum+=pt::evaluateBsdf(cloth,v,l)*(pt::surfaceCosine(cloth,l)*4*pi/(80*160));pdf+=pt::bsdfPdf(cloth,v,l)*4*pi/(80*160);
            if(z>0)check(near(pt::evaluateBsdf(cloth,v,l),pt::evaluateBsdf(cloth,l,v),1e-5f),"Cloth violates reciprocity");}
        check(glm::all(glm::lessThanEqual(sum,glm::vec3(1.01f)))&&std::abs(pdf-1)<.002,"Cloth mixture furnace/PDF invalid");
    }
    for(int i=0;i<400;++i){glm::vec2 uv{float(i)*.0137f,float(i)*.0191f};auto pattern=pt::fiber::fiberKnit(uv,{1,24,32,.12f});auto repeat=pt::fiber::fiberKnit(uv+glm::vec2(1.f/24,1.f/32),{1,24,32,.12f});
        check(pattern.x>=.32f&&pattern.x<=1&&std::abs(glm::length(glm::vec2(pattern.y,pattern.z))-1)<1e-5f&&glm::length(pattern-repeat)<.001f,"Knit yarn pattern is unbounded/nonperiodic");}
    pt::Surface knit;knit.bsdfModel=4;knit.normal=knit.geometricNormal={0,0,1};knit.albedo=glm::vec3(1);knit.sheen={.8f,.5f,.1f,0};knit.knitSurface=pt::fiber::fiberKnit({.015f,.02f},{1,24,32,.12f});
    auto grazing=glm::normalize(glm::vec3(1,0,.02f));energy=glm::vec3(0);
    for(int i=0;i<10000;++i){auto sample=pt::sampleBsdf(knit,grazing,rng);energy+=sample.value*(pt::surfaceCosine(knit,sample.direction)/sample.pdf);if(sample.direction.z>0)check(near(sample.value,pt::evaluateBsdf(knit,sample.direction,grazing),1e-5f),"Directional knit sheen not reciprocal");}
    check(glm::all(glm::lessThanEqual(energy/10000.f,glm::vec3(1.01f))),"Knit white furnace gained energy");
    // Integrate reflection + transmission independently of the sampler. The
    // new yarn lobe must remain reciprocal and passive, including grazing views.
    for(float rough:{.3f,.65f,1.f})for(float nv:{.05f,.4f,1.f})for(float weight:{.25f,1.f}){
        auto yarn=knit;yarn.sheen={.2f,.7f,.15f,weight};yarn.roughness=rough;yarn.knitSurface={1,1,0,1};
        auto v=glm::vec3(std::sqrt(1-nv*nv),0,nv);glm::vec3 sum(0);
        for(int i=0;i<180;++i)for(int j=0;j<360;++j){float z=2*(i+.5f)/180-1,phi=2*pi*(j+.5f)/360,r=std::sqrt(1-z*z);glm::vec3 l{r*std::cos(phi),r*std::sin(phi),z};
            auto f=pt::evaluateBsdf(yarn,v,l);check(glm::all(glm::greaterThanEqual(f,glm::vec3(0)))&&std::isfinite(f.x),"Yarn reflection became nonfinite/negative");
            sum+=f*(std::abs(z)*4*pi/(180*360));
            if(z>0)check(near(f,pt::evaluateBsdf(yarn,l,v),.0003f),"Yarn reflection violated reciprocity");}
        check(glm::all(glm::lessThanEqual(sum,glm::vec3(1.01f))),"Yarn reflection furnace gained energy");
    }
    auto yarn=knit;yarn.sheen={0,.5f,0,1};yarn.roughness=.6f;yarn.knitSurface={1,1,0,1};
    auto v=glm::normalize(glm::vec3(.4f,.2f,1)),l=glm::normalize(glm::vec3(-.1f,.3f,1));auto f=pt::evaluateBsdf(yarn,v,l);
    yarn.knitSurface={1,0,1,1};auto rotate=[](glm::vec3 d){return glm::vec3(-d.y,d.x,d.z);};
    check(near(f,pt::evaluateBsdf(yarn,rotate(v),rotate(l)),1e-5f),"Yarn tangent rotation did not rotate reflection");
    check(glm::length(f-pt::evaluateBsdf(yarn,v,l))>.001f,"Yarn reflection ignored its tangent");
    yarn.sheen={0,.5f,1,1};
    for(int i=0;i<1000;++i){auto sample=pt::sampleBsdf(yarn,{0,0,1},rng);
        check(sample.direction.z>0&&sample.pdf>0&&near(sample.value,pt::evaluateBsdf(yarn,{0,0,1},sample.direction)),"Fully specular yarn used a transmission-only proposal");}
    auto world=snapshot();auto draw=triangle({-1,-1,0},{1,-1,0},{0,1,0});draw.pathTracingBsdfModel=5;draw.pathTracingFiber={.4f,.2f,1.6f,.02f};world.draws={draw};pt::CpuScene scene(world);pt::Surface hit;
    check(scene.intersect({0,0,1},{0,0,-1},0,2,hit)&&hit.bsdfModel==5&&hit.fiber==draw.pathTracingFiber&&hit.mediumId==0,"Hair geometry attributes entered a medium or disappeared");
    check(scene.exportData().materials[0].fiber==draw.pathTracingFiber&&sizeof(pt::PackedMaterial)==288,"Fiber packed ABI lost parameters");
    auto identity=pt::sceneIdentity(world);world.draws[0].pathTracingFiber.x+=.01f;check(!(pt::sceneIdentity(world)==identity),"Fiber edit did not invalidate PT identity");
    bool rejected=false;world.draws[0].pathTracingFiber.x=0;try{pt::CpuScene invalid(world);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Invalid fiber roughness accepted");
    pt::Options options;options.width=8;options.height=8;options.samples=1;options.bdpt=true;options.adaptive=false;rejected=false;
    try{pt::render(scene,options);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Unvalidated hair BDPT reverse scattering silently enabled");
    rejected=false;try{scene.validatePhotonMapping();}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Unvalidated fiber photon adjoint silently enabled");
    world.draws[0]=draw;world.draws[0].pathTracingBsdfModel=4;world.draws[0].pathTracingKnit={1,24,32,.12f};pt::CpuScene knitted(world);
    check(knitted.intersect({0,0,1},{0,0,-1},0,2,hit)&&hit.knitSurface.w==1&&knitted.exportData().materials[0].knit==world.draws[0].pathTracingKnit,"Knit evaluation/ABI lost parameters");
    identity=pt::sceneIdentity(world);world.draws[0].pathTracingKnit.y+=1;check(!(pt::sceneIdentity(world)==identity),"Knit stitch edit did not invalidate PT identity");
    identity=pt::sceneIdentity(world);world.draws[0].pathTracingSheen.w=.5f;check(!(pt::sceneIdentity(world)==identity),"Yarn reflection edit did not invalidate PT identity");
    std::cout<<"Hair/cloth: sphere PDF quadrature, sample consistency, furnace, absorption, cloth reciprocity, ABI and parameter identity passed\n";
}
void dielectricAndBdpt() {
    check(std::abs(pt::dielectricFresnel(1,1,1.5f)-.04f)<1e-6f,"Glass normal-incidence Fresnel is not 4%");
    check(pt::dielectricFresnel(.3f,1.5f,1)==1,"Glass total internal reflection failed");
    pt::Surface glass;glass.ior=1.5f;glass.normal=glass.geometricNormal={0,0,1};glass.albedo=glm::vec3(1);pt::Random random(418);glm::vec3 radiance(0),importance(0);int reflections=0;
    for(int i=0;i<25000;++i){auto a=pt::sampleBsdf(glass,{0,0,1},random),b=pt::sampleBsdf(glass,{0,0,1},random,pt::TransportMode::Importance);check(a.delta&&a.pdf>0,"Glass event must use discrete delta PDF");radiance+=a.value*(std::abs(a.direction.z)/a.pdf);importance+=b.value*(std::abs(b.direction.z)/b.pdf);reflections+=a.direction.z>0;}
    check(std::abs(float(reflections)/25000-.04f)<.006f,"Fresnel branch frequencies changed");check(near(importance/25000.f,glm::vec3(1),.001f),"Importance glass gained/lost energy");check(near(radiance/25000.f,glm::vec3(.04f+.96f/2.25f),.007f),"Radiance glass omitted eta squared");
    auto s=snapshot();s.draws.push_back(triangle({-20,-20,0},{20,-20,0},{0,20,0},{.7f,.7f,.7f}));auto emitter=triangle({-.3f,-.3f,1},{0,.3f,1},{.3f,-.3f,1});emitter.parameters.albedoAlpha={0,0,0,1};emitter.parameters.emissiveNormal={6,6,6,0};s.draws.push_back(emitter);pt::CpuScene scene(s);
    for(int y=0;y<10;++y)for(int x=0;x<10;++x){glm::vec3 o,d;float u=(x+.5f)/10,v=(y+.5f)/10;scene.cameraRay(u,v,o,d);glm::vec2 uv;float pdf;check(scene.project(o+d*2.f,uv,pdf)&&glm::length(uv-glm::vec2(u,v))<1e-5f&&pdf>0,"BDPT camera projection/directional PDF failed");}
    pt::Options options;options.width=16;options.height=16;options.samples=512;options.maxDepth=2;options.threads=2;options.adaptive=false;auto reference=pt::render(scene,options);options.bdpt=true;auto bidirectional=pt::render(scene,options);double a=0,b=0;
    for(size_t i=0;i<reference.radiance.size();++i){a+=reference.radiance[i].x;b+=bidirectional.radiance[i].x;}std::cout<<"BDPT/PT area-light energy ratio: "<<b/a<<"\n";check(std::abs(b/a-1)<.04,"BDPT connections/splats double count or lose area-light energy");
    bool rejected=false;scene.environment=std::make_shared<pt::Environment>(2,1,std::vector<glm::vec3>(2,glm::vec3(1)));try{pt::render(scene,options);}catch(const std::invalid_argument &){rejected=true;}check(rejected,"BDPT must reject unsupported HDR endpoint instead of losing energy");
    auto validation=pt::makeCausticsScene(32,24);pt::CpuScene caustics(validation.snapshot,validation.dielectrics);options.width=32;options.height=24;options.samples=64;options.maxDepth=6;auto image=pt::render(caustics,options);check(image.nonFiniteSamples==0,"BDPT caustics produced non-finite pixels");float maximum=0;for(auto value:image.radiance)maximum=std::max(maximum,value.x);check(maximum>.1f,"BDPT refractive caustic scene has no energy");
    double causticEnergy=0;for(size_t i=0;i<image.caustics.size();++i){causticEnergy+=image.caustics[i].x;check(glm::all(glm::lessThanEqual(image.caustics[i],image.radiance[i]+glm::vec3(1e-5f))),"Caustic AOV exceeds complete transport");}check(causticEnergy>.1,"BDPT did not construct specular-to-diffuse caustic paths");
    auto noGlass=pt::makeCausticsScene(32,24,false);pt::CpuScene control(noGlass.snapshot);auto clean=pt::render(control,options);for(auto value:clean.caustics)check(near(value,glm::vec3(0)),"No-glass control contains false caustic paths");
}



}
int main(){try{samplers();sceneReuse();packedExportSharing();incrementalTlas();largeInstanceUpdates();instances();comparisonClosures();cornerTangentsAndLeaves();grazingClosures();waterSolarReflection();thinSolarReflection();roughDielectric();thinDielectric();geometry();environment();bsdf();anisotropicBsdf();transport();rendering();fiberMaterials();dielectricAndBdpt();std::cout<<"CPU path tracing geometry, HDR PDF, GGX, MIS/render scheduling and output tests passed\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
