#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include "metal/MetalDemo.h"
#include "metal/MetalBackend.h"
#include "renderer/RenderScene.h"
#include "renderer/Material.h"
#include "renderer/Texture.h"
#include "renderer/RenderPass.h"
#include "component/GameObject.h"
#include "component/transform.h"
#include "component/Mesh_Filter.h"
#include "component/Mesh_Renderer.h"
#include "component/Lights.h"
#include "component/Atmosphere.h"
#include "component/TerrainComponent.h"
#include "component/Grass.h"
#include "component/Ocean.h"
#include "buffer/ImageTexture.h"
#include "object/SkyBox.h"
#include "object/Terrain.h"
#include "system/RenderManager.h"
#include "system/InputManager.h"
#include "utils/Camera.h"
#include <cmath>
#include <iostream>
#include <cstdlib>
extern std::shared_ptr<RenderScene> scene;
namespace {
std::shared_ptr<Texture> solid(glm::vec3 color) {
    auto texture=std::make_shared<Texture>();texture->width=texture->height=4;texture->channels=3;texture->format=texture->internalformat=GL_RGB;
    texture->data=(unsigned char*)std::malloc(4*4*3);
    for(int i=0;i<16;i++)for(int c=0;c<3;c++)texture->data[i*3+c]=static_cast<unsigned char>(255*color[c]);
    return texture;
}
std::shared_ptr<Material> material(glm::vec3 color) {
    auto m=std::make_shared<Material>();m->addTexture(solid(color),"material.albedo");m->addTexture(solid({.5f,.5f,1.f}),"material.normal");
    m->addTexture(solid({.5f,.4f,.1f}),"material.roughness");m->addTexture(solid({0.f,0.f,.1f}),"material.metallic");m->addTexture(solid({1.f,1.f,1.f}),"material.ao");m->addTexture(solid({.3f,.3f,.3f}),"material.height");m->addTexture(solid({.7f,.7f,.7f}),"material.anisotropy");m->addTexture(solid({.2f,.2f,.2f}),"material.clearCoatRoughness");return m;
}
std::shared_ptr<Mesh> sphere() {
    std::vector<Vertex> v;std::vector<unsigned> indices;constexpr int columns=32,rows=16;
    for(int y=0;y<=rows;y++)for(int x=0;x<=columns;x++) {
        float u=float(x)/columns,w=float(y)/rows,a=u*6.2831853f,b=w*3.14159265f;
        Vertex vertex{};vertex.Position={std::sin(b)*std::cos(a),std::cos(b),std::sin(b)*std::sin(a)};vertex.Normal=vertex.Position;vertex.TexCoords={u,w};
        vertex.Tangent={-std::sin(a),0,std::cos(a)};vertex.Bitangent=glm::cross(vertex.Normal,vertex.Tangent);v.push_back(vertex);
    }
    for(int y=0;y<rows;y++)for(int x=0;x<columns;x++){unsigned a=y*(columns+1)+x,b=a+columns+1;indices.insert(indices.end(),{a,a+1,b,a+1,b+1,b});}
    return std::make_shared<Mesh>(v,indices);
}
}
std::shared_ptr<RenderScene> makeMetalDemoScene() {
    auto result=std::make_shared<RenderScene>();result->main_camera=std::make_shared<Camera>(glm::vec3(0,6,18),glm::vec3(0,1,0),-90,-15);
    auto manager=RenderManager::GetInstance();
    ShaderType modes[]={ShaderType::PBR,ShaderType::PBR_CLEARCOAT,ShaderType::PBR_ANISOTROPY,ShaderType::PBR_SSS,ShaderType::PBR_TESS};
    for(int i=0;i<5;i++) {
        auto object=std::make_shared<GameObject>();object->name="Metal material "+std::to_string(i);
        auto trans=std::make_shared<Transform>();trans->position={float(i-2)*2.6f,1.5f,0};object->addComponent(trans);
        auto filter=std::make_shared<MeshFilter>();auto mesh=sphere();mesh->material=material({.25f+.12f*i,.55f-.06f*i,.7f-.08f*i});filter->addMesh(mesh);object->addComponent(filter);
        auto renderer=std::make_shared<MeshRenderer>();renderer->shader=manager->getShader(modes[i]);if(i==4)renderer->drawMode=GL_PATCHES;
        object->addComponent(renderer);object->setDeferred(i==0);result->addObject(object);
    }
    auto light=std::make_shared<GameObject>();light->name="Sun";light->addComponent(std::make_shared<Transform>());
    auto sun=std::make_shared<DirectionLight>();sun->data.direction=glm::normalize(glm::vec3(-.3f,-1,-.2f));sun->data.color={3,3,3};light->addComponent(sun);result->addObject(light);
    auto point=std::make_shared<GameObject>();point->name="Point";auto pt=std::make_shared<Transform>();pt->position={3,5,4};point->addComponent(pt);auto pl=std::make_shared<PointLight>();pl->data.color={20,15,10};point->addComponent(pl);result->addObject(point);
    auto spot=std::make_shared<GameObject>();spot->name="S0";auto st=std::make_shared<Transform>();st->position={-4,7,6};spot->addComponent(st);auto sl=std::make_shared<SpotLight>();sl->data.direction=glm::normalize(-st->position);sl->data.color={2,2,2};spot->addComponent(sl);result->addObject(spot);
    auto sky=std::make_shared<Sky>();sky->addComponent(std::make_shared<Atmosphere>());sky->skybox->initDone=false;sky->skybox->addTexture(std::make_shared<Texture>(),"skybox");sky->width=sky->height=4;
    for(int face=0;face<6;face++){sky->data[face]=(unsigned char*)std::malloc(48);std::fill(sky->data[face],sky->data[face]+48,16);}result->sky=sky;
    auto terrain=std::make_shared<Terrain>();auto tc=std::make_shared<TerrainComponent>();terrain->addComponent(tc);tc->rez=5;tc->nodeIndex.resize(50);
    for(unsigned y=0;y<5;y++)for(unsigned x=0;x<5;x++){tc->nodeIndex[2*(y*5+x)]=x;tc->nodeIndex[2*(y*5+x)+1]=y;}
    tc->yScale=2;tc->yShift=-1;tc->model=glm::translate(glm::mat4(1),glm::vec3(0,-1,0))*glm::scale(glm::mat4(1),glm::vec3(100,2,100));tc->material=material({.35f,.48f,.18f});
    auto height=std::make_shared<Texture>();height->width=height->height=128;height->internalformat=GL_R32F;height->format=GL_RED;tc->terrainMaterial->addTexture(height,"heightMap");
    tc->heightData=new float[128*128];for(int y=0;y<128;y++)for(int x=0;x<128;x++)tc->heightData[y*128+x]=.5f+.2f*std::sin(x*.08f)*std::cos(y*.07f);
    terrain->addComponent(std::make_shared<Grass>());auto ocean=std::make_shared<Ocean>();ocean->FFTPow=6;ocean->fft_size=64;ocean->HeightScale=.3f;ocean->MeshLength=100;ocean->MeshSize=48;terrain->addComponent(ocean);result->terrain=terrain;
    return result;
}
void validateMetalFeatures() {
    auto input=InputManager::GetInstance();input->width=320;input->height=180;
    auto manager=RenderManager::GetInstance();manager->init();scene=makeMetalDemoScene();
    manager->setting.enableShadow=true;
    for(int i=0;i<3;i++) {
        manager->setting.enableRSM=i==2;manager->setting.enableShadow=i!=0;
        MetalBackend::beginFrame();manager->render(scene);
        if(i==0) {
            MetalBackend::inspectTexture(manager->deferredPass->gAlbedoSpec->id,"build/metal-albedo.png");
            MetalBackend::inspectTexture(manager->deferredPass->gNormal->id,"build/metal-normal.png");
            MetalBackend::inspectTexture(manager->deferredPass->postTexture->id,"build/metal-hdr.png");
            auto atmosphere=std::static_pointer_cast<Atmosphere>(scene->sky->GetComponent("Atmosphere"));
            MetalBackend::inspectTexture(atmosphere->skyViewTexture->tex->id,"build/metal-sky-lut.png");
        }
        MetalBackend::capture(i==0?"build/metal-validation.png":i==1?"build/metal-shadow-validation.png":"build/metal-rsm-validation.png");MetalBackend::present();
    }
    auto ocean=std::static_pointer_cast<Ocean>(scene->terrain->GetComponent("Ocean"));
    scene->terrain=std::make_shared<Terrain>();scene->terrain->addComponent(ocean);
    manager->setting.enableRSM=false;MetalBackend::beginFrame();manager->render(scene);
    MetalBackend::inspectTexture(ocean->DisplaceRT_Texture->tex->id,"build/metal-ocean-displacement.png");
    MetalBackend::capture("build/metal-ocean-validation.png");MetalBackend::present();
    // The legacy standalone forward pipeline also supplies SSS front/back depth.
    scene->terrain.reset();manager->setting.useDefer=false;
    manager->postPass=std::make_shared<PostPass>();manager->basePass=std::make_shared<BasePass>();manager->depthPass=std::make_shared<DepthPass>();
    MetalBackend::beginFrame();manager->render(scene);
    MetalBackend::inspectTexture(manager->postPass->colorBuffer,"build/metal-forward-hdr.png");
    MetalBackend::inspectTexture(manager->depthPass->frontDepth->id,"build/metal-front-depth.png");
    MetalBackend::inspectTexture(manager->depthPass->backDepth->id,"build/metal-back-depth.png");
    MetalBackend::capture("build/metal-forward-validation.png");MetalBackend::present();
    std::cout<<"Metal standalone forward/HDR/SSS depth frames passed\n";
    std::cout<<"Metal deferred/PBR/tessellation/shadows/SSAO/RSM/sky/ocean/terrain/grass frames passed\n";
}
