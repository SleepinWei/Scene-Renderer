#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include "component/Ocean.h"
#include "component/Atmosphere.h"
#include "component/Lights.h"
#include "renderer/Texture.h"
#include "renderer/RenderScene.h"
#include "renderer/RenderPass.h"
#include "renderer/TemporalAA.h"
#include "buffer/FrameBuffer.h"
#include "system/InputManager.h"
#include "object/SkyBox.h"
#include "system/RenderManager.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
extern std::shared_ptr<RenderScene> scene;

Ocean::Ocean() { Component::name="Ocean"; }
Ocean::~Ocean() {
    if(VAO)glDeleteVertexArrays(1,&VAO);
    if(VBO)glDeleteBuffers(1,&VBO);
    if(EBO)glDeleteBuffers(1,&EBO);
}
void Ocean::render() {
    if(!initDone)Start();
    Update();Draw();
}
void Ocean::simulate(float seconds) {
    if(!initDone)Start();
    inner_time=seconds;ComputeOceanValue();
}
void Ocean::Start() {
    if(FFTPow<3 || FFTPow>11 || fft_size!=(1<<FFTPow) || MeshSize<2 || !std::isfinite(MeshLength) || MeshLength<=0)
        throw std::invalid_argument("Ocean requires fft_size=2^FFTPow (8..2048), MeshSize>=2 and positive length");
    initMesh();initTextures();initShaders();initGaussianRandom();
    initializedSize=fft_size;initializedMeshSize=MeshSize;initializedLength=MeshLength;initializedSeed=seed;
    lastFrame=static_cast<float>(glfwGetTime());initDone=true;
}
void Ocean::initTextures() {
    for(auto* target : {&GaussianRandomRT_Texture,&HeightSpectrumRT_Texture,&DisplaceXSpectrumRT_Texture,
                       &DisplaceZSpectrumRT_Texture,&OutputRT_Texture,&DisplaceRT_Texture,&NormalRT_Texture,&BubblesRT_Texture}) {
        *target=std::make_shared<ImageTexture>();(*target)->genImageTexture(GL_RGBA32F,GL_RGBA,fft_size,fft_size);
        glBindTexture(GL_TEXTURE_2D,(*target)->tex->gpuId());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    }
}
void Ocean::initShaders() {
    GaussianRandomRT_Shader=std::make_shared<Shader>("./src/shader/ocean/ocean_ComputeGaussianRandom.comp");
    HeightSpectrum_Shader=std::make_shared<Shader>("./src/shader/ocean/ocean_CreateHeightSpectrum.comp");
    DisplaceSpectrum_Shader=std::make_shared<Shader>("./src/shader/ocean/ocean_CreateDisplaceSpectrum.comp");
    FFTHorizontal_Shader=std::make_shared<Shader>("./src/shader/ocean/ocean_FFTHorizontal.comp");
    FFTHorizontalEnd_Shader=std::make_shared<Shader>("./src/shader/ocean/ocean_FFTHorizontalEnd.comp");
    FFTVertical_Shader=std::make_shared<Shader>("./src/shader/ocean/ocean_FFTVertical.comp");
    FFTVerticalEnd_Shader=std::make_shared<Shader>("./src/shader/ocean/ocean_FFTVerticalEnd.comp");
    TextureDisplace_Shader=std::make_shared<Shader>("./src/shader/ocean/ocean_TextureGenerationDisplace.comp");
    TextureNormalBubbles_Shader=std::make_shared<Shader>("./src/shader/ocean/ocean_TextureGenerationNormalBubbles.comp");
    draw_shader=std::make_shared<Shader>("./src/shader/ocean/ocean.vs","./src/shader/ocean/ocean.fs");
}
void Ocean::initGaussianRandom() {
    GaussianRandomRT_Shader->use();GaussianRandomRT_Shader->setInt("N",fft_size);
    GaussianRandomRT_Shader->setInt("Seed",seed);GaussianRandomRT_Texture->setBinding(1);
    glDispatchCompute(fft_size/8,fft_size/8,1);glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}
