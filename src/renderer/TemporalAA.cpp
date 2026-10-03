#include "renderer/TemporalAA.h"
#include "renderer/RenderScene.h"
#include "renderer/Texture.h"
#include "renderer/RenderPass.h"
#include "buffer/FrameBuffer.h"
#include "component/Ocean.h"
#include "component/Atmosphere.h"
#include "component/Lights.h"
#include "object/Terrain.h"
#include "object/SkyBox.h"
#include "system/InputManager.h"
#include "system/RenderManager.h"
#include "utils/Shader.h"
#include "utils/Camera.h"
#include <glad/glad.h>
#include <algorithm>
#include <cmath>
#include <functional>
namespace {
float halton(unsigned i,unsigned base){float f=1,result=0;while(i){f/=base;result+=f*(i%base);i/=base;}return result;}
size_t signature(const std::shared_ptr<RenderScene>& scene) {
    auto manager=RenderManager::GetInstance();size_t hash=0;
    auto add=[&](float x){hash^=std::hash<float>{}(x)+0x9e3779b9+(hash<<6)+(hash>>2);};
    auto s=manager->setting;for(bool x:{s.enableRSM,s.enableShadow,s.enableSSAO,s.enableDirectional})add(x);
    if(manager->rsmPass){auto r=manager->rsmPass;for(float x:{float(r->indirectOnly),float(r->sunBounce),float(r->skyBounce),r->intensity,r->sampleRadius,float(r->sampleCount),r->worldRadius,r->minDistance,float(r->useSunSky)})add(x);}
    if(scene->sky()){auto a=std::static_pointer_cast<Atmosphere>(scene->sky()->GetComponent("Atmosphere"));if(a)add(a->settings().sunAngle);}
    for(auto light:scene->directionLights())if(light){add(light->isEnabled());for(int c=0;c<3;++c){add(light->getData().color[c]);add(light->getData().direction[c]);}}
    if(scene->terrain()) {
        auto o=std::static_pointer_cast<Ocean>(scene->terrain()->GetComponent("Ocean"));
        if(o)for(float x:{float(o->settings().seed),float(o->settings().detailWaves),o->settings().detailStrength,float(o->settings().refraction),o->settings().refractionStrength,o->settings().subsurfaceStrength,o->settings().HeightScale,o->settings().Lambda,o->settings().A,o->settings().WindScale,o->settings().WindAndSeed.x,o->settings().WindAndSeed.y,o->settings().seaLevel,o->settings().absorption.x,o->settings().absorption.y,o->settings().absorption.z,o->settings().scattering.x,o->settings().scattering.y,o->settings().scattering.z})add(x);
    }
    return hash;
}
std::shared_ptr<Texture> texture(unsigned format,unsigned channels,int w,int h) {
    auto t=std::make_shared<Texture>();t->genTexture(format,channels,w,h);glBindTexture(GL_TEXTURE_2D,t->gpuId());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);return t;
}
}
glm::vec2 TemporalAA::jitterSample(unsigned frame) {
    glm::vec2 mean(0);for(unsigned i=1;i<=16;++i)mean+=glm::vec2(halton(i,2),halton(i,3));mean/=16.f;
    unsigned index=frame%16+1;return glm::vec2(halton(index,2),halton(index,3))-mean;
}
void TemporalAA::reset(){valid=false;samples=0;}
unsigned TemporalAA::motionTexture() const {return motion?motion->gpuId():0;}
unsigned TemporalAA::outputTexture() const {return enabled && valid?color[1-writeIndex]->gpuId():0;}
void TemporalAA::allocate(int w,int h) {
    width=w;height=h;writeIndex=0;reset();
    for(int i=0;i<2;++i){color[i]=texture(GL_RGBA16F,GL_RGBA,w,h);depth[i]=texture(GL_RGBA32F,GL_RGBA,w,h);glBindTexture(GL_TEXTURE_2D,depth[i]->gpuId());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);}
    currentDepth=texture(GL_DEPTH_COMPONENT32F,GL_DEPTH_COMPONENT,w,h);motion=texture(GL_RGBA16F,GL_RGBA,w,h);
    depthBuffer=std::make_shared<FrameBuffer>();depthBuffer->bindTexture(currentDepth,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D);glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);
    motionBuffer=std::make_shared<FrameBuffer>();motionBuffer->bindTexture(motion,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D);
    if(!shader)shader=std::make_shared<Shader>("./src/shader/post/tsaa.comp");
}
void TemporalAA::begin(const std::shared_ptr<RenderScene>& scene,bool enable) {
    if(!enable || !scene || !scene->mainCamera()){enabled=false;reset();return;}
    auto camera=scene->mainCamera();auto input=InputManager::GetInstance();auto baseProjection=camera->GetPerspective();
    size_t state=signature(scene);bool changed=!enabled || lastScene.lock()!=scene || lastCamera!=camera.get() || state!=lastSignature;
    if(width!=input->width || height!=input->height)allocate(input->width,input->height);
    if(glm::length(camera->getPosition()-lastPosition)>3 || glm::dot(camera->getFront(),lastFront)<.8f)changed=true;
    for(int c=0;c<4;++c)for(int r=0;r<4;++r)changed |= std::abs(baseProjection[c][r]-lastProjection[c][r])>1e-5;
    if(changed)reset();enabled=true;lastScene=scene;lastCamera=camera.get();lastSignature=state;
    lastPosition=camera->getPosition();lastFront=camera->getFront();lastProjection=baseProjection;
    projection=baseProjection;view=camera->GetViewMatrix();auto jitter=jitterSample(samples);
    projection[2][0]-=2*jitter.x/width;projection[2][1]-=2*jitter.y/height;currentVP=projection*view;
    if(!valid){previousVP=currentVP;previousView=view;}
    motionBuffer->bindBuffer();glViewport(0,0,width,height);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);glClearColor(0,0,0,1);
}
unsigned TemporalAA::resolve(unsigned source,unsigned framebuffer) {
    if(!enabled)return source;
    glBindFramebuffer(GL_READ_FRAMEBUFFER,framebuffer);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,depthBuffer->FBO);
    glBlitFramebuffer(0,0,width,height,0,0,width,height,GL_DEPTH_BUFFER_BIT,GL_NEAREST);
    shader->use();const unsigned textures[]={source,currentDepth->gpuId(),color[1-writeIndex]->gpuId(),depth[1-writeIndex]->gpuId(),motion->gpuId()};
    const char* names[]={"currentColor","currentDepth","historyColor","historyDepth","surfaceMotion"};
    for(int i=0;i<5;++i){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,textures[i]);shader->setInt(names[i],i);}
    glBindImageTexture(0,color[writeIndex]->gpuId(),0,GL_FALSE,0,GL_WRITE_ONLY,GL_RGBA16F);
    glBindImageTexture(1,depth[writeIndex]->gpuId(),0,GL_FALSE,0,GL_WRITE_ONLY,GL_RGBA32F);
    shader->setMat4("inverseVP",glm::inverse(currentVP));shader->setMat4("currentView",view);
    shader->setMat4("previousVP",previousVP);shader->setMat4("previousView",previousView);
    shader->setVec3("cameraPosition",lastPosition);shader->setInt("historyValid",valid?1:0);
    shader->setFloat("historyWeight",std::min(historyWeight,float(samples)/float(samples+1)));
    glDispatchCompute((width+7)/8,(height+7)/8,1);glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT);
    unsigned result=color[writeIndex]->gpuId();writeIndex=1-writeIndex;valid=true;++samples;previousVP=currentVP;previousView=view;
    return result;
}
