#include "renderer/rhi/GpuTerrain.h"
#include "renderer/rhi/GpuGrass.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <tuple>
namespace render {
namespace{void check(bool v,const char* r){if(!v)throw std::runtime_error(r);}}
namespace {
void checkClosed(GpuTerrain& surface){
    auto arguments=surface.readArguments();
    auto bv=surface.readVertices(arguments.indexCount/6*4);auto bi=surface.readIndices(arguments.indexCount);
    using Point=std::pair<int,int>;std::map<std::pair<Point,Point>,uint32_t> edges;int64_t area=0;
    std::map<Point,float> heights;
    for(const auto& v:bv){Point p{int(std::round((v.position.x+1)*640)),int(std::round((v.position.z+1)*640))};
        auto found=heights.find(p);if(found!=heights.end())check(std::abs(found->second-v.position.y)<1e-5f,"Terrain morph disagrees at a shared seam vertex");else heights.emplace(p,v.position.y); }
    for(size_t at=0;at<bi.size();at+=3){Point q[3];for(int i=0;i<3;i++){check(bi[at+i]<bv.size(),"Bounded terrain index overflow");auto v=bv[bi[at+i]].position;q[i]={int(std::round((v.x+1)*640)),int(std::round((v.z+1)*640))};check(q[i].first>=0&&q[i].first<=1280&&q[i].second>=0&&q[i].second<=1280,"Bounded stitch outside grid");}
        int64_t signedArea=int64_t(q[1].first-q[0].first)*(q[2].second-q[0].second)-int64_t(q[1].second-q[0].second)*(q[2].first-q[0].first);check(signedArea<=0,"Terrain stitch inverted a triangle");if(!signedArea)continue;area-=signedArea;for(int i=0;i<3;i++){auto a=q[i],b=q[(i+1)%3];if(b<a)std::swap(a,b);edges[{a,b}]++;}}
    check(area==int64_t(1280)*1280*2,"Terrain stitched mesh overlaps or leaves a hole");for(const auto& e:edges){auto a=e.first.first,b=e.first.second;bool boundary=(a.first==b.first&&(a.first==0||a.first==1280))||(a.second==b.second&&(a.second==0||a.second==1280));check(e.second==(boundary?1u:2u),"Terrain stitch leaves an unmatched interior edge / T junction");}
}
}
void validateTerrainRhi(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& directory){
    if(!d->computeLimits().maxStorageImages){std::cout<<"RHI terrain GPU queues require compute; deferred on this backend\n";return;}
    validateVirtualTextureRhi(d,directory);
    {
        std::vector<float> spike(129*129);spike[64*129+64]=3;
        auto source=heightVirtualSource(129,129,spike);
        check(source.terrainBounds.size()==34125 && source.terrainBounds[8525].y==0,"Terrain block bounds retained global height range");
        check(source.terrainBounds[8525+80*160+80].y==3 && source.terrainBounds[2*5+2].y==3,"Terrain block hierarchy lost an interior height spike");
    }
    std::vector<float> h(32*32);for(int y=0;y<32;y++)for(int x=0;x<32;x++)h[y*32+x]=.2f+.1f*x/31-.07f*y/31;
    auto terrainOwner=std::make_shared<GpuTerrain>(d,directory,32,32,h,8192);auto& terrain=*terrainOwner;FrameData f;f.cameraPosition={0,6,12};f.view=glm::lookAt(f.cameraPosition,glm::vec3(0),glm::vec3(0,1,0));glm::mat4 correction(1);correction[2][2]=.5f;correction[3][2]=.5f;f.viewProjection=correction*glm::perspective(glm::radians(65.f),1.f,.1f,100.f)*f.view;
    const auto model=glm::scale(glm::mat4(1),glm::vec3(8,3,8));terrain.update(f,model);auto nodes=terrain.readNodes();auto args=terrain.readArguments();const uint32_t leaves=uint32_t(nodes.size()/4);
    check(leaves>=25 && args.indexCount==leaves*64*6 && args.instanceCount==1 && !args.firstIndex && !args.baseVertex && !args.firstInstance,"Terrain generated indirect ABI/count invalid");
    // Resolution/FOV are part of the LOD criterion; validity and coverage are
    // tested independently of the GPU formula instead of mirroring it.
    terrain.update(f,model);check(terrain.geometryStable(),"Unchanged terrain never enables temporal history");
    auto fineLeaves=terrain.readNodes().size()/4;
    auto low=f;low.viewportWidth=128;low.viewportHeight=72;terrain.update(low,model);
    check(terrain.readNodes().size()/4<=fineLeaves,"Lower resolution increased terrain refinement");
    terrain.update(f,model);nodes=terrain.readNodes();args=terrain.readArguments();
    auto vertices=terrain.readVertices(args.indexCount/6*4);auto indices=terrain.readIndices(args.indexCount);for(const auto& v:vertices){check(std::isfinite(v.position.x)&&std::isfinite(v.normal.y),"Terrain generated vertex nonfinite");check(v.position.x>=-1.001f && v.position.x<=1.001f && v.position.z>=-1.001f && v.position.z<=1.001f,"Terrain stitched vertex outside domain");const float height=.2f+.1f*(v.position.x*.5f+.5f)-.07f*(v.position.z*.5f+.5f);check(std::abs(v.position.y-height)<.0001f,"Terrain bilinear height/stitching failed");check(std::abs(glm::length(v.normal)-1)<.0001f,"Terrain normal not normalized");check(glm::length(v.normal-glm::normalize(glm::vec3(-.05f,1,.035f)))<.0001f,"Terrain boundary derivative halves plane slope");}for(auto i:indices)check(i<vertices.size(),"Terrain generated index overflow");
    MaterialDesc desc;desc.images[0]={128,128,std::vector<uint8_t>(128*128*4)};for(size_t i=0;i<desc.images[0].pixels.size();i+=4){desc.images[0].pixels[i]=51;desc.images[0].pixels[i+1]=102;desc.images[0].pixels[i+2]=153;desc.images[0].pixels[i+3]=255;}desc.parameters.albedoAlpha={.3f,.5f,.15f,1};desc.parameters.factors={0,.8f,1,0};auto virtualMaterial=std::make_shared<GpuVirtualTexture>(d,materialVirtualSource(desc.images));auto material=std::make_shared<GpuMaterial>(d,desc,virtualMaterial);f.lights={{{0,0,0,0},{4,4,4,0},{0,-1,-.1f,0}}};ForwardPbrRenderer renderer(d,directory,64,64,PbrPath::Deferred);renderer.render(f,{{terrain.mesh(),material,model}});size_t valid=0;auto positions=renderer.readGBuffer(0);for(size_t i=3;i<positions.size();i+=4)if(positions[i]==1)++valid;check(valid>100,"Terrain compute -> indexed indirect drawing produced no surface");auto colors=renderer.readGBuffer(2);for(size_t i=0;i<positions.size();i+=4)if(positions[i+3]==1){glm::vec3 expected=glm::pow(glm::vec3(.3f*.2f,.5f*.4f,.15f*.6f),glm::vec3(2.2f));check(glm::length(glm::vec3(colors[i],colors[i+1],colors[i+2])-expected)<.001f,"Terrain virtual albedo / material factors / G-buffer mismatch");}
    GpuGrass grass(d,directory,terrainOwner,model,4096);grass.update(model,0);auto grassArgs=grass.readArguments();uint32_t expectedGrass=0;for(size_t i=0;i<nodes.size();i+=4)if(nodes[i+2]<=1)expectedGrass+=256;
    check(grassArgs.indexCount==3 && (grassArgs.instanceCount<=std::min(expectedGrass,4096u)) && grassArgs.instanceCount>0 && !grassArgs.firstInstance,"Grass indirect pose capacity/count invalid");
    auto poses=grass.readPoses(grassArgs.instanceCount);for(const auto& p:poses){for(int c=0;c<4;c++)for(int r=0;r<4;r++)check(std::isfinite(p[c][r]),"Grass pose nonfinite");check(std::abs(p[3].x)<=8.01f && std::abs(p[3].z)<=8.01f,"Grass pose outside terrain");const float y=3*(.2f+.1f*(p[3].x/16+.5f)-.07f*(p[3].z/16+.5f));check(std::abs(p[3].y-y)<.0001f,"Grass stem detached from height field");}
    renderer.render(f,{{grass.mesh(),material,glm::mat4(1)}});auto grassPixels=renderer.readGBuffer(0);size_t blades=0;for(size_t i=3;i<grassPixels.size();i+=4)if(grassPixels[i]==1)++blades;check(blades>5,"Grass storage vertex -> indexed instanced indirect draw missing");
    grass.update(model,1);auto wind=grass.readPoses(grass.readArguments().instanceCount);bool bending=false;for(const auto& p:wind)if(std::abs(p[1].x)>.001f)bending=true;check(bending,"Grass wind did not bend blades");
    std::cout<<"RHI grass GPU poses, height attachment, bounded indirect instance generation, vertex storage sampling and wind passed; instances "<<grassArgs.instanceCount<<", pixels "<<blades<<"\n";
    f.cameraPosition={0,70,100};f.view=glm::lookAt(f.cameraPosition,glm::vec3(0),glm::vec3(0,1,0));terrain.update(f,model);check(terrain.readNodes().size()/4<=fineLeaves,"Terrain distant view increased refinement");
    // Force the mesh budget to exhaust with uneven LOD, then verify a closed
    // manifold in XZ. Height comparison alone cannot detect a T junction or hole.
    f.cameraPosition={0,3,25};f.view=glm::lookAt(f.cameraPosition,glm::vec3(0),glm::vec3(0,1,0));f.viewProjection=correction*glm::perspective(glm::radians(65.f),1.f,.1f,200.f)*f.view;
    std::vector<float> hills(128*128);for(uint32_t y=0;y<128;y++)for(uint32_t x=0;x<128;x++)hills[y*128+x]=.1f*std::sin(x*.11f)*std::cos(y*.08f);
    GpuTerrain bounded(d,directory,128,128,hills,512);bounded.update(f,glm::scale(glm::mat4(1),glm::vec3(30,3,30)));auto boundedNodes=bounded.readNodes();auto boundedArgs=bounded.readArguments();check(boundedNodes.size()/4<=512 && boundedArgs.indexCount<=512*384,"Terrain leaf budget overflow");
    std::vector<uint32_t> lodGrid(160*160);for(size_t i=0;i<boundedNodes.size();i+=4){uint32_t step=1u<<boundedNodes[i+2],xx=boundedNodes[i]*step,yy=boundedNodes[i+1]*step;for(uint32_t y=yy;y<yy+step;y++)for(uint32_t x=xx;x<xx+step;x++)lodGrid[y*160+x]=boundedNodes[i+2];}uint32_t largestDifference=0;for(int y=0;y<160;y++)for(int x=0;x<160;x++){if(x<159)largestDifference=std::max(largestDifference,uint32_t(std::abs(int(lodGrid[y*160+x])-int(lodGrid[y*160+x+1]))));if(y<159)largestDifference=std::max(largestDifference,uint32_t(std::abs(int(lodGrid[y*160+x])-int(lodGrid[(y+1)*160+x]))));}
    checkClosed(bounded);
    std::cout<<"Terrain bounded nonlinear mesh: "<<boundedNodes.size()/4<<" leaves, max adjacent LOD difference "<<largestDifference<<"; closed manifold and complete coverage validated\n";
    {
        using namespace rhi;
        // One root is fully refined, all 24 neighbors remain at level 5.
        // This gives a deliberate five-level transition, independent of LOD heuristics.
        GpuTerrain mixed(d,directory,128,128,hills,2048);mixed.heightTexture()->update({{0,0,0},{0,1,0},{0,0,1},{0,1,1}},4);
        std::vector<uint32_t> queue(4);queue[1]=queue[2]=1;auto append=[&](uint32_t x,uint32_t y,uint32_t lod){queue.insert(queue.end(),{x,y,lod,0});++queue[0];};
        for(uint32_t y=0;y<5;y++)for(uint32_t x=0;x<5;x++)if(x!=2 || y!=2)append(x,y,5);
        for(uint32_t y=64;y<96;y++)for(uint32_t x=64;x<96;x++)append(x,y,0);d->writeBuffer(mixed.leafQueue(),0,queue.size()*4,queue.data());
        Resources test(d);std::vector<float> levels(160*160*4);for(uint32_t y=0;y<160;y++)for(uint32_t x=0;x<160;x++)levels[(y*160+x)*4]=x>=64&&x<96&&y>=64&&y<96?0:1;
        auto lod=test.texture({160,160,Format::RGBA32Float,TextureUsage::Storage|TextureUsage::CopyDestination,"Synthetic five-level terrain seam"});auto lodView=test.view(lod);d->writeTextureFloat(lod,levels.data(),levels.size()*4);
        struct alignas(16) Params{glm::mat4 vp,view,model;glm::ivec4 counts,dimensions;glm::vec4 screen;};Params parameters{f.viewProjection,f.view,glm::mat4(1),{0,2048,0,0},{128,128,0,0},{1280,720,2,0}};auto data=test.buffer({240,BufferUsage::Uniform,"Mixed LOD terrain parameters"},&parameters);
        auto path=directory+"/terrain-generate.comp";ComputePipelineDesc pipeline;pipeline.shader={path+".glsl",path+".metallib",path+".spv",path+".json","main0"};pipeline.threads={8,8,1};pipeline.bindings={{0,{{0,BindingType::UniformBuffer,ShaderStage::Compute,"TerrainParameters",240},{1,BindingType::SampledTexture,ShaderStage::Compute,"heightPageTable",0}}},{1,{{0,BindingType::StorageWrite,ShaderStage::Compute,"GeneratedVertices",0},{2,BindingType::StorageRead,ShaderStage::Compute,"FinalNodeList",16},{3,BindingType::StorageWrite,ShaderStage::Compute,"GeneratedIndices",0},{5,BindingType::StorageReadWrite,ShaderStage::Compute,"OutIndirect",20},{6,BindingType::SampledTexture,ShaderStage::Compute,"heightAtlas",0},{7,BindingType::StorageTextureRead,ShaderStage::Compute,"lodMap",0}}}};
        auto kernel=test.computePipeline(pipeline);auto height=mixed.heightTexture();auto a=test.bindings({pipeline.bindings[0],{{0,data,0,240,{},{}},{1,{},0,0,height->pageTable(),height->sampler()}}});auto b=test.bindings({pipeline.bindings[1],{{0,mixed.mesh()->vertexBuffer(),0,size_t(2048)*64*4*32,{},{}},{2,mixed.leafQueue(),0,16+2048*16,{},{}},{3,mixed.mesh()->indexBuffer(),0,size_t(2048)*64*6*4,{},{}},{5,mixed.mesh()->indirectBuffer(),0,20,{},{}},{6,{},0,0,height->atlas(),height->sampler()},{7,{},0,0,lodView,{}}}});
        auto commands=d->createCommandList();commands.dispatchIndirect(kernel,{a,b},mixed.leafQueue());d->submit(commands);checkClosed(mixed);std::cout<<"Terrain forced LOD 0 / 5 transitions: closed manifold, coverage and winding validated\n";
    }
    std::cout<<"RHI terrain indirect subdivision queues, screen-error LOD, LOD neighbor stitching, height/normal/index generation and indexed indirect drawing passed; leaves "<<leaves<<"\n";
}
}
