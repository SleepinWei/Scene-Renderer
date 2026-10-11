#include "renderer/rhi/GpuGeometryCuller.h"
#include "rhi/ShaderAssets.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <json/json.hpp>
namespace render {
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool referenceVisible(const std::array<glm::vec4,3>& p,const FrameData& f){
    double scale=1;for(const auto& point:p)for(int c=0;c<4;++c)scale=std::max(scale,std::abs(double(point[c])));
    const double guardX=1+4./f.viewportWidth,guardY=1+4./f.viewportHeight,epsilon=scale*1e-5;
    for(int plane=0;plane<6;++plane){bool outside=true;
        for(const auto& v:p){double d=plane==0?v.x+guardX*v.w:plane==1?guardX*v.w-v.x:plane==2?v.y+guardY*v.w:plane==3?guardY*v.w-v.y:plane==4?v.z:v.w-v.z;
            if(d>=-epsilon)outside=false;}
        if(outside)return false;
    }
    return true;
}
}
void validateGeometryCulling(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory){
    using namespace rhi;if(!device->computeLimits().supported)return;
    MaterialDesc desc;desc.parameters.albedoAlpha={0,0,0,1};desc.parameters.emissiveNormal={.3f,.6f,1.2f,0};
    auto material=std::make_shared<GpuMaterial>(device,desc);
    // Unequal group count and scan chunks exercise carry between all 128 lanes.
    std::vector<MeshVertex> vertices;std::vector<uint32_t> indices;
    for(int triangle=0;triangle<16513;++triangle){
        const glm::vec3 offsets[]={{0,0,0},{4,0,0},{-4,0,0},{0,4,0},{0,-4,0},{0,0,-4},{0,0,4}};
        auto offset=offsets[triangle%7];const auto first=uint32_t(vertices.size());
        for(const auto& p:std::array<glm::vec3,3>{{{-.2f,-.2f,.5f},{.2f,-.2f,.5f},{0,.2f,.5f}}})vertices.push_back({p+offset,{0,0,1},{0,0}});
        indices.insert(indices.end(),{first,first+1,first+2});
    }
    auto mesh=std::make_shared<GpuMesh>(device,vertices,indices);
    std::vector<DrawPacket> packets{{mesh,material,glm::mat4(1),1}};
    FrameData frame;frame.viewportWidth=frame.viewportHeight=64;frame.triangleCullMinimum=1;
    frame.triangleCulling=true;
    GpuGeometryCuller culler(device,directory);
    auto compare=[&](std::shared_ptr<GpuMesh> source,const std::vector<MeshVertex>& v,const std::vector<uint32_t>& input,uint32_t base=0){
        std::vector<uint32_t> expected;
        for(size_t at=0;at<input.size();at+=3){
            std::array<glm::vec4,3> clip;
            for(size_t j=0;j<3;++j)clip[j]=frame.viewProjection*(packets[0].model*glm::vec4(v[input[at+j]+base].position,1));
            if(referenceVisible(clip,frame))expected.insert(expected.end(),input.begin()+at,input.begin()+at+3);
        }
        packets[0].mesh=source;Resources resources(device);auto commands=device->createCommandList();
        const auto draws=culler.record(resources,commands,frame,packets);check(bool(draws[0].arguments),"Triangle culling unexpectedly bypassed fixture");
        if(culler.dispatches()){device->submit(commands);culler.commit();}
        DrawIndexedIndirectArguments output{};device->readBuffer(draws[0].arguments,0,20,&output);
        check(output.indexCount==expected.size() && output.instanceCount==1 && output.firstIndex==0 && output.baseVertex==int32_t(base),"GPU triangle count / indirect fields differ from reference");
        std::vector<uint32_t> compact(output.indexCount);if(!compact.empty())device->readBuffer(draws[0].indices,0,compact.size()*4,compact.data());
        check(compact==expected,"GPU triangle compaction changed original index order or kept a rejected triangle");
        return output.indexCount/3;
    };
    const auto visible=compare(mesh,vertices,indices);
    check(visible>0 && visible<indices.size()/6,"Fine frustum culling did not reduce triangle draw count");
    // Preserve partially clipped / behind-eye triangles; no perspective divide.
    glm::mat4 depth(1);depth[2][2]=.5f;depth[3][2]=.5f;
    frame.viewProjection=depth*glm::perspective(glm::radians(75.f),1.f,.1f,100.f);
    std::vector<MeshVertex> crossings;
    for(int repeat=0;repeat<129;++repeat)for(const auto& p:std::array<glm::vec3,9>{{{-.05f,-.05f,-.2f},{.05f,-.05f,-.2f},{0,.05f,.1f},
        {-.05f,-.05f,-.05f},{.05f,-.05f,-.5f},{0,.05f,-.5f},{4,0,-2},{4,.1f,-2},{4.1f,0,-2}}})crossings.push_back({p,{0,0,1},{0,0}});
    std::vector<uint32_t> crossingIndices(crossings.size());for(size_t i=0;i<crossingIndices.size();++i)crossingIndices[i]=uint32_t(i);
    auto crossing=std::make_shared<GpuMesh>(device,crossings,crossingIndices);compare(crossing,crossings,crossingIndices);
    frame.viewProjection=glm::mat4(1);
    // Generated capacity differs from active count, with nonzero first/base.
    auto generated=std::make_shared<GpuMesh>(device,uint32_t(vertices.size()+1),uint32_t(indices.size()+192),glm::vec3(-8),glm::vec3(8));
    auto shifted=vertices;shifted.insert(shifted.begin(),{{0,0,0},{0,0,1},{0,0}});
    device->writeBuffer(generated->vertexBuffer(),0,shifted.size()*32,shifted.data());
    device->writeBuffer(generated->indexBuffer(),24,indices.size()*4,indices.data());
    DrawIndexedIndirectArguments input{uint32_t(indices.size()),1,6,1,0};device->writeBuffer(generated->indirectBuffer(),0,20,&input);
    compare(generated,shifted,indices,1);
    input.indexCount=0;device->writeBuffer(generated->indirectBuffer(),0,20,&input);compare(generated,shifted,{},1);
    // Multiple in-flight frames overwrite the same cached outputs in queue order.
    packets[0].mesh=mesh;
    for(int i=0;i<7;++i){device->beginFrame();Resources resources(device);auto commands=device->createCommandList();
        packets[0].model=glm::translate(glm::mat4(1),glm::vec3(i%2?4:0,0,0));culler.record(resources,commands,frame,packets);device->submit(commands);culler.commit();device->endFrame();}
    const auto finalCounts=culler.readCounts();check(finalCounts.size()==1 && finalCounts[0].outputTriangles==visible,"Reused GPU culling storage retained an earlier in-flight frame");
    auto streamed=GpuMesh::beginUpload(device,uint32_t(vertices.size()),uint32_t(indices.size()));
    streamed->uploadVertices(0,vertices.data(),uint32_t(vertices.size()));streamed->uploadIndices(0,indices.data(),uint32_t(indices.size()));
    compare(streamed,vertices,indices);compare(streamed,vertices,indices);check(culler.cacheHits()==1,"Static mesh cache was not reused after publication");
    auto edited=vertices;for(auto& vertex:edited)vertex.position.x+=20;
    streamed->uploadVertices(0,edited.data(),uint32_t(edited.size()));compare(streamed,edited,indices);
    check(!culler.cacheHits(),"Vertex upload reused an obsolete geometry visibility cache");

    // A large grid straddles the screen; baseline and compacted rendering must
    // match while truly drawing fewer indices, including the forward replay.
    std::vector<MeshVertex> grid;std::vector<uint32_t> gridIndices;
    constexpr uint32_t width=128,height=64;
    for(uint32_t y=0;y<=height;++y)for(uint32_t x=0;x<=width;++x)grid.push_back({{float(x)/width*8-4,float(y)/height*4-2,.5f},{0,0,1},{float(x)/width,float(y)/height}});
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x){uint32_t a=y*(width+1)+x,b=a+1,c=a+width+2,d=a+width+1;gridIndices.insert(gridIndices.end(),{a,b,c,a,c,d});}
    auto dense=std::make_shared<GpuMesh>(device,grid,gridIndices);
    std::vector<DrawPacket> scene{{dense,material,glm::mat4(1),1},{dense,material,glm::translate(glm::mat4(1),glm::vec3(20,0,0)),2},{dense,material,glm::translate(glm::mat4(1),glm::vec3(-20,0,0)),3}};
    FrameData view;view.viewportWidth=view.viewportHeight=96;view.toneMapping=false;view.ambient=0;
    ForwardPbrRenderer renderer(device,directory,96,96,PbrPath::Scene);
    view.objectCulling=view.triangleCulling=false;renderer.render(view,scene);const auto baseline=renderer.readHDR();const auto display=renderer.readOutput();
    view.objectCulling=view.triangleCulling=true;renderer.render(view,scene);const auto compacted=renderer.readHDR();const auto stats=renderer.geometryCullStats();const auto counts=renderer.readTriangleCullCounts();
    check(stats.sourcePackets==3 && stats.visiblePackets==1 && stats.culledPackets==2 && stats.triangleDraws==1,"Renderer did not reduce object draws and compact one intersecting mesh");
    check(counts.size()==1 && counts[0].inputTriangles==16384 && counts[0].outputTriangles<8192,"Renderer indirect draw did not reduce real triangle count");
    float error=0;for(size_t i=0;i<baseline.size();++i)error=std::max(error,std::abs(baseline[i]-compacted[i]));
    check(error<.002f && display==renderer.readOutput(),"Geometry culling changed HDR / display coverage");
    const auto bytes=stats.bufferBytes;renderer.render(view,scene);check(renderer.geometryCullStats().bufferBytes==bytes && renderer.geometryCullStats().triangleCacheHits==1 && !renderer.geometryCullStats().triangleDispatches,"Unchanged static camera did not reuse culling outputs");
    view.forwardShading=true;renderer.render(view,scene);auto forward=renderer.readHDR();for(size_t i=0;i<forward.size();++i)check(std::abs(forward[i]-baseline[i])<.002f,"Forward replay lost compacted geometry");
    // TAA visibility changes keep stable model identity, including id==0.
    scene[0].id=0;view.taa=true;renderer.render(view,scene);scene[1].model=glm::mat4(1);renderer.render(view,scene);
    for(float value:renderer.readHDR())check(std::isfinite(value),"TAA camera visibility change produced invalid history");
    if(const auto path=std::getenv("SCENERENDERER_GEOMETRY_CULL_REPORT")){
        nlohmann::json report={{"scope","96x96 grid geometry + triangle cull GPU stages; not full frame FPS"},
            {"objects_before",3},{"objects_after",1},{"triangles_before",counts[0].inputTriangles},{"triangles_after",counts[0].outputTriangles},
            {"extra_buffer_bytes",bytes},{"hdr_max_error",error},{"display_equal",true}};
        view.taa=false;view.forwardShading=false;scene[0].id=1;scene[1].model=glm::translate(glm::mat4(1),glm::vec3(20,0,0));
        for(int mode=0;mode<3;++mode){
            const bool enabled=mode!=0,moving=mode==2;
            view.triangleCulling=enabled;device->drainGpuProfile();std::vector<double> sums;
            nlohmann::json raw=nlohmann::json::array();
            for(int sample=0;sample<11;++sample){
                view.viewProjection=glm::mat4(1);if(moving)view.viewProjection[3].x=sample%2?.02f:0;
                renderer.render(view,scene);renderer.readHDR();const auto timing=device->drainGpuProfile();
                if(sample<3)continue;
                double sum=0;size_t selected=0;
                for(const auto& t:timing)if(t.label=="geometry" || t.label.rfind("geometry-cull/",0)==0){
                    sum+=t.endMilliseconds-t.startMilliseconds;++selected;
                    raw.push_back({{"sample",sample-3},{"stage",t.label},{"milliseconds",t.endMilliseconds-t.startMilliseconds}});
                }
                if(selected)sums.push_back(sum);
            }
            std::sort(sums.begin(),sums.end());
            const auto key=moving?"triangle_culling_moving":enabled?"triangle_culling_static":"baseline";
            report[key]={{"supported",!sums.empty()},{"samples",sums.size()},{"stages",raw}};
            if(!sums.empty())report[key]["median_gpu_geometry_ms"]=(sums[(sums.size()-1)/2]+sums[sums.size()/2])*.5;
            report[key]["last_cache_hits"]=renderer.geometryCullStats().triangleCacheHits;
            report[key]["last_dispatches"]=renderer.geometryCullStats().triangleDispatches;
        }
        const auto parent=std::filesystem::path(path).parent_path();if(!parent.empty())std::filesystem::create_directories(parent);
        std::ofstream file(path);file<<report.dump(2)<<'\n';check(bool(file),"Cannot save geometry culling report");
    }
    std::cout<<"Geometry culling: objects 3 -> 1; grid triangles "<<counts[0].inputTriangles<<" -> "<<counts[0].outputTriangles
             <<"; stable compaction reference "<<indices.size()/3<<" -> "<<visible<<"; HDR max error "<<error<<"; identical display, near/eye crossings, generated counts, storage reuse and TAA passed\n";
}
}