void Ocean::initMesh() {
    vertexInfo.resize(5*MeshSize*MeshSize);vertexIndexs.clear();
    for(int z=0;z<MeshSize;++z)for(int x=0;x<MeshSize;++x) {
        int index=z*MeshSize+x;float u=float(x)/(MeshSize-1),v=float(z)/(MeshSize-1);
        vertexInfo[5*index]=(u-.5f)*MeshLength;vertexInfo[5*index+1]=0;
        vertexInfo[5*index+2]=(v-.5f)*MeshLength;vertexInfo[5*index+3]=u;vertexInfo[5*index+4]=v;
        if(x+1<MeshSize && z+1<MeshSize)vertexIndexs.insert(vertexIndexs.end(),
            {unsigned(index),unsigned(index+MeshSize),unsigned(index+MeshSize+1),
             unsigned(index),unsigned(index+MeshSize+1),unsigned(index+1)});
    }
    glGenVertexArrays(1,&VAO);glGenBuffers(1,&VBO);glGenBuffers(1,&EBO);
    glBindVertexArray(VAO);glBindBuffer(GL_ARRAY_BUFFER,VBO);
    glBufferData(GL_ARRAY_BUFFER,vertexInfo.size()*sizeof(float),vertexInfo.data(),GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,vertexIndexs.size()*sizeof(unsigned),vertexIndexs.data(),GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),nullptr);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
}
void Ocean::ComputeFFT(std::shared_ptr<Shader> shader,std::shared_ptr<ImageTexture> input) {
    shader->use();input->setBinding(5);OutputRT_Texture->setBinding(6);
    glDispatchCompute(fft_size/8,fft_size/8,1);glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    input->tex->swapGpuStorage(*OutputRT_Texture->tex);
}
void Ocean::ComputeOceanValue() {
    if(FFTPow<3 || FFTPow>11 || fft_size!=initializedSize || MeshSize!=initializedMeshSize || MeshLength!=initializedLength || fft_size!=(1<<FFTPow))
        throw std::invalid_argument("Ocean grid/domain changed after initialization; recreate the component");
    if(seed!=initializedSeed){initGaussianRandom();initializedSeed=seed;}
    glm::vec2 wind(WindAndSeed);float length=glm::length(wind);
    wind=length>1e-6f ? wind/length*std::max(WindScale,0.f) : glm::vec2(0);
    HeightSpectrum_Shader->use();HeightSpectrum_Shader->setInt("N",fft_size);
    HeightSpectrum_Shader->setFloat("Time",inner_time);HeightSpectrum_Shader->setFloat("OceanLength",MeshLength);
    HeightSpectrum_Shader->setVec4("WindAndSeed",glm::vec4(wind,0,0));HeightSpectrum_Shader->setFloat("A",std::max(A,0.f));
    GaussianRandomRT_Texture->setBinding(1);HeightSpectrumRT_Texture->setBinding(2);
    glDispatchCompute(fft_size/8,fft_size/8,1);glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    DisplaceSpectrum_Shader->use();DisplaceSpectrum_Shader->setInt("N",fft_size);
    HeightSpectrumRT_Texture->setBinding(2);DisplaceXSpectrumRT_Texture->setBinding(3);DisplaceZSpectrumRT_Texture->setBinding(4);
    glDispatchCompute(fft_size/8,fft_size/8,1);glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    for(int axis=0;axis<2;++axis)for(int stage=1;stage<=FFTPow;++stage) {
        auto shader=axis==0 ? (stage==FFTPow?FFTHorizontalEnd_Shader:FFTHorizontal_Shader) :
                             (stage==FFTPow?FFTVerticalEnd_Shader:FFTVertical_Shader);
        shader->use();shader->setInt("N",fft_size);shader->setInt("Ns",1<<(stage-1));
        for(auto texture : {HeightSpectrumRT_Texture,DisplaceXSpectrumRT_Texture,DisplaceZSpectrumRT_Texture})ComputeFFT(shader,texture);
    }
    TextureDisplace_Shader->use();TextureDisplace_Shader->setInt("N",fft_size);
    TextureDisplace_Shader->setFloat("Lambda",Lambda);TextureDisplace_Shader->setFloat("HeightScale",HeightScale);
    HeightSpectrumRT_Texture->setBinding(2);DisplaceXSpectrumRT_Texture->setBinding(3);
    DisplaceZSpectrumRT_Texture->setBinding(4);DisplaceRT_Texture->setBinding(7);
    glDispatchCompute(fft_size/8,fft_size/8,1);glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    TextureNormalBubbles_Shader->use();TextureNormalBubbles_Shader->setInt("N",fft_size);
    TextureNormalBubbles_Shader->setFloat("OceanLength",MeshLength);
    TextureNormalBubbles_Shader->setFloat("BubblesScale",std::max(BubblesScale,0.f));
    TextureNormalBubbles_Shader->setFloat("BubblesThreshold",BubblesThreshold);
    DisplaceRT_Texture->setBinding(7);NormalRT_Texture->setBinding(5);BubblesRT_Texture->setBinding(6);
    glDispatchCompute(fft_size/8,fft_size/8,1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT);
    if(detailWaves && FFTPow>=6) {
        if(!detailOcean) {
            detailOcean=std::make_shared<Ocean>();detailOcean->detailWaves=false;
            detailOcean->FFTPow=8;detailOcean->fft_size=256;detailOcean->MeshSize=2;detailOcean->MeshLength=24;
        }
        detailOcean->seed=seed^9001;detailOcean->WindAndSeed=WindAndSeed;
        detailOcean->WindScale=std::min(WindScale,6.f);detailOcean->A=A*.3f;
        detailOcean->HeightScale=HeightScale*.5f*std::max(detailStrength,0.f);
        detailOcean->Lambda=Lambda*.3f*std::max(detailStrength,0.f);
        detailOcean->BubblesScale=BubblesScale;detailOcean->BubblesThreshold=BubblesThreshold;
        detailOcean->simulate(inner_time);
    }
}
void Ocean::Update() {
    float now=static_cast<float>(glfwGetTime());deltaTime=std::clamp(now-lastFrame,0.f,.1f);lastFrame=now;
    if(animate)inner_time+=deltaTime*TimeScale;
    ComputeOceanValue();
}
void Ocean::Draw() {
    auto deferred=RenderManager::GetInstance()->deferredPass;
    const int width=InputManager::GetInstance()->width,height=InputManager::GetInstance()->height;
    if(!opaqueSceneColor || opaqueSceneColor->getWidth()!=width || opaqueSceneColor->getHeight()!=height) {
        opaqueSceneColor=std::make_shared<Texture>();opaqueSceneColor->genTexture(GL_RGBA16F,GL_RGBA,width,height);
        opaqueSceneBuffer=std::make_shared<FrameBuffer>();opaqueSceneBuffer->bindTexture(opaqueSceneColor,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D);
    }
    // Copy the lit opaque scene before writing water: never sample the active color attachment.
    glBindFramebuffer(GL_READ_FRAMEBUFFER,deferred->postBuffer->FBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,opaqueSceneBuffer->FBO);
    glBlitFramebuffer(0,0,width,height,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    deferred->postBuffer->bindBuffer();glViewport(0,0,width,height);
    draw_shader->use();glm::vec3 sunDirection(0,-1,0),sunColor(0);
    if(scene && RenderManager::GetInstance()->setting.enableDirectional)
        for(auto light : scene->directionLights())if(light && light->isEnabled()){sunDirection=light->getData().direction;sunColor=light->getData().color;break;}
    draw_shader->setVec3("dirLight.direction",sunDirection);draw_shader->setVec3("dirLight.diffuse",sunColor);
    draw_shader->setVec3("dirLight.specular",sunColor);
    draw_shader->setFloat("outer_FresnelScale",outer_FresnelScale);
    draw_shader->setVec3("outer_OceanColorShallow",outer_OceanColorShallow);
    draw_shader->setVec3("outer_OceanColorDeep",outer_OceanColorDeep);
    draw_shader->setVec3("outer_BubblesColor",outer_BubblesColor);draw_shader->setVec3("outer_Specular",outer_Specular);
    draw_shader->setInt("outer_Gloss",outer_Gloss);draw_shader->setVec3("outer_ambient",outer_ambient);
    DisplaceRT_Texture->tex->bind(GL_TEXTURE_2D,0);draw_shader->setInt("DisplaceRT",0);
    NormalRT_Texture->tex->bind(GL_TEXTURE_2D,1);draw_shader->setInt("NormalRT",1);
    BubblesRT_Texture->tex->bind(GL_TEXTURE_2D,2);draw_shader->setInt("BubblesRT",2);
    glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,0);
    auto atmosphere=scene && scene->sky() ? std::static_pointer_cast<Atmosphere>(scene->sky()->GetComponent("Atmosphere")) : nullptr;
    if(atmosphere && atmosphere->skyViewTexture)glBindTexture(GL_TEXTURE_2D,atmosphere->skyViewTexture->tex->gpuId());
    draw_shader->setInt("hasSky",atmosphere && atmosphere->skyViewTexture ? 1:0);draw_shader->setInt("skyview",3);
    opaqueSceneColor->bind(GL_TEXTURE_2D,4);draw_shader->setInt("opaqueScene",4);
    deferred->gPosition->bind(GL_TEXTURE_2D,5);draw_shader->setInt("scenePosition",5);
    deferred->gNormal->bind(GL_TEXTURE_2D,6);draw_shader->setInt("sceneNormal",6);
    const bool detail=detailWaves && bool(detailOcean);
    (detail?detailOcean->DisplaceRT_Texture:DisplaceRT_Texture)->tex->bind(GL_TEXTURE_2D,7);draw_shader->setInt("detailDisplace",7);
    (detail?detailOcean->NormalRT_Texture:NormalRT_Texture)->tex->bind(GL_TEXTURE_2D,8);draw_shader->setInt("detailNormal",8);
    (detail?detailOcean->BubblesRT_Texture:BubblesRT_Texture)->tex->bind(GL_TEXTURE_2D,9);draw_shader->setInt("detailFoam",9);
    draw_shader->setInt("enableDetail",detail?1:0);draw_shader->setFloat("detailLength",24);
    draw_shader->setInt("enableRefraction",refraction?1:0);
    draw_shader->setFloat("refractionStrength",std::clamp(refractionStrength,0.f,1.f));
    draw_shader->setFloat("deepWaterDistance",std::max(deepWaterDistance,.01f));
    draw_shader->setFloat("subsurfaceStrength",std::max(subsurfaceStrength,0.f));
    draw_shader->setVec3("absorption",glm::max(absorption,glm::vec3(0)));
    draw_shader->setVec3("scattering",glm::max(scattering,glm::vec3(0)));
    draw_shader->setFloat("scatteringAnisotropy",std::clamp(scatteringAnisotropy,-.95f,.95f));
    draw_shader->setFloat("seaLevel",seaLevel);draw_shader->setFloat("waveHeightScale",std::max(std::abs(HeightScale),.01f));
    draw_shader->setMat4("model",glm::translate(glm::mat4(1),glm::vec3(0,seaLevel,0)));
    auto taa=RenderManager::GetInstance()->temporalAA;
    const bool temporal=taa && taa->active();
    draw_shader->setInt("temporalActive",temporal?1:0);
    (previousDisplacement?previousDisplacement:DisplaceRT_Texture->tex)->bind(GL_TEXTURE_2D,10);draw_shader->setInt("previousDisplace",10);
    (previousDetailDisplacement?previousDetailDisplacement:(detail?detailOcean->DisplaceRT_Texture->tex:DisplaceRT_Texture->tex))->bind(GL_TEXTURE_2D,11);draw_shader->setInt("previousDetailDisplace",11);
    draw_shader->setMat4("previousVP",temporal?taa->previousVP:glm::mat4(1));
    draw_shader->setMat4("previousView",temporal?taa->previousView:glm::mat4(1));
    draw_shader->setMat4("previousModel",glm::translate(glm::mat4(1),glm::vec3(0,previousSeaLevel,0)));
    if(temporal) {
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,taa->motionTexture(),0);
        const GLenum targets[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1};glDrawBuffers(2,targets);
    }
    glBindVertexArray(VAO);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,EBO);
    // Shader composites transmission/scattering with the copied scene in HDR; no alpha double-blend.
    glDisable(GL_BLEND);glDrawElements(GL_TRIANGLES,static_cast<int>(vertexIndexs.size()),GL_UNSIGNED_INT,nullptr);
    if(temporal) {
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,0,0);glDrawBuffer(GL_COLOR_ATTACHMENT0);
        if(!copyDisplacementShader)copyDisplacementShader=std::make_shared<Shader>("./src/shader/ocean/ocean_CopyDisplacement.comp");
        auto copy=[&](const std::shared_ptr<Texture>& source,std::shared_ptr<Texture>& target) {
            if(!target || target->getWidth()!=source->getWidth() || target->getHeight()!=source->getHeight()) {
                target=std::make_shared<Texture>();target->genTexture(GL_RGBA32F,GL_RGBA,source->getWidth(),source->getHeight());
                glBindTexture(GL_TEXTURE_2D,target->gpuId());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
            }
            copyDisplacementShader->use();
            glBindImageTexture(0,source->gpuId(),0,GL_FALSE,0,GL_READ_ONLY,GL_RGBA32F);
            glBindImageTexture(1,target->gpuId(),0,GL_FALSE,0,GL_WRITE_ONLY,GL_RGBA32F);
            glDispatchCompute((source->getWidth()+7)/8,(source->getHeight()+7)/8,1);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT);
        };
        copy(DisplaceRT_Texture->tex,previousDisplacement);
        if(detail)copy(detailOcean->DisplaceRT_Texture->tex,previousDetailDisplacement);
        previousSeaLevel=seaLevel;
    }
}

