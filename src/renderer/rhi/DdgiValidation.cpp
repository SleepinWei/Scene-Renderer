#include "renderer/rhi/GpuDdgi.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "PT/GpuPathTracer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <json/json.hpp>

namespace render {
namespace {
glm::mat4 depthCorrection(){glm::mat4 m(1);m[2][2]=.5f;m[3][2]=.5f;return m;}

void check(bool result,const char* message){if(!result)throw std::runtime_error(message);}
std::shared_ptr<GpuMesh> quad(std::shared_ptr<rhi::GraphicsDevice> device,glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 d,glm::vec3 n){return std::make_shared<GpuMesh>(device,std::vector<MeshVertex>{{a,n,{0,0}},{b,n,{1,0}},{c,n,{1,1}},{d,n,{0,1}}},std::vector<uint32_t>{0,1,2,0,2,3});}
std::shared_ptr<GpuMaterial> material(std::shared_ptr<rhi::GraphicsDevice> device,glm::vec3 albedo,glm::vec3 emission={0,0,0}){MaterialDesc d;d.parameters.albedoAlpha=glm::vec4(albedo,1);d.parameters.factors={0,.7f,1,0};d.parameters.emissiveNormal=glm::vec4(emission,0);d.images[0]={1,1,{255,255,255,255}};return std::make_shared<GpuMaterial>(device,d);}
nlohmann::json rgb(glm::vec4 v){return {v.x,v.y,v.z,v.w};}
}
void validateDdgi(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory){
    using namespace rhi;DdgiSettings settings;settings.validate();auto bad=settings;bad.counts={32,32,32};bool rejected=false;try{bad.validate();}catch(const std::invalid_argument&){rejected=true;}check(rejected,"DDGI failed to enforce cache memory cap");bad=settings;bad.hysteresis=1;rejected=false;try{bad.validate();}catch(const std::invalid_argument&){rejected=true;}check(rejected,"DDGI accepted infinite convergence latency");
    const auto start=std::chrono::steady_clock::now();ForwardPbrRenderer renderer(device,directory,320,240,PbrPath::Scene);
    FrameData f;f.viewportWidth=320;f.viewportHeight=240;f.cameraPosition={0,2.8f,8};f.view=glm::lookAtRH(f.cameraPosition,glm::vec3(0,1,0),glm::vec3(0,1,0));f.viewProjection=depthCorrection()*glm::perspective(glm::radians(50.f),320.f/240.f,.1f,100.f)*f.view;f.ambient=.5f;f.ddgi.enabled=true;f.ddgi.origin={-2.25f,-.5f,-2.25f};f.ddgi.counts={4,3,4};f.ddgi.spacing=1.5f;f.ddgi.probesPerFrame=48;f.ddgi.maxDistance=20;f.ddgi.multiBounce=false;f.ddgi.hysteresis=0;
    const std::vector<glm::vec4> points{{0,1.5f,0,1},{0,1.5f,0,1},{0,1.5f,0,1},{0,1.5f,0,1},{0,1.5f,0,1},{0,1.5f,0,1}};
    const std::vector<glm::vec4> normals{{1,0,0,0},{-1,0,0,0},{0,1,0,0},{0,-1,0,0},{0,0,1,0},{0,0,-1,0}};
    renderer.render(f,{});auto uniform=renderer.sampleDdgi(points,normals);for(auto v:uniform)check(std::abs(v.x-.5f)<.018f&&std::abs(v.y-.5f)<.018f&&v.w>.99,"DDGI uniform environment quadrature differs from E/pi");
    nlohmann::json report;report["backend"]=device->backend()==Backend::Metal?"Metal":"Vulkan";report["uniform_environment"]=nlohmann::json::array();for(auto v:uniform)report["uniform_environment"].push_back(rgb(v));
    auto black=material(device,{0,0,0});auto plane=quad(device,{-100,0,-100},{100,0,-100},{100,0,100},{-100,0,100},{0,1,0});
    ++f.ddgi.resetSerial;renderer.render(f,{{plane,black,glm::mat4(1),100}});auto hemisphere=renderer.sampleDdgi(points,normals);
    check(hemisphere[2].x>.47f && hemisphere[3].x<.03f,"DDGI cubemap orientation failed analytic upper/lower hemisphere occlusion");report["hemisphere_occlusion"]={rgb(hemisphere[2]),rgb(hemisphere[3])};
    MaterialDesc mask;mask.parameters.albedoAlpha={0,0,0,1};mask.parameters.factors={0,.7f,1,.5f};mask.images[0]={1,1,{255,255,255,0}};auto cutout=std::make_shared<GpuMaterial>(device,mask);
    ++f.ddgi.resetSerial;renderer.render(f,{{plane,cutout,glm::mat4(1),101}});auto hole=renderer.sampleDdgi(points,normals);check(hole[3].x>.47f,"DDGI alpha-cutout capture sealed an empty mask");report["alpha_cutout"]=rgb(hole[3]);
    auto white=material(device,{.8f,.8f,.8f}),red=material(device,{.95f,.12f,.08f});
    std::vector<DrawPacket> room;
    auto add=[&](glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 d,glm::vec3 n,std::shared_ptr<GpuMaterial> m){room.push_back({quad(device,a,b,c,d,n),m,glm::mat4(1),room.size()+1});};
    add({-3,0,-3},{3,0,-3},{3,0,3},{-3,0,3},{0,1,0},white);
    add({-3,0,3},{-3,0,-3},{-3,4,-3},{-3,4,3},{1,0,0},red);
    add({3,0,-3},{3,0,3},{3,4,3},{3,4,-3},{-1,0,0},white);
    add({-3,0,-3},{3,0,-3},{3,4,-3},{-3,4,-3},{0,0,1},white);
    auto closed=room;closed.push_back({quad(device,{-3,4,-3},{3,4,-3},{3,4,3},{-3,4,3},{0,-1,0}),white,glm::mat4(1),8});closed.push_back({quad(device,{-3,0,3},{3,0,3},{3,4,3},{-3,4,3},{0,0,-1}),white,glm::mat4(1),9});
    ++f.ddgi.resetSerial;renderer.render(f,closed);auto sealed=renderer.sampleDdgi(points,normals);for(auto v:sealed)check(v.x<.005f && v.y<.005f && v.z<.005f && v.w>.99f,"DDGI sky leaked into a closed room");report["closed_room"]=rgb(sealed[2]);
    auto sealedFloor=renderer.sampleDdgi({{0,.05f,0,1}},{{0,1,0,0}});check(sealedFloor[0].x<.005f && sealedFloor[0].w>.99f,"Inactive probes under a closed floor reintroduced ambient sky");report["sealed_floor_coverage"]=rgb(sealedFloor[0]);
    auto instancedClosed=closed;for(auto& packet:instancedClosed)packet.mesh=GpuMesh::withInstances(packet.mesh,{glm::mat4(1)});++f.ddgi.resetSerial;renderer.render(f,instancedClosed);auto instances=renderer.sampleDdgi(points,normals);check(instances[2].x<.005f && instances[2].w>.99f,"DDGI instanced occluder capture differs");report["instanced_closed_room"]=rgb(instances[2]);
    const auto outputDir=std::getenv("SCENERENDERER_DDGI_CAPTURE_DIR");std::filesystem::path out;if(outputDir){out=outputDir;std::filesystem::create_directories(out);}
    auto capture=[&](const char* name){if(!outputDir)return;auto image=renderer.readOutput();std::ofstream ppm(out/(std::string(name)+".ppm"),std::ios::binary);ppm<<"P6\n320 240\n255\n";for(size_t i=0;i<image.size();i+=4)ppm.write(reinterpret_cast<const char*>(image.data()+i),3);check(bool(ppm),"DDGI image export failed");};
    f.ambient=.2f;f.lights={{{0,0,0,0},{6,6,6,0},{-1,-.25f,-.1f,0}}};f.shadows=true;f.shadowSettings.pcss=false;f.ddgi.enabled=false;renderer.render(f,room);const auto off=renderer.readHDR();capture("room-off");
    f.ddgi.enabled=true;f.ddgi.multiBounce=true;f.ddgi.hysteresis=.5f;++f.ddgi.resetSerial;for(int i=0;i<5;i++)renderer.render(f,room);capture("room-on");auto hdr=renderer.readHDR();double diff=0;for(size_t i=0;i<hdr.size();i++){check(std::isfinite(hdr[i])&&hdr[i]>-.01f,"DDGI produced invalid or negative HDR");diff+=std::abs(hdr[i]-off[i]);}check(diff>1,"DDGI surface pass made no change");
    const std::vector<glm::vec4> floorPoints{{-1.8f,.05f,0,1},{1.8f,.05f,0,1}};const std::vector<glm::vec4> floorNormals(2,glm::vec4(0,1,0,0));auto colour=renderer.sampleDdgi(floorPoints,floorNormals);report["colour_bleeding"]={rgb(colour[0]),rgb(colour[1])};std::cout<<"DDGI colour "<<report["colour_bleeding"]<<std::endl;check(colour[0].x>colour[0].y*1.15f && colour[0].x>colour[1].x,"DDGI red wall failed to tint nearby floor");
    // Compare the GI increment separately from the existing direct-light
    // shadow edge differences between forward and GBuffer shading.
    f.ddgi.multiBounce=false;f.ddgi.hysteresis=0;f.ddgi.enabled=false;renderer.render(f,room);const auto deferredBase=renderer.readHDR();
    f.ddgi.enabled=true;renderer.render(f,room);const auto deferred=renderer.readHDR();
    f.forwardShading=true;f.ddgi.enabled=false;renderer.render(f,room);const auto forwardBase=renderer.readHDR();
    f.ddgi.enabled=true;renderer.render(f,room);const auto forward=renderer.readHDR();float pathError=0;
    for(size_t i=0;i<forward.size();i++)pathError=std::max(pathError,std::abs((forward[i]-forwardBase[i])-(deferred[i]-deferredBase[i])));
    check(pathError<.01f,"DDGI forward/deferred GI increment differs");report["forward_deferred_max_gi_increment_error"]=pathError;f.forwardShading=false;
    // Test geometry wholly omitted from the main camera list still affects GI.
    f.ddgi.multiBounce=false;f.ddgi.hysteresis=0;++f.ddgi.resetSerial;f.lights.clear();f.shadows=false;renderer.render(f,room,1,2.2f,&closed);auto aux=renderer.sampleDdgi(points,normals);check(aux[2].x<.005f,"DDGI captured the camera-culled list instead of auxiliary geometry");
    // Thin divider with emissive source only on the right. Distances suppress
    // bright probes across the wall; disable visibility as an explicit control.
    auto divider=room;auto thinWall=material(device,{.8f,.8f,.8f});auto twoSided=thinWall->extension();twoSided.settings.w=1;thinWall->updateExtension(twoSided);auto emitter=material(device,{0,0,0},{6,1,0});divider.push_back({quad(device,{0,0,-3},{0,0,3},{0,4,3},{0,4,-3},{-1,0,0}),thinWall,glm::mat4(1),10});divider.push_back({quad(device,{2.8f,.2f,-2},{2.8f,.2f,2},{2.8f,3.8f,2},{2.8f,3.8f,-2},{-1,0,0}),emitter,glm::mat4(1),11});
    f.ambient=0;++f.ddgi.resetSerial;f.ddgi.visibility=true;renderer.render(f,divider);const std::vector<glm::vec4> leakPoints{{-.15f,1.7f,0,1},{.4f,1.7f,0,1}};const std::vector<glm::vec4> leakNormals(2,glm::vec4(0,1,0,0));auto visible=renderer.sampleDdgi(leakPoints,leakNormals);if(outputDir){auto field=renderer.readDdgiField();std::ofstream data(out/"thin-wall-field.bin",std::ios::binary);data.write(reinterpret_cast<const char*>(field.data()),field.size()*16);}
    f.ddgi.visibility=false;renderer.render(f,divider);auto leaking=renderer.sampleDdgi(leakPoints,leakNormals);report["thin_wall"]={{"visibility_on",rgb(visible[0])},{"visibility_off",rgb(leaking[0])},{"source_side",rgb(visible[1])}};std::cout<<"DDGI visibility "<<report["thin_wall"]<<std::endl;check(visible[0].x<leaking[0].x*.7f,"DDGI moments did not suppress thin-wall light leak");
    // Round-robin latency and a light switch: current source geometry/material
    // is sampled without a CPU visibility readback. At most one sweep to visit.
    f.ddgi.visibility=true;f.ddgi.probesPerFrame=4;f.ddgi.hysteresis=.95f;++f.ddgi.resetSerial;for(int i=0;i<12;i++){if(i==6)++f.historyKey;renderer.render(f,divider);}check(renderer.ddgiStats().frame==12,"TAA history reset discarded DDGI budget progress");auto before=renderer.sampleDdgi(leakPoints,leakNormals);MaterialParameters dark;dark.albedoAlpha={0,0,0,1};dark.factors={0,.7f,1,0};dark.emissiveNormal={0,0,0,0};emitter->update(dark);for(int i=0;i<12;i++)renderer.render(f,divider);auto switched=renderer.sampleDdgi(leakPoints,leakNormals);report["light_switch"]={{"before",rgb(before[1])},{"after_one_sweep",rgb(switched[1])}};check(switched[1].x<before[1].x*.6f,"DDGI light switch failed to accelerate history convergence");check(renderer.ddgiStats().updated==4&&renderer.ddgiStats().sweepFrames==12,"DDGI update budget or sweep accounting differs");
    // Inactive probes must be revisited after geometry moves away.
    std::vector<DrawPacket> solid;
    const auto outward=[&](glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 d,glm::vec3 n){solid.push_back({quad(device,a,b,c,d,n),white,glm::mat4(1),50+solid.size()});};
    outward({-1,0,-1},{1,0,-1},{1,0,1},{-1,0,1},{0,-1,0});
    outward({-1,1.2f,-1},{1,1.2f,-1},{1,1.2f,1},{-1,1.2f,1},{0,1,0});
    outward({-1,0,-1},{-1,0,1},{-1,1.2f,1},{-1,1.2f,-1},{-1,0,0});
    outward({1,0,-1},{1,0,1},{1,1.2f,1},{1,1.2f,-1},{1,0,0});
    outward({-1,0,-1},{1,0,-1},{1,1.2f,-1},{-1,1.2f,-1},{0,0,-1});
    outward({-1,0,1},{1,0,1},{1,1.2f,1},{-1,1.2f,1},{0,0,1});
    f.ddgi.probesPerFrame=48;f.ddgi.origin={-.45f,.1f,-.45f};f.ddgi.spacing=.3f;f.ddgi.counts={4,3,4};f.ddgi.maxDistance=20;f.ambient=.5f;++f.ddgi.resetSerial;renderer.render(f,solid);const std::vector<glm::vec4> movePoint{{0,.2f,0,1}},moveNormal{{0,1,0,0}};auto obstructed=renderer.sampleDdgi(movePoint,moveNormal);auto classified=renderer.readDdgiField();for(uint32_t i=0;i<f.ddgi.probeCount();i++)check(classified[i*DdgiSettings::stride].x==0 && classified[i*DdgiSettings::stride].y>0,"DDGI failed to classify probes inside solid geometry");for(int i=0;i<2;i++)renderer.render(f,{});auto revealed=renderer.sampleDdgi(movePoint,moveNormal);check(revealed[0].x>obstructed[0].x+.2f,"DDGI failed to refresh a moving occluder");report["moving_occluder"]={{"closed",rgb(obstructed[0])},{"revealed",rgb(revealed[0])}};
    f.sky=true;f.ambient=0;++f.ddgi.resetSerial;renderer.render(f,{});auto actualSky=renderer.sampleDdgi(movePoint,moveNormal);check(actualSky[0].x>0 && std::isfinite(actualSky[0].x),"DDGI missed atmospheric sky radiance");report["atmospheric_sky"]=rgb(actualSky[0]);
    f.ddgi.freezeUpdates=true;renderer.render(f,{});
    const auto debugField=renderer.readDdgiField();const auto debugHdr=renderer.readHDR();const auto plain=renderer.readOutput();
    f.ddgi.debugProbes=2;renderer.render(f,{});
    check(renderer.readOutput()!=plain,"DDGI classification overlay drew no probes");
    check(renderer.readHDR()==debugHdr&&renderer.readDdgiField()==debugField,"DDGI debug overlay polluted HDR or probe cache");
    f.ddgi.debugProbes=1;f.ddgi.debugLayer=1;renderer.render(f,{});
    check(renderer.readOutput()!=plain,"DDGI directional/layer visualization drew no probes");
    check(renderer.readHDR()==debugHdr&&renderer.readDdgiField()==debugField,"DDGI directional overlay changed lighting");
    f.ddgi.debugProbes=0;f.ddgi.debugLayer=-1;renderer.render(f,solid);const auto occludedPlain=renderer.readOutput();
    f.ddgi.debugProbes=2;renderer.render(f,solid);
    check(renderer.readOutput()==occludedPlain,"DDGI normal visualization showed probes through a solid enclosure");
    f.ddgi.debugXray=true;renderer.render(f,solid);
    check(renderer.readOutput()!=occludedPlain,"DDGI X-ray visualization did not reveal hidden probes");
    f.ddgi.debugProbes=0;f.ddgi.debugXray=false;f.ddgi.freezeUpdates=false;
    report["probe_visualization"]={{"classification",true},{"directional_irradiance",true},{"layer_filter",true},{"occlusion",true},{"xray",true},{"lighting_cache_unchanged",true}};
    f.ddgi.enabled=false;f.sky=false;renderer.render(f,room);auto disabled=renderer.readHDR();renderer.render(f,room);check(disabled==renderer.readHDR()&&renderer.ddgiStats().cacheBytes==0,"DDGI disabled path retained work or changed HDR");
    {
        RenderWorldSnapshot world;world.frame=f;pt::CpuScene empty(world);empty.environment=std::make_shared<pt::Environment>(16,8,std::vector<glm::vec3>(128,glm::vec3(.3f,.2f,.1f)));
        // Analytic sun on first-segment misses must not be baked into diffuse GI.
        empty.sunDirection=glm::normalize(glm::vec3(1,.125f,.125f));empty.sunRadius=.08f;empty.sunIrradiance=glm::vec3(10);
        auto tracer=std::make_shared<pt::GpuProbeTracer>(empty,device,8,2,4);
        ForwardPbrRenderer reference(device,directory,96,64,PbrPath::Scene);reference.setDdgiProbeTracer(tracer);
        f.ddgi.enabled=true;f.ddgi.pathTracedCapture=true;f.ddgi.captureResolution=8;f.ddgi.probesPerFrame=48;f.ddgi.hysteresis=0;f.sky=false;
        reference.render(f,{});auto values=reference.sampleDdgi(movePoint,moveNormal);auto scratch=reference.readDdgiCapture();
        check(glm::distance(glm::vec3(values[0]),glm::vec3(.3f,.2f,.1f))<.008f,"PT probe E/pi normalization / sky integration changed");
        for(size_t i=0;i<scratch.size();i+=4){check(scratch[i+3]==0,"PT probe miss distance is nonzero");check(glm::distance(glm::vec3(scratch[i],scratch[i+1],scratch[i+2]),glm::vec3(.3f,.2f,.1f))<1e-5f,"PT probe double counted sky or primary sun");}
        report["shared_pt_probe"]={{"constant_sky_E_over_pi",rgb(values[0])},{"first_segment_sun_excluded",true},{"gpu_only_capture_integration",true}};
    }
    report["cache_layout"]={{"probe_grid",{4,3,4}},{"stride_vec4",DdgiSettings::stride},{"irradiance_size",8},{"distance_size",16},{"visibility_power",3}};
    report["cache_bytes_48_probes"]=size_t(48)*DdgiSettings::stride*16*2;report["default_cache_bytes"]=settings.cacheBytes();report["budget_probes_per_frame"]=4;report["sweep_frames"]=12;report["total_validation_seconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();report["passed"]=true;
    if(outputDir)std::ofstream(out/"validation.json")<<report.dump(2)<<'\n';std::cout<<"DDGI validation passed: "<<report.dump()<<std::endl;
}
}
