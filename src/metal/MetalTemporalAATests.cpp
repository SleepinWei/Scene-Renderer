#include "metal/MetalBackend.h"
#include "metal/MetalDemo.h"
#include "renderer/TemporalAA.h"
#include "renderer/Texture.h"
#include "renderer/RenderPass.h"
#include "renderer/RenderScene.h"
#include "buffer/UniformBuffer.h"
#include "component/Ocean.h"
#include "object/Terrain.h"
#include "system/RenderManager.h"
#include "system/InputManager.h"
#include "utils/Camera.h"
#include "utils/Shader.h"
#include <glad/glad.h>
#include <cmath>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <stdexcept>
#include <iostream>
extern std::shared_ptr<RenderScene> scene;
namespace {
int cases=0;
void check(bool ok,const char* label){if(!ok)throw std::runtime_error(std::string("TSAA regression: ")+label);++cases;}
std::shared_ptr<Texture> texture(int w,int h,unsigned format,const std::vector<glm::vec4>& values={}) {
    auto t=std::make_shared<Texture>();t->genTexture(format,GL_RGBA,w,h);glBindTexture(GL_TEXTURE_2D,t->id);
    if(!values.empty())glTexImage2D(GL_TEXTURE_2D,0,format,w,h,0,GL_RGBA,GL_FLOAT,values.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);return t;
}
std::shared_ptr<Texture> constant(glm::vec4 v){return texture(8,8,GL_RGBA32F,std::vector<glm::vec4>(64,v));}
void resolveTests() {
    auto shader=std::make_shared<Shader>("./src/shader/post/tsaa.comp");
    std::vector<glm::vec4> pattern(64);
    for(int y=0;y<8;++y)for(int x=0;x<8;++x)pattern[y*8+x]=glm::vec4(glm::vec3((x+y)%2?1.f:0.f),1);
    pattern[36]=glm::vec4(.25f,.25f,.25f,1);
    auto current=texture(8,8,GL_RGBA32F,pattern),z=constant({.5,0,0,1}),history=constant({.5,.5,.5,1});
    auto historyDepth=constant({1,0,0,1}),motion=constant({0,0,0,0});
    auto output=texture(8,8,GL_RGBA16F),depthOutput=texture(8,8,GL_RGBA32F);
    glm::mat4 view(1);view[3][2]=-1;bool skyFixture=false;
    auto perspective=glm::perspective(glm::radians(60.f),1.f,.1f,100.f);
    auto run=[&](bool valid,glm::mat4 previous=glm::mat4(1)) {
        shader->use();const std::shared_ptr<Texture> textures[]={current,z,history,historyDepth,motion};
        const char* names[]={"currentColor","currentDepth","historyColor","historyDepth","surfaceMotion"};
        for(int i=0;i<5;++i){textures[i]->bind(GL_TEXTURE_2D,i);shader->setInt(names[i],i);}
        glBindImageTexture(0,output->id,0,GL_FALSE,0,GL_WRITE_ONLY,GL_RGBA16F);glBindImageTexture(1,depthOutput->id,0,GL_FALSE,0,GL_WRITE_ONLY,GL_RGBA32F);
        shader->setMat4("inverseVP",skyFixture?glm::inverse(perspective):glm::mat4(1));shader->setMat4("currentView",view);shader->setMat4("previousVP",previous);shader->setMat4("previousView",view);
        shader->setVec3("cameraPosition",glm::vec3(0));shader->setFloat("historyWeight",.9);shader->setInt("historyValid",valid?1:0);
        glDispatchCompute(1,1,1);glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT);
        return MetalBackend::readFloatTexture(output->id).rgba[4*36];
    };
    MetalBackend::beginFrame();
    check(std::abs(run(false)-.25)<1e-4,"fresh history uses current HDR exactly");
    // Known greyscale neighborhood: h=1/3, c=1/5, feedback weight=.6.
    check(std::abs(run(true)-(.28/.72))<1e-3,"reprojected history blend agrees with analytic HDR compression");
    auto outside=glm::mat4(1);outside[3][0]=4;check(std::abs(run(true,outside)-.25)<1e-4,"out-of-screen history rejected");
    historyDepth=constant({2,0,0,1});check(std::abs(run(true)-.25)<1e-4,"depth disocclusion rejects stale history");historyDepth=constant({1,0,0,1});
    motion=constant({0,0,1,1});double encoded=.2+(.85*(2./3))*(1./3-.2);
    check(std::abs(run(true)-encoded/(1-encoded))<1e-3,"water motion uses responsive history weighting");
    motion=constant({2,0,1,1});check(std::abs(run(true)-.25)<1e-4,"invalid water reprojection rejected");motion=constant({0,0,0,0});
    history=constant({std::numeric_limits<float>::quiet_NaN(),0,0,1});check(std::abs(run(true)-.25)<1e-4,"non-finite history rejected");
    history=constant({100,100,100,1});current=constant({.25,.25,.25,1});check(std::abs(run(true)-.25)<1e-4,"neighborhood clipping removes bright ghost history");
    history=current=constant({4,4,4,1});check(std::abs(run(true)-4)<.005,"HDR energy above one retained");
    auto rawDepth=MetalBackend::readFloatTexture(depthOutput->id);check(std::abs(rawDepth.rgba[4*36]-1)<1e-6,"history stores view-space linear depth");
    current=texture(8,8,GL_RGBA32F,pattern);history=constant({.5,.5,.5,1});
    z=constant({1,0,0,1});historyDepth=constant({0,0,0,1});skyFixture=true;
    glm::mat4 translation(1);translation[3][0]=10;
    check(std::abs(run(true,perspective*translation)-(.28/.72))<1e-3,"sky reprojection ignores camera translation");
    check(MetalBackend::readFloatTexture(depthOutput->id).rgba[4*36]==0,"sky history uses an explicit zero-depth sentinel");
    skyFixture=false;z=constant({.5,0,0,1});historyDepth=constant({1,0,0,1});
    // Temporal aliasing fixture: complementary checkerboards on a stationary depth layer.
    history=constant({0,0,0,1});double minimum=1e10,maximum=-1e10;
    for(int frame=0;frame<20;++frame) {
        for(int y=0;y<8;++y)for(int x=0;x<8;++x)pattern[y*8+x]=glm::vec4(glm::vec3((x+y+frame)%2?1.f:0.f),1);
        current=texture(8,8,GL_RGBA32F,pattern);float value=run(frame!=0);
        if(frame>=8){minimum=std::min(minimum,double(value));maximum=std::max(maximum,double(value));}
        history=output;output=texture(8,8,GL_RGBA16F);
    }
    std::cout<<"TSAA synthetic temporal range="<<maximum-minimum<<" (unfiltered=1)\n";
    check(maximum-minimum<.6,"temporal checkerboard flicker is reduced");
    MetalBackend::present();
}
void lifecycleAndOcean() {
    auto manager=RenderManager::GetInstance();auto savedScene=scene;auto saved=manager->setting;
    auto input=InputManager::GetInstance();int width=input->width,height=input->height;
    manager->uniformVPBuffer->setBinding(0);scene=makeMetalOceanScene();
    auto water=std::static_pointer_cast<Ocean>(scene->terrain()->GetComponent("Ocean"));water->FFTPow=6;water->fft_size=64;water->MeshSize=65;water->animate=false;water->inner_time=8;
    manager->setting.enableTSAA=true;
    auto frame=[&](){MetalBackend::beginFrame();manager->render(scene);MetalBackend::present();};
    frame();check(manager->temporalAA->frameCount()==1 && manager->temporalAA->historyValid(),"first frame seeds history");
    frame();check(manager->temporalAA->frameCount()==2,"stationary frames accumulate history");
    auto motion=MetalBackend::readFloatTexture(manager->temporalAA->motionTexture());
    auto expected=(TemporalAA::jitterSample(0)-TemporalAA::jitterSample(1))/glm::vec2(width,height);
    size_t waterPixels=0;bool correct=true;
    for(size_t i=0;i<motion.rgba.size();i+=4)if(motion.rgba[i+3]>.5){++waterPixels;correct &= std::abs(motion.rgba[i]-expected.x)<3e-4 && std::abs(motion.rgba[i+1]-expected.y)<3e-4 && motion.rgba[i+2]>0;}
    check(waterPixels>100 && correct,"static ocean motion includes the exact previous/current jitter offset");
    water->inner_time+=.1f;frame();motion=MetalBackend::readFloatTexture(manager->temporalAA->motionTexture());
    expected=(TemporalAA::jitterSample(1)-TemporalAA::jitterSample(2))/glm::vec2(width,height);double displacement=0;
    for(size_t i=0;i<motion.rgba.size();i+=4)if(motion.rgba[i+3]>.5)displacement+=std::abs(motion.rgba[i]-expected.x)+std::abs(motion.rgba[i+1]-expected.y);
    check(displacement>1e-4,"animated FFT displacement generates surface motion beyond camera jitter");
    scene->mainCamera()->setPosition(scene->mainCamera()->getPosition()+glm::vec3(10,0,0));manager->temporalAA->begin(scene,true);check(!manager->temporalAA->historyValid() && manager->temporalAA->frameCount()==0,"camera cut invalidates history");frame();
    manager->setting.enableSSAO=!manager->setting.enableSSAO;frame();check(manager->temporalAA->frameCount()==1,"lighting/settings changes start fresh history");
    input->width=width/2;input->height=height/2;manager->temporalAA->begin(scene,true);check(!manager->temporalAA->historyValid(),"resize reallocates and resets history");input->width=width;input->height=height;
    manager->setting.enableTSAA=false;frame();check(!manager->temporalAA->active() && manager->temporalAA->outputTexture()==0,"disabled TSAA bypasses history and jitter");
    manager->setting.enableTSAA=true;frame();check(manager->temporalAA->frameCount()==1,"reenabling TSAA starts fresh history");
    scene=makeMetalOceanScene();frame();check(manager->temporalAA->frameCount()==1,"scene switch cannot reuse previous scene history");
    scene=savedScene;manager->setting=saved;
}
}
void validateMetalTemporalAA() {
    cases=0;glm::vec2 mean(0);bool bounded=true,unique=true;
    for(unsigned i=0;i<16;++i){auto p=TemporalAA::jitterSample(i);mean+=p;bounded &= std::abs(p.x)<.6 && std::abs(p.y)<.6;for(unsigned j=0;j<i;++j)unique &= glm::length(p-TemporalAA::jitterSample(j))>1e-5;}
    check(glm::length(mean)<1e-5 && bounded && unique,"16 Halton samples are centered, bounded and unique");
    check(glm::length(TemporalAA::jitterSample(0)-TemporalAA::jitterSample(16))<1e-7,"jitter sequence repeats deterministically");
    resolveTests();lifecycleAndOcean();std::cout<<"TSAA analytic and lifecycle GPU tests passed ("<<cases<<" cases)\n";
}
