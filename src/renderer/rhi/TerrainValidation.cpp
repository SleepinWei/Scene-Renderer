#include "renderer/rhi/GpuTerrain.h"
#include "renderer/rhi/GpuGrass.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
namespace render {
namespace{void check(bool v,const char* r){if(!v)throw std::runtime_error(r);}}
void validateTerrainRhi(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& directory){
    if(!d->computeLimits().maxStorageImages){std::cout<<"RHI terrain GPU queues require compute; deferred on this backend\n";return;}
    std::vector<float> h(32*32);for(int y=0;y<32;y++)for(int x=0;x<32;x++)h[y*32+x]=.2f+.1f*x/31-.07f*y/31;
    auto terrainOwner=std::make_shared<GpuTerrain>(d,directory,32,32,h);auto& terrain=*terrainOwner;FrameData f;f.cameraPosition={0,6,12};f.view=glm::lookAt(f.cameraPosition,glm::vec3(0),glm::vec3(0,1,0));glm::mat4 correction(1);correction[2][2]=.5f;correction[3][2]=.5f;f.viewProjection=correction*glm::perspective(glm::radians(65.f),1.f,.1f,100.f)*f.view;
    const auto model=glm::scale(glm::mat4(1),glm::vec3(8,3,8));terrain.update(f,model);auto nodes=terrain.readNodes();auto args=terrain.readArguments();const uint32_t leaves=uint32_t(nodes.size()/4);
    check(leaves>=25 && args.indexCount==leaves*64*6 && args.instanceCount==1 && !args.firstIndex && !args.baseVertex && !args.firstInstance,"Terrain generated indirect ABI/count invalid");
    std::vector<std::array<uint32_t,3>> expected,actual;std::function<void(uint32_t,uint32_t,uint32_t)> visit=[&](uint32_t x,uint32_t y,uint32_t level){float nearest=1e30f;const float n=float(5u<<level);for(auto offset:{glm::vec2(0,0),glm::vec2(1,0),glm::vec2(1,1),glm::vec2(0,1)}){glm::vec2 uv=(glm::vec2(x,y)+offset)/n;glm::vec3 p(uv.x*2-1,.2f+.1f*uv.x-.07f*uv.y,uv.y*2-1);nearest=std::min(nearest,glm::length(glm::vec3(f.view*model*glm::vec4(p,1))));}int lod=nearest<5?0:nearest>50?5:int((nearest-5)/45*4+1);if(lod+int(level)<5){visit(x*2,y*2,level+1);visit(x*2+1,y*2,level+1);visit(x*2+1,y*2+1,level+1);visit(x*2,y*2+1,level+1);}else expected.push_back({x,y,5-level});};
    for(uint32_t y=0;y<5;y++)for(uint32_t x=0;x<5;x++)visit(x,y,0);for(size_t i=0;i<nodes.size();i+=4)actual.push_back({nodes[i],nodes[i+1],nodes[i+2]});std::sort(expected.begin(),expected.end());std::sort(actual.begin(),actual.end());check(actual==expected,"Terrain indirect LOD queue differs from CPU quadtree");
    auto vertices=terrain.readVertices(args.indexCount/6*4);auto indices=terrain.readIndices(args.indexCount);for(const auto& v:vertices){check(std::isfinite(v.position.x)&&std::isfinite(v.normal.y),"Terrain generated vertex nonfinite");check(v.position.x>=-1.001f && v.position.x<=1.001f && v.position.z>=-1.001f && v.position.z<=1.001f,"Terrain stitched vertex outside domain");const float height=.2f+.1f*(v.position.x*.5f+.5f)-.07f*(v.position.z*.5f+.5f);check(std::abs(v.position.y-height)<.0001f,"Terrain bilinear height/stitching failed");check(std::abs(glm::length(v.normal)-1)<.0001f,"Terrain normal not normalized");}for(auto i:indices)check(i<vertices.size(),"Terrain generated index overflow");
    MaterialDesc desc;desc.parameters.albedoAlpha={.3f,.5f,.15f,1};desc.parameters.factors={0,.8f,1,0};auto material=std::make_shared<GpuMaterial>(d,desc);f.lights={{{0,0,0,0},{4,4,4,0},{0,-1,-.1f,0}}};ForwardPbrRenderer renderer(d,directory,64,64,PbrPath::Deferred);renderer.render(f,{{terrain.mesh(),material,model}});size_t valid=0;auto positions=renderer.readGBuffer(0);for(size_t i=3;i<positions.size();i+=4)if(positions[i]==1)++valid;check(valid>100,"Terrain compute -> indexed indirect drawing produced no surface");
    GpuGrass grass(d,directory,terrainOwner,model,4096);grass.update(model,0);auto grassArgs=grass.readArguments();uint32_t expectedGrass=0;for(size_t i=0;i<nodes.size();i+=4)if(nodes[i+2]<=1)expectedGrass+=256;
    check(grassArgs.indexCount==3 && grassArgs.instanceCount==std::min(expectedGrass,4096u) && grassArgs.instanceCount>0 && !grassArgs.firstInstance,"Grass indirect pose capacity/count invalid");
    auto poses=grass.readPoses(grassArgs.instanceCount);for(const auto& p:poses){for(int c=0;c<4;c++)for(int r=0;r<4;r++)check(std::isfinite(p[c][r]),"Grass pose nonfinite");check(std::abs(p[3].x)<=8.01f && std::abs(p[3].z)<=8.01f,"Grass pose outside terrain");const float y=3*(.2f+.1f*(p[3].x/16+.5f)-.07f*(p[3].z/16+.5f));check(std::abs(p[3].y-y)<.0001f,"Grass stem detached from height field");}
    renderer.render(f,{{grass.mesh(),material,glm::mat4(1)}});auto grassPixels=renderer.readGBuffer(0);size_t blades=0;for(size_t i=3;i<grassPixels.size();i+=4)if(grassPixels[i]==1)++blades;check(blades>5,"Grass storage vertex -> indexed instanced indirect draw missing");
    grass.update(model,1);auto wind=grass.readPoses(grass.readArguments().instanceCount);bool bending=false;for(const auto& p:wind)if(std::abs(p[1].x)>.001f)bending=true;check(bending,"Grass wind did not bend blades");
    std::cout<<"RHI grass GPU poses, height attachment, bounded indirect instance generation, vertex storage sampling and wind passed; instances "<<grassArgs.instanceCount<<", pixels "<<blades<<"\n";
    f.cameraPosition={0,70,100};f.view=glm::lookAt(f.cameraPosition,glm::vec3(0),glm::vec3(0,1,0));terrain.update(f,model);check(terrain.readNodes().size()/4==25,"Terrain distant view did not collapse LOD tree");
    std::cout<<"RHI terrain indirect subdivision queues, CPU quadtree comparison, LOD neighbor stitching, height/normal/index generation and indexed indirect drawing passed; leaves "<<leaves<<"\n";
}
}
