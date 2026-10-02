#include "metal/MetalBackend.h"
#include "metal/MetalDemo.h"
#include "buffer/FrameBuffer.h"
#include "renderer/Texture.h"
#include "utils/Shader.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace {
std::shared_ptr<Texture> constant(glm::vec4 value) {
    auto t=std::make_shared<Texture>();t->genTexture(GL_RGBA32F,GL_RGBA,8,8);std::vector<glm::vec4> values(64,value);
    glBindTexture(GL_TEXTURE_2D,t->id);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,8,8,0,GL_RGBA,GL_FLOAT,values.data());return t;
}
void requireWater(bool ok,const char* name){if(!ok)throw std::runtime_error(std::string("Water optics regression: ")+name);}
}
void validateMetalWaterOptics() {
    auto shader=std::make_shared<Shader>("./src/shader/ocean/ocean.vs","./src/shader/ocean/ocean.fs");
    auto output=constant({0,0,0,0});FrameBuffer framebuffer;framebuffer.bindTexture(output,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D);
    GLuint vao,vbo,ubo;glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);glGenBuffers(1,&ubo);glBindVertexArray(vao);
    const float vertices[]={-1,0,-1,0,0, 1,0,-1,1,0, -1,0,1,0,1, 1,0,1,1,1};
    glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),nullptr);glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));glEnableVertexAttribArray(1);
    struct CameraData {glm::mat4 projection,view;glm::vec4 camera;};
    CameraData camera{glm::mat4(0),glm::mat4(1),{0,5,0,0}};
    camera.projection[0][0]=1;camera.projection[2][1]=1;camera.projection[3][3]=1;
    glBindBuffer(GL_UNIFORM_BUFFER,ubo);glBufferData(GL_UNIFORM_BUFFER,sizeof(camera),&camera,GL_STATIC_DRAW);glBindBufferBase(GL_UNIFORM_BUFFER,0,ubo);
    auto zero=constant({0,0,0,1}),up=constant({0,1,0,1});
    auto draw=[&](float depth,glm::vec3 absorption,glm::vec3 scattering,float strength,glm::vec3 color,glm::vec3 sun) {
        auto background=constant(glm::vec4(color,1)),position=constant({0,-depth,0,1});
        MetalBackend::beginFrame();framebuffer.bindBuffer();glViewport(0,0,8,8);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);
        shader->use();shader->setUniformBuffer("VP",0);shader->setMat4("model",glm::mat4(1));
        const std::shared_ptr<Texture> textures[]={zero,up,zero,zero,background,position,up};
        const char* names[]={"DisplaceRT","NormalRT","BubblesRT","skyview","opaqueScene","scenePosition","sceneNormal"};
        for(int i=0;i<7;++i){textures[i]->bind(GL_TEXTURE_2D,i);shader->setInt(names[i],i);}
        shader->setInt("temporalActive",0);shader->setInt("previousDisplace",0);shader->setInt("previousDetailDisplace",0);shader->setInt("enableDetail",0);shader->setInt("detailDisplace",0);shader->setInt("detailNormal",1);shader->setInt("detailFoam",2);shader->setFloat("detailLength",24);shader->setInt("hasSky",0);shader->setInt("enableRefraction",1);shader->setFloat("refractionStrength",0);
        shader->setFloat("deepWaterDistance",40);shader->setFloat("subsurfaceStrength",strength);
        shader->setVec3("absorption",absorption);shader->setVec3("scattering",scattering);shader->setFloat("scatteringAnisotropy",.65);
        shader->setFloat("seaLevel",0);shader->setFloat("waveHeightScale",1);
        shader->setFloat("outer_FresnelScale",0);shader->setVec3("outer_OceanColorShallow",glm::vec3(1));shader->setVec3("outer_OceanColorDeep",glm::vec3(1));
        shader->setVec3("outer_BubblesColor",glm::vec3(1));shader->setVec3("outer_Specular",glm::vec3(1));shader->setInt("outer_Gloss",64);
        shader->setVec3("outer_ambient",glm::vec3(0));shader->setVec3("dirLight.direction",{0,-1,0});shader->setVec3("dirLight.diffuse",sun);shader->setVec3("dirLight.specular",sun);
        glBindVertexArray(vao);glDrawArrays(GL_TRIANGLE_STRIP,0,4);auto raw=MetalBackend::readFloatTexture(output->id);MetalBackend::present();
        int pixel=4*(4*8+4);return glm::vec3(raw.rgba[pixel],raw.rgba[pixel+1],raw.rgba[pixel+2]);
    };
    glm::vec3 color(.8,.4,.2),absorption(.4,.2,.1);
    auto shallow=draw(1,absorption,glm::vec3(0),0,color,glm::vec3(0));
    auto deep=draw(5,absorption,glm::vec3(0),0,color,glm::vec3(0));
    float path=std::sqrt(25.f+2*.125f*.125f),NoV=5/std::sqrt(25.f+2*.125f*.125f),F=std::pow(1-NoV,5);
    glm::vec3 expected=(1-F)*color*glm::exp(-absorption*path);
    requireWater(glm::length(deep-expected)<1e-5,"Beer-Lambert attenuation versus analytic RGB result");
    requireWater(shallow.x>deep.x && shallow.y>deep.y && shallow.z>deep.z,"shallow water transmits more than deep water");
    auto clear=draw(5,glm::vec3(0),glm::vec3(0),0,color,glm::vec3(0));
    requireWater(glm::length(clear-(1-F)*color)<1e-5,"zero extinction preserves underwater scene color");
    auto foreground=draw(-1,absorption,glm::vec3(0),0,color,glm::vec3(0));
    requireWater(glm::length(foreground)<1e-6,"above-water foreground is rejected from transmission");
    auto off=draw(5,glm::vec3(.02),glm::vec3(.1),0,glm::vec3(0),glm::vec3(2));
    auto on=draw(5,glm::vec3(.02),glm::vec3(.1),1,glm::vec3(0),glm::vec3(2));
    requireWater(on.x>off.x+1e-4 && std::isfinite(on.x),"subsurface toggle produces finite positive in-scattering");
    glEnable(GL_DEPTH_TEST);glEnable(GL_CULL_FACE);glDeleteVertexArrays(1,&vao);glDeleteBuffers(1,&vbo);glBindBufferBase(GL_UNIFORM_BUFFER,0,0);glBindBuffer(GL_UNIFORM_BUFFER,0);glDeleteBuffers(1,&ubo);
    std::cout<<"Water optics analytic GPU tests passed (5 cases)\n";
}
