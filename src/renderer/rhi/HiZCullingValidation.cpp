#include "renderer/rhi/GpuHiZCuller.h"
#include "renderer/rhi/HiZVisualization.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <json/json.hpp>
namespace render {
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::shared_ptr<GpuMesh> rectangle(std::shared_ptr<rhi::GraphicsDevice> d,float x0,float y0,float x1,float y1,float z,uint32_t columns=1){
    std::vector<MeshVertex> vertices;std::vector<uint32_t> indices;
    for(uint32_t x=0;x<=columns;++x){const float u=float(x)/columns;
        for(float v:{0.f,1.f})vertices.push_back({{x0+(x1-x0)*u,y0+(y1-y0)*v,z},{0,0,1},{u,v}});}
    for(uint32_t x=0;x<columns;++x){auto a=x*2;indices.insert(indices.end(),{a,a+2,a+3,a,a+3,a+1});}
    return std::make_shared<GpuMesh>(d,vertices,indices);
}
void box(std::vector<MeshVertex>& vertices,std::vector<uint32_t>& indices,glm::vec3 lo,glm::vec3 hi){
    auto centre=(lo+hi)*.5f,half=(hi-lo)*.5f;
    for(auto normal:std::array<glm::vec3,6>{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}}){
        auto u=std::abs(normal.y)>.5f?glm::vec3(1,0,0):glm::normalize(glm::cross(glm::vec3(0,1,0),normal));auto v=glm::cross(normal,u);
        auto face=centre+normal*glm::dot(glm::abs(normal),half);auto first=uint32_t(vertices.size());
        for(auto corner:std::array<glm::vec2,4>{{{-1,-1},{1,-1},{1,1},{-1,1}}})vertices.push_back({face+u*glm::dot(glm::abs(u),half)*corner.x+v*glm::dot(glm::abs(v),half)*corner.y,normal,(corner+1.f)*.5f});
        indices.insert(indices.end(),{first,first+1,first+2,first,first+2,first+3});
    }
}
void captureVisualization(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& shaderDirectory,const std::string& directory){
    auto mesh=[&](glm::vec3 lo,glm::vec3 hi){std::vector<MeshVertex> v;std::vector<uint32_t> i;box(v,i,lo,hi);return std::make_shared<GpuMesh>(device,v,i);};
    auto material=[&](glm::vec3 colour){MaterialDesc d;d.parameters.albedoAlpha=glm::vec4(colour,1);d.parameters.factors={0,.6f,1,0};d.parameters.emissiveNormal=glm::vec4(colour*.025f,0);return std::make_shared<GpuMaterial>(device,d);};
    auto teal=material({.2f,.45f,.46f}),floorMaterial=material({.3f,.32f,.36f});
    std::vector<std::shared_ptr<GpuMaterial>> colours;for(auto colour:std::array<glm::vec3,4>{{{.95f,.28f,.12f},{.9f,.62f,.08f},{.2f,.42f,.9f},{.55f,.24f,.8f}}})colours.push_back(material(colour));
    std::vector<MeshVertex> wallVertices;std::vector<uint32_t> wallIndices;
    box(wallVertices,wallIndices,{-4,0,-.1f},{-.65f,3.3f,.1f});box(wallVertices,wallIndices,{.65f,0,-.1f},{4,3.3f,.1f});
    box(wallVertices,wallIndices,{-.65f,0,-.1f},{.65f,1.1f,.1f});box(wallVertices,wallIndices,{-.65f,2.4f,-.1f},{.65f,3.3f,.1f});
    auto wallMesh=std::make_shared<GpuMesh>(device,wallVertices,wallIndices),cube=mesh(glm::vec3(-.17f),glm::vec3(.17f));
    std::vector<DrawPacket> packets{{wallMesh,teal,glm::mat4(1),1},{mesh({-7,-.08f,-7},{7,0,10}),floorMaterial,glm::mat4(1),2}};
    for(int y=0;y<8;++y)for(int x=0;x<8;++x){auto position=glm::vec3((x-3.5f)*.78f,.2f+y*.38f,-1.1f);packets.push_back({cube,colours[(x+y)%4],glm::translate(glm::mat4(1),position),uint64_t(packets.size()+1)});}
    auto front=mesh(glm::vec3(-.38f),glm::vec3(.38f));for(auto p:std::array<glm::vec3,3>{{{-2.7f,.38f,1.5f},{1.8f,.38f,1.1f},{-.7f,.38f,2.2f}}})packets.push_back({front,colours[0],glm::translate(glm::mat4(1),p),uint64_t(packets.size()+1)});
    FrameData frame;frame.viewportWidth=960;frame.viewportHeight=720;frame.cameraPosition={0,2.6f,8};frame.view=glm::lookAt(frame.cameraPosition,glm::vec3(0,1.5f,0),glm::vec3(0,1,0));
    glm::mat4 remap(1);remap[2][2]=.5f;remap[3][2]=.5f;frame.viewProjection=remap*glm::perspective(glm::radians(50.f),960.f/720.f,.1f,40.f)*frame.view;
    frame.ambient=.12f;frame.hizMaxOccluders=1;frame.hizOccluderMinArea=.08f;frame.lights={{{0,0,0,0},{4,3.8f,3.6f,0},{-.5f,-1,-.45f,0}}};
    ForwardPbrRenderer renderer(device,shaderDirectory,960,720,PbrPath::Scene);exportHiZVisualization(renderer,frame,packets,directory,"occlusion-window",1.1f);
}

}
void validateHiZCulling(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory){
    using namespace rhi;
    check(device->computeLimits().supported && device->supportsTextureSubresources(),"HiZ validation requires native compute and mip views");
    MaterialDesc desc;desc.parameters.albedoAlpha={0,0,0,1};desc.parameters.emissiveNormal={.3f,.7f,.2f,0};
    auto green=std::make_shared<GpuMaterial>(device,desc);desc.parameters.emissiveNormal={1,.1f,.4f,0};auto red=std::make_shared<GpuMaterial>(device,desc);
    auto wall=rectangle(device,-.8f,-.8f,.8f,.8f,.2f);
    auto small=rectangle(device,-.1f,-.1f,.1f,.1f,.7f);
    std::vector<DrawPacket> scene{{wall,green,glm::mat4(1),1},{small,red,glm::mat4(1),2},
        {small,red,glm::translate(glm::mat4(1),glm::vec3(.9f,0,0)),3},
        {small,red,glm::translate(glm::mat4(1),glm::vec3(0,.4f,-.6f)),4}};
    FrameData frame;frame.viewportWidth=129;frame.viewportHeight=97;frame.ambient=0;frame.toneMapping=false;frame.hizMaxOccluders=1;
    ForwardPbrRenderer renderer(device,directory,129,97,PbrPath::Scene);
    float worst=0;size_t comparisons=0;
    auto compare=[&](const FrameData& input,const std::vector<DrawPacket>& draws){
        auto f=input;f.hizCulling=false;renderer.resetTemporal();renderer.render(f,draws);auto baseline=renderer.readHDR(),positions=renderer.readGBuffer(0);const auto display=renderer.readOutput();
        f.hizCulling=true;renderer.resetTemporal();renderer.render(f,draws);auto output=renderer.readHDR(),actual=renderer.readGBuffer(0);
        for(size_t i=0;i<baseline.size();++i){check(std::isfinite(output[i]),"HiZ output is not finite");worst=std::max(worst,std::abs(baseline[i]-output[i]));check(std::abs(baseline[i]-output[i])<.002f,"HiZ changed visible HDR pixels");check(std::abs(positions[i]-actual[i])<.001f,"HiZ changed visible G-buffer geometry");}
        check(display==renderer.readOutput(),"HiZ changed display pixels");++comparisons;return renderer.readHiZCullCounts();
    };
    auto counts=compare(frame,scene);auto stats=renderer.hizCullStats();
    check(stats.active && stats.candidates==4 && stats.occluders==1 && stats.prepassTriangles==2 && stats.pyramidLevels==8,"HiZ did not select the expected current-frame occluder");
    check(counts.size()==4 && counts[0].outputTriangles==2 && counts[0].occluder && counts[1].outputTriangles==0 && counts[2].outputTriangles==2 && counts[3].outputTriangles==2,"HiZ behind/front/partial-screen objects have incorrect indirect counts");
    const auto bytes=stats.bufferBytes;
    compare(frame,scene);check(renderer.hizCullStats().bufferBytes==bytes,"HiZ reallocates unchanged frame targets");
    // Current-frame updates: candidates and occluders can move without waiting
    // for a visibility readback or a previous-frame depth result.
    auto moved=scene;moved[1].model=glm::translate(glm::mat4(1),glm::vec3(.9f,0,0));
    counts=compare(frame,moved);check(counts[1].outputTriangles==2,"Uncovered moving object remained culled");
    moved=scene;moved[0].model=glm::translate(glm::mat4(1),glm::vec3(2,0,0));
    compare(frame,moved);check(!renderer.hizCullStats().active,"Missing occluder did not disable HiZ work");
    auto camera=frame;camera.viewProjection=glm::translate(glm::mat4(1),glm::vec3(.55f,0,0));compare(camera,scene);
    auto asymmetric=scene;asymmetric[0].mesh=rectangle(device,-.8f,-.1f,.8f,.9f,.2f);
    asymmetric[1].model=glm::translate(glm::mat4(1),glm::vec3(0,.5f,0));asymmetric[2].model=glm::translate(glm::mat4(1),glm::vec3(0,-.5f,0));
    counts=compare(frame,asymmetric);check(counts[1].outputTriangles==0 && counts[2].outputTriangles==2,"HiZ screen-space Y convention is reversed");
    // Equal-depth / near-plane geometry must fail open, as must bounds that
    // straddle an occluder edge rather than being completely behind it.
    auto edge=scene;edge[1].mesh=rectangle(device,-.95f,-.1f,.95f,.1f,.7f);counts=compare(frame,edge);check(counts[1].outputTriangles==2,"Partially exposed bounds were culled");
    edge=scene;edge[1].mesh=rectangle(device,-.1f,-.1f,.1f,.1f,.2f);counts=compare(frame,edge);check(counts[1].outputTriangles==2,"Equal-depth geometry was culled");
    edge[1].mesh=rectangle(device,-.1f,-.1f,.1f,.1f,0);counts=compare(frame,edge);check(counts[1].outputTriangles==2,"Near-plane geometry was culled");
    // Masked depth must preserve holes. The right half of this wall is alpha 0.
    auto masked=desc;masked.parameters.factors.w=.5f;masked.images[0]={2,1,{255,255,255,255,255,255,255,0}};masked.filter=Filter::Nearest;
    auto holes=scene;holes[0].material=std::make_shared<GpuMaterial>(device,masked);holes[1].model=glm::translate(glm::mat4(1),glm::vec3(.4f,0,0));
    counts=compare(frame,holes);check(counts[1].outputTriangles==2,"Alpha hole became an opaque HiZ occluder");
    // A geometric slit through the occluder leaves background pixels in every
    // covering mip. Both strips share one mesh so they are selected together.
    std::vector<MeshVertex> slitVertices;std::vector<uint32_t> slitIndices;
    for(const auto& range:std::array<glm::vec2,2>{{{-.8f,-.015f},{.015f,.8f}}}){
        const uint32_t start=uint32_t(slitVertices.size());
        for(const auto p:std::array<glm::vec2,4>{{{range.x,-.8f},{range.y,-.8f},{range.y,.8f},{range.x,.8f}}})slitVertices.push_back({{p.x,p.y,.2f},{0,0,1},{0,0}});
        slitIndices.insert(slitIndices.end(),{start,start+1,start+2,start,start+2,start+3});
    }
    auto slit=scene;slit[0].mesh=std::make_shared<GpuMesh>(device,slitVertices,slitIndices);counts=compare(frame,slit);check(counts[1].outputTriangles==2,"Thin uncovered slit disappeared in the depth pyramid");
    // Odd trailing column: reduction must include it rather than rounding the
    // clear texel away. The candidate itself touches the right viewport edge.
    auto trailing=scene;trailing[0].mesh=rectangle(device,-1,-1,.985f,1,.2f);trailing[1].mesh=rectangle(device,.97f,-.1f,1,.1f,.7f);
    counts=compare(frame,trailing);check(counts[1].outputTriangles==2,"Odd trailing clear column caused a false cull");
    // GPU triangle compaction and HiZ compose without modifying the cached
    // triangle arguments. Hidden result stays zero; later reveal restores it.
    auto combined=scene;combined[1].mesh=rectangle(device,-.1f,-.1f,1.4f,.1f,.7f,2048);
    auto both=frame;both.triangleCulling=true;both.triangleCullMinimum=1;
    counts=compare(both,combined);check(counts[1].outputTriangles>0 && counts[1].outputTriangles<4096,"HiZ composition lost compacted triangle counts");
    auto triangleCounts=renderer.readTriangleCullCounts();check(!triangleCounts.empty(),"Combined fixture skipped triangle compaction");
    // Force a hidden dense candidate whose bounds intersect the top clip plane;
    // the wider wall hides the entire on-screen area of that candidate.
    combined[0].mesh=rectangle(device,-1,-1,1,1,.2f);combined[1].mesh=rectangle(device,-.1f,-.1f,.1f,1.4f,.7f,2048);
    counts=compare(both,combined);check(counts[1].outputTriangles==0,"Hidden triangle-compacted draw was not zeroed");
    triangleCounts=renderer.readTriangleCullCounts();auto candidate=std::find_if(triangleCounts.begin(),triangleCounts.end(),[](const auto& c){return c.packet==1;});check(candidate!=triangleCounts.end() && candidate->outputTriangles>0,"HiZ overwrote the reusable triangle visibility cache");
    combined[1].model=glm::translate(glm::mat4(1),glm::vec3(0,0,-.6f));counts=compare(both,combined);check(counts[1].outputTriangles>0,"HiZ reveal reused a stale zero triangle count");
    // Unsupported material and instance paths retain their original draws.
    auto transparent=desc;transparent.transparent=true;transparent.parameters.albedoAlpha.w=.5f;
    auto excluded=scene;excluded.push_back({small,std::make_shared<GpuMaterial>(device,transparent),glm::mat4(1),5});
    auto sss=desc;sss.extension.lobes.w=.2f;excluded.push_back({small,std::make_shared<GpuMaterial>(device,sss),glm::mat4(1),6});
    excluded.push_back({GpuMesh::withInstances(small,{glm::mat4(1)}),red,glm::mat4(1),7});
    counts=compare(frame,excluded);check(counts.size()==4,"HiZ used unknown instance/material bounds");
    // Auxiliary shadows use the source geometry even when a camera draw is zero.
    auto shadow=frame;shadow.shadows=true;shadow.lights={{{0,0,0,0},{3,3,3,0},{-.7f,0,-1,0}}};
    shadow.hizCulling=false;renderer.render(shadow,scene);auto depth=renderer.readShadowDepth();
    shadow.hizCulling=true;renderer.render(shadow,scene);check(depth==renderer.readShadowDepth(),"Main HiZ visibility removed an auxiliary shadow caster");
    auto replay=frame;replay.forwardShading=true;compare(replay,scene);
    // Perspective near/eye crossing and a large-world model/view cancellation.
    auto perspective=frame;glm::mat4 remap(1);remap[2][2]=.5f;remap[3][2]=.5f;
    perspective.viewProjection=remap*glm::perspective(glm::radians(70.f),129.f/97.f,.1f,100.f);
    auto perspectiveScene=scene;perspectiveScene[0].mesh=rectangle(device,-2,-2,2,2,-2);
    perspectiveScene[1].mesh=rectangle(device,-.1f,-.1f,.1f,.1f,-3);perspectiveScene[2].mesh=rectangle(device,-.1f,-.1f,.1f,.1f,-.05f);
    compare(perspective,perspectiveScene);
    std::vector<MeshVertex> crossingVertices{{{-.05f,-.05f,-.2f},{0,0,1},{0,0}},{{.05f,-.05f,-.2f},{0,0,1},{0,0}},{{0,.05f,.1f},{0,0,1},{0,0}}};
    perspectiveScene[2].mesh=std::make_shared<GpuMesh>(device,crossingVertices,std::vector<uint32_t>{0,1,2});compare(perspective,perspectiveScene);
    auto large=frame;large.viewProjection=glm::translate(glm::mat4(1),glm::vec3(-100000,0,0));auto world=scene;
    for(auto& p:world)p.model=glm::translate(glm::mat4(1),glm::vec3(100000,0,0))*p.model;compare(large,world);
    renderer.resize(65,33);auto resized=frame;resized.viewportWidth=65;resized.viewportHeight=33;compare(resized,scene);check(renderer.hizCullStats().pyramidLevels==7,"HiZ resize retained old mip dimensions");
    renderer.resize(129,97);auto temporal=frame;temporal.taa=temporal.hizCulling=true;
    for(int i=0;i<6;++i){device->beginFrame();auto current=scene;current[1].model=glm::translate(glm::mat4(1),glm::vec3(i%2?.9f:0,0,0));renderer.render(temporal,current);device->endFrame();}
    for(float value:renderer.readHDR())check(std::isfinite(value),"TAA HiZ visibility changes produced invalid history");
    // Off switch releases the optional culler; no stale GPU diagnostic state.
    frame.hizCulling=false;renderer.render(frame,scene);check(!renderer.hizCullStats().active && renderer.readHiZCullCounts().empty(),"Disabled HiZ retained active visibility results");
    nlohmann::json report={{"hdr_max_error",worst},{"display_equal",true},{"comparisons",comparisons},{"objects",4},{"occluders",1},{"prepass_triangles",2},{"main_triangles_before",8},{"main_triangles_after",6},{"pyramid_levels",8},{"extra_bytes",bytes}};
    // Many expensive static meshes fully behind one cheap wall: count actual
    // submitted indices, including the extra occluder prepass separately.
    auto dense=rectangle(device,-.12f,-.12f,.12f,.12f,.7f,4096);
    std::vector<DrawPacket> heavy{{wall,green,glm::mat4(1),1}};
    for(int i=0;i<64;++i)heavy.push_back({dense,red,glm::translate(glm::mat4(1),glm::vec3(float(i%8)*.13f-.455f,float(i/8)*.13f-.455f,0)),uint64_t(i+2)});
    auto heavyCounts=compare(frame,heavy);uint64_t before=0,after=0;
    for(const auto& c:heavyCounts){before+=c.inputTriangles;after+=c.outputTriangles;}
    check(before==524290 && after==2 && renderer.hizCullStats().prepassTriangles==2,"HiZ did not remove dense hidden geometry from actual GPU arguments");
    report["comparisons"]=comparisons;report["hdr_max_error"]=worst;
    report["dense_scene"]={{"main_triangles_before",before},{"main_triangles_after",after},{"prepass_triangles",2},{"nonzero_main_draws_before",65},{"nonzero_main_draws_after",1},{"display_equal",true}};
    if(const auto path=std::getenv("SCENERENDERER_HIZ_REPORT")){
        // Timestamp diagnostics only. Render does not wait/read visibility.
        for(int mode=0;mode<2;++mode){
            auto timed=frame;timed.hizCulling=mode!=0;
            std::vector<double> samples;auto stages=nlohmann::json::array();
            for(int i=0;i<11;++i){
                device->drainGpuProfile();renderer.render(timed,heavy);device->waitIdle();const auto profile=device->drainGpuProfile();double sum=0;auto entry=nlohmann::json::object();
                for(const auto& sample:profile)if(sample.label=="geometry" || sample.label.rfind("hiz/",0)==0){const double ms=sample.endMilliseconds-sample.startMilliseconds;sum+=ms;entry[sample.label]=ms;}
                if(i>=3 && !entry.empty()){samples.push_back(sum);stages.push_back(entry);}
            }
            if(!samples.empty()){auto sorted=samples;std::sort(sorted.begin(),sorted.end());report["gpu_timings"][mode?"hiz":"baseline"]={{"median_ms",(sorted[(sorted.size()-1)/2]+sorted[sorted.size()/2])*.5},{"samples_ms",samples},{"stages",stages}};}
        }
        report["timing_scope"]="129x97 dense occluded static meshes; geometry plus selected-occluder depth, full HiZ pyramid and tests; excludes CPU/readback; not full-frame FPS";
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());std::ofstream file(path);file<<report.dump(2)<<'\n';check(bool(file),"HiZ report write failed");
    }
    if(const auto captureDir=std::getenv("SCENERENDERER_HIZ_CAPTURE_DIR"))captureVisualization(device,directory,captureDir);
    std::cout<<"HiZ current-frame occlusion: main objects 4 -> 3, triangles 8 -> 6, prepass 2; HDR max error "<<worst<<"; "<<comparisons<<" image comparisons, alpha/slit/odd holes, near/equal depth, movement, triangle composition, source shadows, resize and TAA passed\n";
}
}
