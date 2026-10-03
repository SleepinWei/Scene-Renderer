#include "renderer/rhi/ForwardPbrRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
namespace render {
namespace{void check(bool v,const char* reason){if(!v)throw std::runtime_error(reason);}}
void validateTemporalRhi(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory){
    if(!device->computeLimits().maxStorageImages)return;
    std::vector<MeshVertex> vertices{{{-.2f,-.2f,0},{0,0,1},{0,1}},{{.2f,-.2f,0},{0,0,1},{1,1}},{{.2f,.2f,0},{0,0,1},{1,0}},{{-.2f,.2f,0},{0,0,1},{0,0}}};auto mesh=std::make_shared<GpuMesh>(device,vertices,std::vector<uint32_t>{0,1,2,0,2,3});MaterialDesc m;m.parameters.albedoAlpha={0,0,0,1};m.parameters.emissiveNormal={4,.2f,.1f,0};auto material=std::make_shared<GpuMaterial>(device,m);
    FrameData f;f.taa=true;f.view=glm::lookAt(glm::vec3(0,0,3),glm::vec3(0),glm::vec3(0,1,0));glm::mat4 correction(1);correction[2][2]=.5f;correction[3][2]=.5f;f.viewProjection=correction*glm::ortho(-1.f,1.f,-1.f,1.f,.1f,10.f)*f.view;
    ForwardPbrRenderer renderer(device,directory,64,64,PbrPath::Scene);std::vector<DrawPacket> draws{{mesh,material,glm::mat4(1),42}};
    for(int i=0;i<4;i++){renderer.render(f,draws);auto hdr=renderer.readHDR();for(float v:hdr)check(std::isfinite(v),"Temporal resolve nonfinite");check(std::abs(hdr[(32*64+32)*4]-4)<.01f,"Temporal static HDR changed/clamped");}
    draws[0].model[3].x=.7f;renderer.render(f,draws);auto moved=renderer.readHDR();check(moved[(32*64+32)*4]<.01f && moved[(32*64+54)*4]>3.9f,"Temporal object motion/disocclusion left an emissive trail");
    renderer.resize(48,32);renderer.render(f,draws);check(renderer.readOutput().size()==48*32*4,"Temporal resize failed");f.taa=false;renderer.render(f,draws);f.taa=true;renderer.render(f,draws);for(float v:renderer.readHDR())check(std::isfinite(v),"Temporal history reset failed");
    f.historyKey=2;renderer.render(f,draws);for(float v:renderer.readHDR())check(std::isfinite(v),"Temporal scene change reset failed");
    std::cout<<"RHI temporal HDR resolve, jitter, object motion, disocclusion rejection, resize, enable/disable and scene history reset passed\n";
}
}
