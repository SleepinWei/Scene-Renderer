#include "metal/MetalBackend.h"
#include "metal/MetalDemo.h"
#include "renderer/RenderPass.h"
#include "renderer/Texture.h"
#include "component/Mesh_Filter.h"
#include <glm/gtc/matrix_transform.hpp>
#include "buffer/FrameBuffer.h"
#include "utils/Shader.h"
#include "utils/Utils.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
std::shared_ptr<Texture> filled(int size, glm::vec4 value) {
    auto texture = std::make_shared<Texture>();
    texture->genTexture(GL_RGBA32F, GL_RGBA, size, size);
    std::vector<glm::vec4> data(size * size, value);
    glBindTexture(GL_TEXTURE_2D, texture->id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, size, size, 0, GL_RGBA, GL_FLOAT, data.data());
    return texture;
}
void expect(glm::vec3 actual, glm::vec3 expected, const char* test) {
    for (int c=0;c<3;++c)
        if (!std::isfinite(actual[c]) || std::abs(actual[c]-expected[c]) > 2e-5f)
            throw std::runtime_error(std::string("RSM transport regression: ")+test+" actual="+std::to_string(actual[c])+" expected="+std::to_string(expected[c]));
}
}
void validateMetalRSM() {
    // Constant transport integrand has a closed-form disk integral. These cases
    // catch missing PDF/texel factors, material response, clipping and HDR saturation.
    MetalBackend::resize(8,8);
    RSMPass pass;
    auto base=filled(8,{.2f,.3f,.4f,1}), receiverPos=filled(8,{0,1,0,1});
    auto output=filled(8,{0,0,0,0});
    FrameBuffer framebuffer;framebuffer.bindTexture(output,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D);
    glm::mat4 projection(1);projection[1][1]=0;
    auto gather=[&](int resolution, int count, float radius, glm::vec3 reflectance,
                    float metal, glm::vec3 normal, bool valid, bool debug, float power) {
        auto flux=filled(resolution,{power/(resolution*resolution),power/(resolution*resolution),power/(resolution*resolution),1});
        auto n=filled(8,glm::vec4(normal,1));auto a=filled(8,glm::vec4(reflectance,1));auto m=filled(8,{.5f,metal,1,0});
        auto p=filled(resolution,{0,0,0,valid?1.f:0.f});
        auto sourceN=filled(resolution,{0,1,0,1});
        MetalBackend::beginFrame();framebuffer.bindBuffer();glViewport(0,0,8,8);glDisable(GL_DEPTH_TEST);
        auto shader=pass.indirectShader;shader->use();
        const std::shared_ptr<Texture> maps[]={sourceN,p,flux,base,receiverPos,n,a,m};
        const char* names[]={"normalMap","worldPosMap","fluxMap","inTexture","gPosition","gNormal","gAlbedoSpec","gPBR"};
        for(int i=0;i<8;++i){maps[i]->bind(GL_TEXTURE_2D,i);shader->setInt(names[i],i);}
        glActiveTexture(GL_TEXTURE8);glBindTexture(GL_TEXTURE_2D,pass.randomMap);shader->setInt("randomMap",8);
        shader->setMat4("lightSpaceMatrix",projection);shader->setInt("sample_num",count);
        shader->setFloat("sample_radius",radius);shader->setFloat("minDistance",.1f);
        shader->setFloat("rsmIntensity",1);shader->setInt("indirectOnly",debug?1:0);
        renderQuad();auto read=MetalBackend::readFloatTexture(output->id);MetalBackend::present();
        return glm::vec3(read.rgba[0],read.rgba[1],read.rgba[2]);
    };
    const glm::vec3 diffuse(.8f,.4f,.2f);
    const float radius=.25f, power=8, pi=std::acos(-1.f);
    const glm::vec3 expected=diffuse * (power*radius*radius/pi);
    expect(gather(16,64,radius,diffuse,0,{0,-1,0},true,true,power),expected,"analytic diffuse radiance");
    expect(gather(32,256,radius,diffuse,0,{0,-1,0},true,true,power),expected,"resolution and sample-count invariance");
    expect(gather(16,128,radius,diffuse,0,{0,-1,0},true,true,2*power),2.f*expected,"linear light power");
    expect(gather(16,128,radius,glm::vec3(0),0,{0,-1,0},true,true,power),glm::vec3(0),"black receiver");
    expect(gather(16,128,radius,diffuse,1,{0,-1,0},true,true,power),glm::vec3(0),"metal receiver has no diffuse bounce");
    expect(gather(16,128,radius,diffuse,0,{0,1,0},true,true,power),glm::vec3(0),"back-facing receiver");
    expect(gather(16,128,radius,diffuse,0,{0,-1,0},false,true,power),glm::vec3(0),"invalid background VPL");
    expect(gather(16,128,radius,diffuse,0,{0,0,0},true,false,power),glm::vec3(.2f,.3f,.4f),"preserve sky and forward pixels");
    expect(gather(16,128,radius,diffuse,0,{0,-1,0},true,false,power),expected+glm::vec3(.2f,.3f,.4f),"HDR additive composition");
    expect(gather(16,128,radius,diffuse,0,{0,-1,0},true,true,128),expected*16.f,"indirect HDR is not clamped to one");
    projection[3][0]=4;
    expect(gather(16,128,radius,diffuse,0,{0,-1,0},true,true,power),glm::vec3(0),"outside-map samples are zero");
    // Generate actual directional/sky RSM flux on a 2x2 world-unit patch.
    // Its analytic total reflected power is incident irradiance * area (4).
    auto normalsOut=filled(16,{0,0,0,0}), positionsOut=filled(16,{0,0,0,0}), fluxOut=filled(16,{0,0,0,0});
    FrameBuffer emissionBuffer;
    emissionBuffer.bindTexture(normalsOut,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D);
    emissionBuffer.bindTexture(positionsOut,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D);
    emissionBuffer.bindTexture(fluxOut,GL_COLOR_ATTACHMENT2,GL_TEXTURE_2D);
    const GLuint attachments[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2};
    emissionBuffer.bindBuffer();glDrawBuffers(3,attachments);
    std::vector<Vertex> vertices(4);
    const glm::vec3 corners[]={{-1,0,-1},{-1,0,1},{1,0,1},{1,0,-1}};
    const glm::vec2 uv[]={{0,0},{0,1},{1,1},{1,0}};
    for(int i=0;i<4;++i){vertices[i]=Vertex{};vertices[i].Position=corners[i];vertices[i].Normal={0,1,0};vertices[i].TexCoords=uv[i];}
    Mesh patch(vertices,{0,1,2,0,2,3});patch.genVAO();
    auto white=filled(2,{1,1,1,1}), flatNormal=filled(2,{.5f,.5f,1,1}), noMetal=filled(2,{0,0,0,1}), sky=filled(2,{.5f,.5f,.5f,1});
    auto emission=[&](glm::vec3 sunColor, bool skyOn, glm::vec3 direction) {
        MetalBackend::beginFrame();emissionBuffer.bindBuffer();glViewport(0,0,16,16);glDisable(GL_DEPTH_TEST);
        glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
        auto shader=pass.RSMShader;shader->use();
        shader->setMat4("model",glm::mat4(1));
        shader->setMat4("lightSpaceMatrix",glm::ortho(-1.f,1.f,-1.f,1.f,.1f,10.f)*glm::lookAt(glm::vec3(0,2,0),glm::vec3(0),glm::vec3(0,0,1)));
        shader->setInt("light.type",1);shader->setVec3("light.Color",sunColor);shader->setVec3("light.Direction",direction);
        shader->setFloat("alphaCutoff",0);shader->setVec3("albedoFactor",glm::vec3(1));
        white->bind(GL_TEXTURE_2D,0);shader->setInt("material.albedo",0);
        flatNormal->bind(GL_TEXTURE_2D,1);shader->setInt("material.normal",1);
        noMetal->bind(GL_TEXTURE_2D,2);shader->setInt("material.metallic",2);
        sky->bind(GL_TEXTURE_2D,3);shader->setInt("skyIrradiance",3);shader->setInt("enableSky",skyOn?1:0);
        glBindVertexArray(patch.VAO);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,patch.EBO);glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,0);
        auto read=MetalBackend::readFloatTexture(fluxOut->id);MetalBackend::present();
        glm::vec3 sum(0);for(size_t i=0;i<read.rgba.size();i+=4)sum+=glm::vec3(read.rgba[i],read.rgba[i+1],read.rgba[i+2]);
        return sum;
    };
    expect(emission(glm::vec3(2),false,{0,-1,0}),glm::vec3(8),"directional flux = irradiance times world area");
    expect(emission(glm::vec3(0),true,{0,-1,0}),glm::vec3(2*pi),"sky E/PI LUT to reflected power");
    expect(emission(glm::vec3(2),true,{0,-1,0}),glm::vec3(8+2*pi),"sun and sky powers add");
    expect(emission(glm::vec3(2),false,{0,1,0}),glm::vec3(0),"sun incidence rejects back illumination");
    glEnable(GL_DEPTH_TEST);
    std::cout<<"RSM analytic GPU transport tests passed (15 cases)\n";

}