void Ocean::setSettings(OceanConfiguration value) {
    checkLogicThread();
    if(value.fft_size<8 || value.fft_size>2048 || (value.fft_size&(value.fft_size-1)) ||
       value.MeshSize<2 || value.MeshSize>1025 || value.MeshLength<=0)
        throw std::invalid_argument("Invalid ocean domain or mesh resolution");
    value.FFTPow=0;for(int n=value.fft_size;n>1;n>>=1)++value.FFTPow;
    for(float v:{value.MeshLength,value.SpectrumLength,value.TimeScale,value.detailStrength,value.rippleRmsHeight,value.A,value.Lambda,value.HeightScale,
                 value.BubblesScale,value.BubblesThreshold,value.WindScale,value.seaLevel,value.refractionStrength,
                 value.deepWaterDistance,value.subsurfaceStrength,value.scatteringAnisotropy,value.outer_FresnelScale,value.gridFocus,
                 value.shore.length,value.shore.maxDepth,value.shore.swellHeight,value.shore.swellPeriod,value.shore.swellDirection.x,value.shore.swellDirection.y,
                 value.shore.foamStrength,value.shore.foamLifetime,value.shore.dryingTime})
        if(!std::isfinite(v))throw std::invalid_argument("Nonfinite ocean configuration");
    if(value.SpectrumLength<0 || value.A<0 || value.WindScale<0 || value.HeightScale<0 || value.BubblesScale<0 || value.detailStrength<0 || value.rippleRmsHeight<0 || value.rippleRmsHeight>.1f ||
       value.deepWaterDistance<=0 || value.gridFocus<=0 || value.subsurfaceStrength<0 || std::abs(value.scatteringAnisotropy)>=1 ||
       value.outer_FresnelScale<0 || value.outer_FresnelScale>1 || value.outer_Gloss<0 || value.refractionStrength<0)
        throw std::invalid_argument("Invalid ocean optical or spectrum configuration");
    if(value.opticalDebug>5||value.shore.resolution<32||value.shore.resolution>512||(value.shore.resolution%8)||value.shore.length<8||value.shore.length/value.shore.resolution<.125f||value.shore.maxDepth<=0||value.shore.maxDepth>32||
       value.shore.swellHeight<0||value.shore.swellHeight>2||value.shore.swellPeriod<1||value.shore.foamStrength<0||value.shore.foamLifetime<=0||value.shore.dryingTime<=0)
        throw std::invalid_argument("Invalid coastal water configuration");
    for(auto vector:{value.absorption,value.scattering,value.outer_OceanColorShallow,value.outer_OceanColorDeep,
                     value.outer_BubblesColor,value.outer_Specular,value.outer_ambient})
        for(int i=0;i<3;++i)if(!std::isfinite(vector[i]) || vector[i]<0)throw std::invalid_argument("Invalid ocean color/coefficient");
    for(int i=0;i<4;++i)if(!std::isfinite(value.WindAndSeed[i]))throw std::invalid_argument("Invalid ocean wind");
    static_cast<OceanConfiguration&>(*this)=value;invalidate();
}
