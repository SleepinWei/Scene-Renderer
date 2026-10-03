#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include "renderer/rhi/FeatureScenes.h"
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
namespace {
std::shared_ptr<Texture> solid(glm::vec3 color) {
    auto texture=std::make_shared<Texture>();std::vector<unsigned char> pixels(48);
    for(int i=0;i<16;i++)for(int c=0;c<3;c++)pixels[i*3+c]=static_cast<unsigned char>(255*color[c]);
    texture->setPixels(4,4,3,std::move(pixels));
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
std::shared_ptr<RenderScene> render::makeFeatureScene() {
    auto result=std::make_shared<RenderScene>();result->setCamera(std::make_shared<Camera>(glm::vec3(0,6,18),glm::vec3(0,1,0),-90,-15));
    auto manager=RenderManager::GetInstance();
    ShaderType modes[]={ShaderType::PBR,ShaderType::PBR_CLEARCOAT,ShaderType::PBR_ANISOTROPY,ShaderType::PBR_SSS,ShaderType::PBR_TESS};
    for(int i=0;i<5;i++) {
        auto object=std::make_shared<GameObject>();object->name="RHI material "+std::to_string(i);
        auto trans=std::make_shared<Transform>();trans->setPosition({float(i-2)*2.6f,1.5f,0});object->addComponent(trans);
        auto filter=std::make_shared<MeshFilter>();auto mesh=sphere();mesh->setMaterial(material({.25f+.12f*i,.55f-.06f*i,.7f-.08f*i}));filter->addMesh(mesh);object->addComponent(filter);
        auto renderer=std::make_shared<MeshRenderer>();renderer->setShader(modes[i]);if(i==4)renderer->setDrawMode(GL_PATCHES);
        object->addComponent(renderer);object->setDeferred(i==0);result->addObject(object);
    }
    auto light=std::make_shared<GameObject>();light->name="Sun";light->addComponent(std::make_shared<Transform>());
    auto sun=std::make_shared<DirectionLight>();sun->setDirection(glm::normalize(glm::vec3(-.3f,-1,-.2f)));sun->setColor({3,3,3});light->addComponent(sun);result->addObject(light);
    auto point=std::make_shared<GameObject>();point->name="Point";auto pt=std::make_shared<Transform>();pt->setPosition({3,5,4});point->addComponent(pt);auto pl=std::make_shared<PointLight>();pl->setColor({20,15,10});point->addComponent(pl);result->addObject(point);
    auto spot=std::make_shared<GameObject>();spot->name="S0";auto st=std::make_shared<Transform>();st->setPosition({-4,7,6});spot->addComponent(st);auto sl=std::make_shared<SpotLight>();sl->setDirection(glm::normalize(-st->getPosition()));sl->setColor({2,2,2});spot->addComponent(sl);result->addObject(spot);
    auto sky=std::make_shared<Sky>();sky->addComponent(std::make_shared<Atmosphere>());sky->skybox->setInitialized(false);sky->skybox->addTexture(std::make_shared<Texture>(),"skybox");sky->width=sky->height=4;
    for(int face=0;face<6;face++){sky->data[face]=(unsigned char*)std::malloc(48);std::fill(sky->data[face],sky->data[face]+48,16);}result->addSky(sky);
    auto terrain=std::make_shared<Terrain>();auto tc=std::make_shared<TerrainComponent>();terrain->addComponent(tc);tc->initializeLegacyGrid();
    tc->updateSettings([&](auto& value){value.yScale=2;});tc->updateSettings([&](auto& value){value.yShift=-1;});tc->updateSettings([&](auto& value){value.model=glm::translate(glm::mat4(1),glm::vec3(0,-1,0))*glm::scale(glm::mat4(1),glm::vec3(100,2,100));});tc->updateSettings([&](auto& value){value.material=material({.35f,.48f,.18f});});
    auto height=std::make_shared<Texture>();height->setStorageDescriptor(128,128,GL_R32F,GL_RED);tc->settings().terrainMaterial->addTexture(height,"heightMap");
    std::vector<float> heights(128*128);for(int y=0;y<128;y++)for(int x=0;x<128;x++)heights[y*128+x]=.5f+.2f*std::sin(x*.08f)*std::cos(y*.07f);
    tc->setHeightData(128,128,std::move(heights));
    terrain->addComponent(std::make_shared<Grass>());auto ocean=std::make_shared<Ocean>();ocean->updateSettings([&](auto& value){value.FFTPow=9;});ocean->updateSettings([&](auto& value){value.fft_size=512;});ocean->updateSettings([&](auto& value){value.HeightScale=1;});ocean->updateSettings([&](auto& value){value.WindScale=16;});ocean->updateSettings([&](auto& value){value.MeshLength=100;});ocean->updateSettings([&](auto& value){value.MeshSize=257;});terrain->addComponent(ocean);result->addTerrain(terrain);
    return result;
}
std::shared_ptr<RenderScene> render::makeOceanScene(bool clearWater) {
    auto result=makeFeatureScene();
    if(!clearWater) { auto all=result->objects();for(const auto& o:all)if(!o->getComponent<DirectionLight>())result->removeObject(o->assetId); }
    else for(size_t i=0;i<result->objects().size();++i) {
        auto object=result->objects()[i];
        auto renderer=std::static_pointer_cast<MeshRenderer>(object->GetComponent("MeshRenderer"));
        if(!renderer)continue;
        object->setDeferred(true);
        auto trans=std::static_pointer_cast<Transform>(object->GetComponent("Transform"));
        trans->setPosition({float(int(i)-2)*2.6f,-2.6f,-5});trans->setScale(glm::vec3(1.4f));
        renderer->setShader(ShaderType::PBR);renderer->setDrawMode(GL_TRIANGLES);
    }
    {auto all=result->objects();for(const auto& o:all)if(o->getComponent<PointLight>() || o->getComponent<SpotLight>())result->removeObject(o->assetId);}
    result->setCamera(std::make_shared<Camera>(glm::vec3(0,7.5f,30),glm::vec3(0,1,0),-90,-10));result->mainCamera()->setZoom(58);result->mainCamera()->setExposure(1);
    if(!result->directionLights().empty())result->directionLights()[0]->setDirection({0,-.17364818f,.98480775f});
    auto ocean=std::make_shared<Ocean>();ocean->updateSettings([&](auto& value){value.FFTPow=10;});ocean->updateSettings([&](auto& value){value.fft_size=1024;});ocean->updateSettings([&](auto& value){value.MeshSize=513;});
    ocean->updateSettings([&](auto& value){value.MeshLength=256;});ocean->updateSettings([&](auto& value){value.seaLevel=0;});
    // A rough deep-water preset: larger swell, steep crests and compression-driven whitecaps.
    ocean->updateSettings([&](auto& value){value.WindScale=28;});ocean->updateSettings([&](auto& value){value.A=.0008f;});ocean->updateSettings([&](auto& value){value.HeightScale=1.8f;});ocean->updateSettings([&](auto& value){value.Lambda=1.15f;});
    ocean->updateSettings([&](auto& value){value.BubblesThreshold=.92f;});ocean->updateSettings([&](auto& value){value.BubblesScale=3;});
    ocean->updateSettings([&](auto& value){value.outer_OceanColorShallow={.2f,.75f,.85f};});ocean->updateSettings([&](auto& value){value.outer_OceanColorDeep={.035f,.28f,.35f};});
    if(clearWater) {
        ocean->updateSettings([&](auto& value){value.WindScale=9;});ocean->updateSettings([&](auto& value){value.A=.0005f;});ocean->updateSettings([&](auto& value){value.HeightScale=.6f;});ocean->updateSettings([&](auto& value){value.Lambda=.5f;});ocean->updateSettings([&](auto& value){value.refractionStrength=.35f;});
        ocean->updateSettings([&](auto& value){value.BubblesThreshold=.86f;});ocean->updateSettings([&](auto& value){value.BubblesScale=2;});
        ocean->updateSettings([&](auto& value){value.absorption={.08f,.025f,.012f};});ocean->updateSettings([&](auto& value){value.scattering={.01f,.02f,.025f};});
        result->setCamera(std::make_shared<Camera>(glm::vec3(0,9,13),glm::vec3(0,1,0),-90,-38));result->mainCamera()->setZoom(58);
        result->directionLights()[0]->setDirection({0,-.5735764f,.8191520f});
        std::static_pointer_cast<Atmosphere>(result->sky()->GetComponent("Atmosphere"))->updateSettings([](auto& value){value.sunAngle=35;});
        auto floor=std::make_shared<GameObject>();floor->name="Submerged sand";auto transform=std::make_shared<Transform>();floor->addComponent(transform);
        std::vector<Vertex> vertices(4);const glm::vec3 positions[]={{-45,-4,40},{45,-4,40},{45,-4,-50},{-45,-4,-50}};
        for(int i=0;i<4;++i){vertices[i]=Vertex{};vertices[i].Position=positions[i];vertices[i].Normal={0,1,0};vertices[i].TexCoords={float(i==1||i==2),float(i>=2)};}
        auto mesh=std::make_shared<Mesh>(vertices,std::vector<unsigned>{0,1,2,0,2,3});
        auto sample=std::static_pointer_cast<MeshFilter>(result->objects()[0]->GetComponent("MeshFilter"))->getMeshes()[0]->getMaterial();
        auto mat=std::make_shared<Material>();mat->setTextures(sample->getTextures());mat->setAlbedoFactor({2.4f,1.6f,.8f});mesh->setMaterial(mat);
        auto filter=std::make_shared<MeshFilter>();filter->addMesh(mesh);floor->addComponent(filter);
        auto renderer=std::make_shared<MeshRenderer>();renderer->setShader(ShaderType::PBR);floor->addComponent(renderer);floor->setDeferred(true);result->addObject(floor);
    }
    result->addTerrain(std::make_shared<Terrain>());result->terrain()->addComponent(ocean);
    auto manager=RenderManager::GetInstance();manager->setting.enableRSM=false;manager->setting.enableSSAO=false;
    manager->setting.useDefer=true;manager->setting.enableDirectional=true;
    return result;
}
